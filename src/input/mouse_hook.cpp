#include "shared_km/input/mouse_hook.hpp"
#include "shared_km/input/input_injector.hpp"

#include <array>
#include <utility>
#include <windows.h>

namespace shared_km::input {

namespace {

MouseHook* g_active_hook = nullptr;

MouseEventType WParamToMouseEventType(WPARAM wparam) {
    switch (wparam) {
    case WM_MOUSEMOVE:    return MouseEventType::Move;
    case WM_LBUTTONDOWN:  return MouseEventType::LeftDown;
    case WM_LBUTTONUP:    return MouseEventType::LeftUp;
    case WM_RBUTTONDOWN:  return MouseEventType::RightDown;
    case WM_RBUTTONUP:    return MouseEventType::RightUp;
    case WM_MBUTTONDOWN:  return MouseEventType::MiddleDown;
    case WM_MBUTTONUP:    return MouseEventType::MiddleUp;
    case WM_MOUSEWHEEL:   return MouseEventType::Wheel;
    default:              return MouseEventType::Move;
    }
}

LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_active_hook != nullptr && g_active_hook->IsRunning()) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
        if (info != nullptr) {
            // Ignore injected mouse events (from our own SendInput)
            if (info->dwExtraInfo == static_cast<ULONG_PTR>(kInjectedMouseMarker)) {
                if (wparam == WM_MOUSEMOVE) {
                    g_active_hook->OnInjectedMouseMoveIgnored();
                }
                return CallNextHookEx(nullptr, code, wparam, lparam);
            }

            int wheel_delta = 0;
            if (wparam == WM_MOUSEWHEEL) {
                wheel_delta = GET_WHEEL_DELTA_WPARAM(info->mouseData);
            }

            MouseEvent event;
            event.type = WParamToMouseEventType(wparam);
            event.x = info->pt.x;
            event.y = info->pt.y;
            event.wheel_delta = wheel_delta;
            if (g_active_hook->OnMouseEvent(event)) {
                return 1;  // block the event locally
            }
        }
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}

}  // namespace

MouseHook::MouseHook() = default;

MouseHook::~MouseHook() {
    Stop();
}

bool MouseHook::Start(MouseHookCallback callback) {
    if (running_) {
        return false;
    }

    callback_ = std::move(callback);
    seen_mouse_move_ = false;
    ignored_injected_mouse_move_ = false;
    last_error_ = 0;
    desktop_name_.clear();
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ready_event_ == nullptr) {
        last_error_ = GetLastError();
        return false;
    }

    thread_handle_ = CreateThread(
        nullptr,
        0,
        [](LPVOID self) -> DWORD {
            static_cast<MouseHook*>(self)->RunLoop();
            return 0;
        },
        this,
        0,
        &thread_id_
    );
    if (thread_handle_ == nullptr) {
        last_error_ = GetLastError();
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        return false;
    }

    const auto wait_result = WaitForSingleObject(ready_event_, 2000);
    running_ = wait_result == WAIT_OBJECT_0 && hook_handle_ != nullptr;
    return running_;
}

void MouseHook::Stop() {
    if (thread_id_ != 0) {
        PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    }

    if (thread_handle_ != nullptr) {
        WaitForSingleObject(thread_handle_, 2000);
        CloseHandle(thread_handle_);
        thread_handle_ = nullptr;
    }

    if (ready_event_ != nullptr) {
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
    }

    callback_ = {};
    hook_handle_ = nullptr;
    thread_id_ = 0;
    running_ = false;
}

bool MouseHook::IsRunning() const {
    return running_;
}

bool MouseHook::OnMouseEvent(const MouseEvent& event) {
    if (event.type == MouseEventType::Move) {
        seen_mouse_move_ = true;
    }
    if (callback_) {
        return callback_(event);
    }
    return false;
}

void MouseHook::OnInjectedMouseMoveIgnored() {
    ignored_injected_mouse_move_ = true;
}

unsigned long MouseHook::LastError() const {
    return last_error_;
}

bool MouseHook::HasSeenMouseMove() const {
    return seen_mouse_move_.load();
}

bool MouseHook::HasIgnoredInjectedMouseMove() const {
    return ignored_injected_mouse_move_.load();
}

unsigned long MouseHook::ThreadId() const {
    return thread_id_;
}

std::string MouseHook::DesktopName() const {
    return desktop_name_;
}

void MouseHook::RunLoop() {
    g_active_hook = this;
    if (const auto desktop = GetThreadDesktop(GetCurrentThreadId()); desktop != nullptr) {
        std::array<wchar_t, 256> name{};
        DWORD needed = 0;
        if (GetUserObjectInformationW(
                desktop,
                UOI_NAME,
                name.data(),
                static_cast<DWORD>(name.size() * sizeof(wchar_t)),
                &needed
            )) {
            const int size = WideCharToMultiByte(CP_UTF8, 0, name.data(), -1, nullptr, 0, nullptr, nullptr);
            if (size > 1) {
                std::string utf8(static_cast<std::size_t>(size - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, name.data(), -1, utf8.data(), size, nullptr, nullptr);
                desktop_name_ = utf8;
            }
        }
    }
    hook_handle_ = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    if (hook_handle_ == nullptr) {
        last_error_ = GetLastError();
    }
    SetEvent(static_cast<HANDLE>(ready_event_));

    if (hook_handle_ == nullptr) {
        g_active_hook = nullptr;
        return;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnhookWindowsHookEx(static_cast<HHOOK>(hook_handle_));
    hook_handle_ = nullptr;
    g_active_hook = nullptr;
}

}  // namespace shared_km::input
