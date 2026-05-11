#pragma once

namespace shared_km::input {

inline constexpr unsigned long long kInjectedMouseMarker = 0x53484B4D4F555345ULL;

struct NativePoint {
    long x = 0;
    long y = 0;
};

struct MousePosition {
    int x = 0;
    int y = 0;
};

using GetCursorPosFn = bool (*)(NativePoint& point);

class InputInjector {
public:
    bool MoveMouseAbsolute(int x, int y) const;
    bool MoveMouseRelative(int dx, int dy) const;
    bool LeftButtonDown() const;
    bool LeftButtonUp() const;
    bool RightButtonDown() const;
    bool RightButtonUp() const;
    bool MouseWheel(short delta) const;
    bool KeyDown(unsigned int virtual_key_code, unsigned int scan_code, unsigned int flags) const;
    bool KeyUp(unsigned int virtual_key_code, unsigned int scan_code, unsigned int flags) const;
    unsigned long LastError() const;
};

MousePosition GetCurrentMousePosition();
MousePosition GetCurrentMousePosition(GetCursorPosFn get_cursor_pos);

}  // namespace shared_km::input
