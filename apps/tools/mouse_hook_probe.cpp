#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <windows.h>

#include "shared_km/input/input_injector.hpp"
#include "shared_km/input/mouse_hook.hpp"

int main() {
    shared_km::input::MouseHook hook;

    if (!hook.Start([](const shared_km::input::MouseEvent& event) {
            std::cout << "HOOK_EVENT type=" << static_cast<int>(event.type)
                      << " x=" << event.x << " y=" << event.y;
            if (event.type == shared_km::input::MouseEventType::Wheel)
                std::cout << " delta=" << event.wheel_delta;
            std::cout << '\n';
        })) {
        std::cerr << "HOOK_START_FAIL " << hook.LastError() << '\n';
        return 1;
    }

    std::cout << "HOOK_THREAD " << hook.ThreadId() << '\n';
    std::cout << "HOOK_DESKTOP " << hook.DesktopName() << '\n';

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    POINT original{};
    GetCursorPos(&original);

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = 2000;
    input.mi.dy = 0;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dwExtraInfo = 0;

    const auto sent = SendInput(1, &input, sizeof(INPUT));
    std::cout << "SENDINPUT_RESULT " << sent << '\n';

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "SEEN_MOUSE_MOVE " << (hook.HasSeenMouseMove() ? "true" : "false") << '\n';
    std::cout << "IGNORED_INJECTED " << (hook.HasIgnoredInjectedMouseMove() ? "true" : "false") << '\n';

    SetCursorPos(original.x, original.y);
    hook.Stop();
    return 0;
}
