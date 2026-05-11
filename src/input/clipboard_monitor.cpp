#include "shared_km/input/clipboard_monitor.hpp"

#include <windows.h>
#include <string>
#include <thread>

namespace shared_km::input {

namespace {

const wchar_t kWindowClass[] = L"SharedKM_ClipboardWindow";

LRESULT CALLBACK ClipboardWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CLIPBOARDUPDATE) {
        auto* monitor = reinterpret_cast<ClipboardMonitor*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!monitor) return 0;

        if (OpenClipboard(hwnd)) {
            HANDLE data = GetClipboardData(CF_UNICODETEXT);
            if (data) {
                auto* wtext = static_cast<const wchar_t*>(GlobalLock(data));
                if (wtext) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 1) {
                        std::string utf8(static_cast<std::size_t>(len - 1), '\0');
                        WideCharToMultiByte(CP_UTF8, 0, wtext, -1, utf8.data(), len, nullptr, nullptr);
                        // Invoke callback while clipboard is NOT open (to avoid deadlock)
                        GlobalUnlock(data);
                        CloseClipboard();
                        if (monitor) {
                            // Re-fetch monitor in case it was modified during CloseClipboard
                            auto* m = reinterpret_cast<ClipboardMonitor*>(
                                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                            if (m) m->OnClipboardChange(utf8);
                        }
                        return 0;
                    }
                    GlobalUnlock(data);
                }
            }
            CloseClipboard();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

struct ThreadStartParam {
    ClipboardMonitor* monitor;
    HWND* hwnd_out;
    std::string* error_out;
};

DWORD WINAPI ClipboardThreadProc(LPVOID param) {
    auto* tp = static_cast<ThreadStartParam*>(param);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ClipboardWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        *tp->error_out = "CreateWindow failed";
        return 1;
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tp->monitor));
    *tp->hwnd_out = hwnd;

    if (!AddClipboardFormatListener(hwnd)) {
        *tp->error_out = "AddClipboardFormatListener failed";
        DestroyWindow(hwnd);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    RemoveClipboardFormatListener(hwnd);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    DestroyWindow(hwnd);
    return 0;
}

} // namespace

ClipboardMonitor::~ClipboardMonitor() {
    Stop();
}

void ClipboardMonitor::OnClipboardChange(const std::string& utf8_text) {
    if (callback_) {
        callback_(utf8_text);
    }
}

bool ClipboardMonitor::Start(Callback callback) {
    Stop();

    if (!callback) {
        last_error_ = "callback is null";
        return false;
    }

    callback_ = std::move(callback);

    HWND hwnd = nullptr;
    ThreadStartParam tp{};
    tp.monitor = this;
    tp.hwnd_out = &hwnd;
    tp.error_out = &last_error_;

    thread_handle_ = CreateThread(nullptr, 0, ClipboardThreadProc, &tp, 0, &thread_id_);
    if (!thread_handle_) {
        last_error_ = "CreateThread failed";
        return false;
    }

    // Wait for thread to create the window (up to 3 seconds)
    for (int i = 0; i < 300; i++) {
        if (hwnd != nullptr || !last_error_.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (hwnd == nullptr) {
        if (last_error_.empty()) {
            last_error_ = "timeout waiting for window";
        }
        CloseHandle(thread_handle_);
        thread_handle_ = nullptr;
        thread_id_ = 0;
        return false;
    }

    hwnd_ = static_cast<void*>(hwnd);
    return true;
}

void ClipboardMonitor::Stop() {
    if (hwnd_) {
        // Clear callback to prevent any further invocations
        callback_ = nullptr;
        PostMessageW(static_cast<HWND>(hwnd_), WM_QUIT, 0, 0);
        hwnd_ = nullptr;
    }
    if (thread_handle_) {
        if (WaitForSingleObject(thread_handle_, 3000) == WAIT_TIMEOUT) {
            TerminateThread(thread_handle_, 1);
        }
        CloseHandle(thread_handle_);
        thread_handle_ = nullptr;
    }
    thread_id_ = 0;
}

} // namespace shared_km::input
