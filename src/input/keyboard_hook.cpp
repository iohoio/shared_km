#include "shared_km/input/keyboard_hook.hpp"
#include "shared_km/input/input_injector.hpp"

#include <array>
#include <utility>
#include <windows.h>

namespace shared_km::input {

namespace {

KeyboardHook* g_active_key_hook = nullptr;

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_active_key_hook != nullptr && g_active_key_hook->IsRunning()) {
        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        if (info != nullptr) {
            if (info->dwExtraInfo == static_cast<ULONG_PTR>(kInjectedMouseMarker)) {
                g_active_key_hook->OnInjectedKeyIgnored();
                return CallNextHookEx(nullptr, code, wparam, lparam);
            }

            KeyEventInfo key_info{};
            key_info.message = static_cast<unsigned int>(wparam);
            key_info.virtual_key_code = static_cast<unsigned int>(info->vkCode);
            key_info.scan_code = static_cast<unsigned int>(info->scanCode);
            key_info.flags = static_cast<unsigned int>(info->flags);
            if (g_active_key_hook->OnKeyEvent(key_info)) {
                return 1;  // block the key from local processing
            }
        }
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}

}  // namespace

KeyboardHook::KeyboardHook() = default;

KeyboardHook::~KeyboardHook() {
    Stop();
}

bool KeyboardHook::Start(KeyboardHookCallback callback) {
    if (running_) {
        return false;
    }

    callback_ = std::move(callback);
    seen_key_event_ = false;
    ignored_injected_key_ = false;
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
            static_cast<KeyboardHook*>(self)->RunLoop();
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

void KeyboardHook::Stop() {
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

bool KeyboardHook::IsRunning() const {
    return running_;
}

bool KeyboardHook::OnKeyEvent(const KeyEventInfo& info) {
    seen_key_event_ = true;
    if (callback_) {
        return callback_(info);
    }
    return false;
}

void KeyboardHook::OnInjectedKeyIgnored() {
    ignored_injected_key_ = true;
}

unsigned long KeyboardHook::LastError() const {
    return last_error_;
}

bool KeyboardHook::HasSeenKeyEvent() const {
    return seen_key_event_.load();
}

bool KeyboardHook::HasIgnoredInjectedKey() const {
    return ignored_injected_key_.load();
}

unsigned long KeyboardHook::ThreadId() const {
    return thread_id_;
}

std::string KeyboardHook::DesktopName() const {
    return desktop_name_;
}

void KeyboardHook::RunLoop() {
    g_active_key_hook = this;
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
    hook_handle_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    if (hook_handle_ == nullptr) {
        last_error_ = GetLastError();
    }
    SetEvent(static_cast<HANDLE>(ready_event_));

    if (hook_handle_ == nullptr) {
        g_active_key_hook = nullptr;
        return;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnhookWindowsHookEx(static_cast<HHOOK>(hook_handle_));
    hook_handle_ = nullptr;
    g_active_key_hook = nullptr;
}

}  // namespace shared_km::input
