#include <bl_core/image_sequence.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>

namespace bl {

namespace {

std::string fileNameOf(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string dirNameOf(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string extensionOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot == name.size() - 1) {
        return {};
    }
    return name.substr(dot + 1);
}

bool isAllDigits(const std::string& s) {
    return !s.empty() &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) {
               return std::isdigit(c);
           });
}

} // namespace

std::optional<ImageSequence> detectImageSequence(const std::string& filePath,
                                                 int minCount) {
    if (minCount < 2) minCount = 2;

    std::error_code ec;
    const std::filesystem::path candidate(filePath);
    if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
        return std::nullopt;
    }

    const std::string name = fileNameOf(filePath);
    const std::string ext = extensionOf(name);
    if (ext.empty()) return std::nullopt;

    // "stem" + digits + "." + ext — the digit run is terminated by the dot.
    const std::string base = name.substr(0, name.size() - ext.size() - 1);
    size_t runStart = base.size();
    while (runStart > 0 &&
           std::isdigit(static_cast<unsigned char>(base[runStart - 1]))) {
        --runStart;
    }
    const size_t runLen = base.size() - runStart;
    if (runLen < 2) return std::nullopt;
    const std::string stem = base.substr(0, runStart);

    int firstIndex = 0;
    try {
        firstIndex = std::stoi(base.substr(runStart));
    } catch (...) {
        return std::nullopt;
    }

    // Scan the directory for siblings sharing stem + exact-width digits + ext.
    std::set<int> indices;
    const std::filesystem::path dir(dirNameOf(filePath));
    std::filesystem::directory_iterator it(dir, ec);
    const std::filesystem::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string sibling = it->path().filename().string();
        if (extensionOf(sibling) != ext) continue;
        if (sibling.compare(0, stem.size(), stem) != 0) continue;
        const std::string tail = sibling.substr(stem.size());
        // tail == <digits>.<ext> with exactly runLen digits.
        if (tail.size() != runLen + ext.size() + 1) continue;
        if (tail[runLen] != '.') continue;
        if (tail.compare(runLen + 1, ext.size(), ext) != 0) continue;
        const std::string digits = tail.substr(0, runLen);
        if (!isAllDigits(digits)) continue;
        try {
            indices.insert(std::stoi(digits));
        } catch (...) {
            continue;
        }
    }
    if (ec) return std::nullopt;
    if (indices.empty() || indices.count(firstIndex) == 0) return std::nullopt;
    if (static_cast<int>(indices.size()) < minCount) return std::nullopt;

    ImageSequence seq;
    seq.directory = dirNameOf(filePath);
    seq.stem = stem;
    seq.extension = ext;
    seq.digits = static_cast<int>(runLen);
    seq.firstIndex = firstIndex;
    seq.frameCount = static_cast<int>(indices.size());
    seq.patternPath =
        seq.directory + "/" + stem + "%0" + std::to_string(runLen) + "d." + ext;
    return seq;
}

} // namespace bl