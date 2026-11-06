#include <bl_audio/device_output.hpp>
#include <bl_core/result.hpp>

#if defined(_WIN32)

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif

#pragma comment(lib, "winmm.lib")

#include <string>
#include <vector>

namespace bl::detail {
namespace {

class WinMmOutput final : public IDeviceOutput {
public:
    ~WinMmOutput() override { close(); }

    Result<void> open(const AudioMixConfig& config) override {
        close();

        format_.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format_.nChannels = static_cast<WORD>(config.channels);
        format_.nSamplesPerSec = config.sampleRate;
        format_.wBitsPerSample = 32;
        format_.nBlockAlign = static_cast<WORD>(config.channels * sizeof(float));
        format_.nAvgBytesPerSec = config.sampleRate * format_.nBlockAlign;
        format_.cbSize = 0;

        doneEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!doneEvent_) {
            return Result<void>::err(Err::Internal, "waveOut: CreateEvent failed");
        }

        const MMRESULT rc = waveOutOpen(&handle_, WAVE_MAPPER, &format_,
                                        reinterpret_cast<DWORD_PTR>(doneEvent_), 0,
                                        CALLBACK_EVENT);
        if (rc != MMSYSERR_NOERROR) {
            CloseHandle(doneEvent_);
            doneEvent_ = nullptr;
            return Result<void>::err(Err::Internal,
                                     "waveOutOpen failed: " + std::to_string(rc));
        }
        opened_ = true;
        return Result<void>{};
    }

    void write(const AudioBuffer& buffer) override {
        if (!opened_ || !handle_ || buffer.sampleCount == 0) return;

        scratch_.resize(static_cast<size_t>(buffer.sampleCount) * buffer.channels);
        interleaveTo(buffer, scratch_.data());

        WAVEHDR header{};
        header.lpData = reinterpret_cast<LPSTR>(scratch_.data());
        header.dwBufferLength =
            static_cast<DWORD>(scratch_.size() * sizeof(float));
        if (waveOutPrepareHeader(handle_, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            return;
        }

        ResetEvent(doneEvent_);
        if (waveOutWrite(handle_, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(handle_, &header, sizeof(header));
            return;
        }

        WaitForSingleObject(doneEvent_, 10000);
        waveOutUnprepareHeader(handle_, &header, sizeof(header));
    }

    void close() override {
        if (handle_) {
            waveOutReset(handle_);
            waveOutClose(handle_);
            handle_ = nullptr;
        }
        if (doneEvent_) {
            CloseHandle(doneEvent_);
            doneEvent_ = nullptr;
        }
        opened_ = false;
    }

    bool blocksOnWrite() const noexcept override { return true; }

private:
    HWAVEOUT handle_{nullptr};
    WAVEFORMATEX format_{};
    HANDLE doneEvent_{nullptr};
    bool opened_{false};
    std::vector<float> scratch_;
};

} // namespace

std::unique_ptr<IDeviceOutput> createWinMmOutput() {
    return std::make_unique<WinMmOutput>();
}

} // namespace bl::detail

#endif // _WIN32
