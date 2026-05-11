#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace shared_km::input {

enum class MouseEventType {
    Move,
    LeftDown,
    LeftUp,
    RightDown,
    RightUp,
    MiddleDown,
    MiddleUp,
    Wheel,
};

struct MouseEvent {
    MouseEventType type = MouseEventType::Move;
    int x = 0;
    int y = 0;
    int wheel_delta = 0;
};

using MouseHookCallback = std::function<bool(const MouseEvent& event)>;

class MouseHook {
public:
    MouseHook();
    ~MouseHook();

    MouseHook(const MouseHook&) = delete;
    MouseHook& operator=(const MouseHook&) = delete;

    bool Start(MouseHookCallback callback);
    void Stop();
    bool IsRunning() const;
    void OnInjectedMouseMoveIgnored();
    bool OnMouseEvent(const MouseEvent& event);
    unsigned long LastError() const;
    bool HasSeenMouseMove() const;
    bool HasIgnoredInjectedMouseMove() const;
    unsigned long ThreadId() const;
    std::string DesktopName() const;

private:
    void RunLoop();

    MouseHookCallback callback_;
    void* ready_event_ = nullptr;
    void* thread_handle_ = nullptr;
    unsigned long thread_id_ = 0;
    void* hook_handle_ = nullptr;
    bool running_ = false;
    std::atomic<bool> seen_mouse_move_{false};
    std::atomic<bool> ignored_injected_mouse_move_{false};
    unsigned long last_error_ = 0;
    std::string desktop_name_;
};

}  // namespace shared_km::input
