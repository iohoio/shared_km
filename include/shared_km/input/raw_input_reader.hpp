#pragma once

#include <atomic>
#include <cstdint>

namespace shared_km::input {

class RawInputReader {
public:
    RawInputReader() = default;
    ~RawInputReader();

    RawInputReader(const RawInputReader&) = delete;
    RawInputReader& operator=(const RawInputReader&) = delete;

    bool Start();
    void Stop();

    // Atomically consume accumulated delta since last call.
    void ReadDelta(int& dx, int& dy);

    // Called from the hidden window's WM_INPUT handler (static WndProc).
    void AccumulateDelta(int dx, int dy);

    bool IsRunning() const { return running_; }

private:
    void RunLoop();

    std::atomic<long> delta_x_{0};
    std::atomic<long> delta_y_{0};
    std::atomic<bool> running_{false};

    void* thread_handle_ = nullptr;
    unsigned long thread_id_ = 0;
    void* ready_event_ = nullptr;
};

} // namespace shared_km::input
