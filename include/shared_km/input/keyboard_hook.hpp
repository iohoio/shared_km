#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace shared_km::input {

struct KeyEventInfo {
    unsigned int message = 0;  // WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP
    unsigned int virtual_key_code = 0;
    unsigned int scan_code = 0;
    unsigned int flags = 0;
};

using KeyboardHookCallback = std::function<bool(const KeyEventInfo& info)>;

class KeyboardHook {
public:
    KeyboardHook();
    ~KeyboardHook();

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

    bool Start(KeyboardHookCallback callback);
    void Stop();
    bool IsRunning() const;
    bool OnKeyEvent(const KeyEventInfo& info);
    void OnInjectedKeyIgnored();
    unsigned long LastError() const;
    bool HasSeenKeyEvent() const;
    bool HasIgnoredInjectedKey() const;
    unsigned long ThreadId() const;
    std::string DesktopName() const;

private:
    void RunLoop();

    KeyboardHookCallback callback_;
    void* ready_event_ = nullptr;
    void* thread_handle_ = nullptr;
    unsigned long thread_id_ = 0;
    void* hook_handle_ = nullptr;
    bool running_ = false;
    std::atomic<bool> seen_key_event_{false};
    std::atomic<bool> ignored_injected_key_{false};
    unsigned long last_error_ = 0;
    std::string desktop_name_;
};

}  // namespace shared_km::input
