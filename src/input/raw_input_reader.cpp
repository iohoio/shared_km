#include "shared_km/input/raw_input_reader.hpp"

#include <windows.h>

namespace shared_km::input {

namespace {

// Window class for the hidden message window.
const wchar_t kRawInputWindowClass[] = L"SharedKM_RawInputWindow";

LRESULT CALLBACK RawInputWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_INPUT) {
        auto* reader = reinterpret_cast<RawInputReader*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (reader == nullptr) {
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        UINT size = sizeof(RAWINPUT);
        RAWINPUT raw{};
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT,
                            &raw, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1)) {
            if (raw.header.dwType == RIM_TYPEMOUSE) {
                const auto& mouse = raw.data.mouse;
                // lLastX/lLastY contain relative motion when the mouse is not in absolute mode
                if (mouse.usFlags == MOUSE_MOVE_RELATIVE || mouse.usFlags == 0) {
                    reader->AccumulateDelta(mouse.lLastX, mouse.lLastY);
                }
            }
        }
        return 0;
    }

    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

RawInputReader::~RawInputReader() {
    Stop();
}

bool RawInputReader::Start() {
    if (running_) {
        return false;
    }

    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ready_event_ == nullptr) {
        return false;
    }

    thread_handle_ = CreateThread(
        nullptr, 0,
        [](LPVOID self) -> DWORD {
            static_cast<RawInputReader*>(self)->RunLoop();
            return 0;
        },
        this, 0, &thread_id_);

    if (thread_handle_ == nullptr) {
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        return false;
    }

    const auto wait = WaitForSingleObject(ready_event_, 2000);
    return wait == WAIT_OBJECT_0;
}

void RawInputReader::Stop() {
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
    thread_id_ = 0;
    running_ = false;
}

void RawInputReader::ReadDelta(int& dx, int& dy) {
    dx = static_cast<int>(delta_x_.exchange(0));
    dy = static_cast<int>(delta_y_.exchange(0));
}

void RawInputReader::AccumulateDelta(int dx, int dy) {
    delta_x_.fetch_add(dx);
    delta_y_.fetch_add(dy);
}

void RawInputReader::RunLoop() {
    running_ = true;

    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = RawInputWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kRawInputWindowClass;
    RegisterClassExW(&wc);

    // Create hidden window
    HWND hwnd = CreateWindowExW(0, kRawInputWindowClass, L"RawInputWindow",
                                0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        SetEvent(static_cast<HANDLE>(ready_event_));
        running_ = false;
        return;
    }

    // Store this pointer in window user data
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Register for raw mouse input (RIDEV_INPUTSINK = receive even when not foreground)
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // HID usage page: Generic Desktop
    rid.usUsage = 0x02;     // HID usage: Mouse
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DestroyWindow(hwnd);
        SetEvent(static_cast<HANDLE>(ready_event_));
        running_ = false;
        return;
    }

    SetEvent(static_cast<HANDLE>(ready_event_));

    // Message loop
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyWindow(hwnd);
    running_ = false;
}

} // namespace shared_km::input
