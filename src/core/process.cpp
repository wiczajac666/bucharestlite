#include <bl_core/process.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace bl {
namespace {

std::string tailOf(const std::string& text, size_t maxChars = 800) {
    if (text.size() <= maxChars) return text;
    return text.substr(text.size() - maxChars);
}

std::string readAndRemove(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    in.close();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    // Normalize CRLF (Windows) line endings to LF so captured output is
    // consistent across platforms (e.g. "cmd /c echo hello" emits CRLF).
    const std::string raw = buffer.str();
    std::string normalized;
    normalized.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') {
            continue;
        }
        normalized.push_back(raw[i]);
    }
    return tailOf(normalized);
}

#ifdef _WIN32

// Quote one argument for the CommandLineToArgvW / CRT parsing rules.
std::string windowsQuote(const std::string& arg) {
    if (!arg.empty() &&
        arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }
    std::string out = "\"";
    size_t backslashes = 0;
    for (const char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, '\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

#endif

} // namespace

#ifdef _WIN32

ProcessResult runProcess(const std::vector<std::string>& argv,
                         const std::function<bool()>& shouldCancel) {
    ProcessResult result;
    if (argv.empty()) return result;

    std::string commandLine;
    for (const auto& arg : argv) {
        if (!commandLine.empty()) commandLine.push_back(' ');
        commandLine += windowsQuote(arg);
    }

    wchar_t tempDir[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDir) == 0 ||
        GetTempFileNameW(tempDir, L"blp", 0, tempFile) == 0) {
        return result;
    }
    const std::filesystem::path logPath(tempFile);

    SECURITY_ATTRIBUTES attrs{sizeof(attrs), nullptr, TRUE};
    HANDLE log = CreateFileW(tempFile, GENERIC_WRITE | GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &attrs,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        DeleteFileW(tempFile);
        return result;
    }
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &attrs,
                             OPEN_EXISTING, 0, nullptr);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;

    PROCESS_INFORMATION process{};
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');
    const BOOL created = CreateProcessA(
        nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(log);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);

    if (!created) {
        result.output = readAndRemove(logPath);
        return result;
    }
    result.started = true;

    for (;;) {
        const DWORD wait = WaitForSingleObject(process.hProcess, 50);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) break;
        if (shouldCancel && shouldCancel()) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
            result.cancelled = true;
            break;
        }
    }

    DWORD exitCode = 1;
    if (GetExitCodeProcess(process.hProcess, &exitCode)) {
        result.exitCode = static_cast<int>(exitCode);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    result.output = readAndRemove(logPath);
    return result;
}

#else

ProcessResult runProcess(const std::vector<std::string>& argv,
                         const std::function<bool()>& shouldCancel) {
    ProcessResult result;
    if (argv.empty()) return result;

    std::string pathTemplate =
        (std::filesystem::temp_directory_path() / "bl-process-XXXXXX").string();
    std::vector<char> pathBuffer(pathTemplate.begin(), pathTemplate.end());
    pathBuffer.push_back('\0');
    const int logFd = mkstemp(pathBuffer.data());
    if (logFd < 0) return result;
    const std::filesystem::path logPath(pathBuffer.data());

    std::vector<char*> childArgv;
    childArgv.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
        childArgv.push_back(const_cast<char*>(arg.c_str()));
    }
    childArgv.push_back(nullptr);

    // CLOEXEC pipe used by the child to report an exec() failure. On a
    // successful exec the write end closes and the parent reads EOF; on
    // failure the child writes errno before exiting, so a missing binary is
    // distinguishable from a program that ran and returned 127 itself.
    int execPipe[2];
    if (pipe(execPipe) != 0) {
        close(logFd);
        std::error_code ec;
        std::filesystem::remove(logPath, ec);
        return result;
    }
    fcntl(execPipe[1], F_SETFD, FD_CLOEXEC);

    const pid_t pid = fork();
    if (pid < 0) {
        close(execPipe[0]);
        close(execPipe[1]);
        close(logFd);
        std::error_code ec;
        std::filesystem::remove(logPath, ec);
        return result;
    }
    if (pid == 0) {
        // Child: route both streams to the log file and exec. Never returns on
        // success; _exit avoids running the parent's atexit handlers.
        close(execPipe[0]);
        dup2(logFd, STDOUT_FILENO);
        dup2(logFd, STDERR_FILENO);
        close(logFd);
        execvp(childArgv[0], childArgv.data());
        const int execErrno = errno;
        const ssize_t written =
            write(execPipe[1], &execErrno, sizeof(execErrno));
        (void)written;
        _exit(127);
    }

    close(logFd);
    close(execPipe[1]);
    int execErrno = 0;
    ssize_t execReport = 0;
    do {
        execReport = read(execPipe[0], &execErrno, sizeof(execErrno));
    } while (execReport < 0 && errno == EINTR);
    close(execPipe[0]);
    result.started = execReport != static_cast<ssize_t>(sizeof(execErrno));

    int status = 0;
    for (;;) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) break;
        if (waited < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (shouldCancel && shouldCancel()) {
            kill(pid, SIGTERM);
            // Give the encoder a moment to clean up before insisting.
            bool reaped = false;
            for (int i = 0; i < 20; ++i) {
                const pid_t again = waitpid(pid, &status, WNOHANG);
                if (again == pid) {
                    reaped = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!reaped) {
                kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
            }
            result.cancelled = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.output = readAndRemove(logPath);
    return result;
}

#endif

} // namespace bl
