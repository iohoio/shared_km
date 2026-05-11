#include "shared_km/input/input_injector.hpp"

#include <windows.h>

namespace shared_km::input {

namespace {

bool GetCursorPosAdapter(NativePoint& point) {
    POINT win_point{};
    if (!GetCursorPos(&win_point)) {
        return false;
    }

    point.x = win_point.x;
    point.y = win_point.y;
    return true;
}

}  // namespace

bool InputInjector::MoveMouseAbsolute(int x, int y) const {
    const auto screen_width = GetSystemMetrics(SM_CXSCREEN);
    const auto screen_height = GetSystemMetrics(SM_CYSCREEN);
    if (screen_width <= 0 || screen_height <= 0) {
        return false;
    }

    const auto normalized_x = static_cast<LONG>((static_cast<double>(x) * 65535.0) / (screen_width - 1));
    const auto normalized_y = static_cast<LONG>((static_cast<double>(y) * 65535.0) / (screen_height - 1));

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = normalized_x;
    input.mi.dy = normalized_y;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    input.mi.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedMouseMarker);

    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputInjector::MoveMouseRelative(int dx, int dy) const {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(dx);
    input.mi.dy = static_cast<LONG>(dy);
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedMouseMarker);

    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputInjector::LeftButtonDown() const {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input.mi.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedMouseMarker);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputInjector::RightButtonDown() const {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    input.mi.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedMouseMarker);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputInjector::RightButtonUp() const {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    input.mi.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedMouseMarker);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputInjector::MouseWheel(short delta) const {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.mouseData = static_cast<DWORD>(delta);
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedMouseMarker);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputInjector::LeftButtonUp() const {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    input.mi.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedMouseMarker);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputInjector::KeyDown(unsigned int virtual_key_code, unsigned int scan_code, unsigned int flags) const {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(virtual_key_code);
    input.ki.wScan = static_cast<WORD>(scan_code);
    input.ki.dwFlags = (flags & 0x01) ? KEYEVENTF_EXTENDEDKEY : 0;
    input.ki.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedMouseMarker);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool InputInjector::KeyUp(unsigned int virtual_key_code, unsigned int scan_code, unsigned int flags) const {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(virtual_key_code);
    input.ki.wScan = static_cast<WORD>(scan_code);
    input.ki.dwFlags = KEYEVENTF_KEYUP | ((flags & 0x01) ? KEYEVENTF_EXTENDEDKEY : 0);
    input.ki.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedMouseMarker);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

unsigned long InputInjector::LastError() const {
    return GetLastError();
}

MousePosition GetCurrentMousePosition() {
    return GetCurrentMousePosition(GetCursorPosAdapter);
}

MousePosition GetCurrentMousePosition(GetCursorPosFn get_cursor_pos) {
    if (get_cursor_pos == nullptr) {
        return {};
    }

    NativePoint point{};
    if (!get_cursor_pos(point)) {
        return {};
    }

    return MousePosition{
        .x = static_cast<int>(point.x),
        .y = static_cast<int>(point.y),
    };
}

}  // namespace shared_km::input
