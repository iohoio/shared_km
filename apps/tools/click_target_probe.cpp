#include <windows.h>
#include <windowsx.h>

#include <fstream>
#include <string>

namespace {

constexpr wchar_t kWindowClassName[] = L"SharedKmClickTargetProbeWindow";
constexpr int kWindowLeft = 80;
constexpr int kWindowTop = 60;
constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr int kTargetCenterX = kWindowLeft + (kWindowWidth / 2);
constexpr int kTargetCenterY = kWindowTop + (kWindowHeight / 2);

std::ofstream g_log;

void LogLine(const std::string& line) {
    if (g_log.is_open()) {
        g_log << line << std::endl;
        g_log.flush();
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_CREATE:
        LogLine(
            "READY left=" + std::to_string(kWindowLeft) +
            " top=" + std::to_string(kWindowTop) +
            " width=" + std::to_string(kWindowWidth) +
            " height=" + std::to_string(kWindowHeight) +
            " center_x=" + std::to_string(kTargetCenterX) +
            " center_y=" + std::to_string(kTargetCenterY)
        );
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        DrawTextA(
            dc,
            "shared_km click target probe\nWaiting for WM_LBUTTONDOWN / WM_LBUTTONUP",
            -1,
            &rect,
            DT_CENTER | DT_VCENTER | DT_WORDBREAK
        );
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN:
        LogLine(
            "WM_LBUTTONDOWN x=" + std::to_string(static_cast<int>(GET_X_LPARAM(l_param))) +
            " y=" + std::to_string(static_cast<int>(GET_Y_LPARAM(l_param)))
        );
        return 0;
    case WM_LBUTTONUP:
        LogLine(
            "WM_LBUTTONUP x=" + std::to_string(static_cast<int>(GET_X_LPARAM(l_param))) +
            " y=" + std::to_string(static_cast<int>(GET_Y_LPARAM(l_param)))
        );
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        LogLine("EXIT");
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    g_log.open(".run\\click-target.log", std::ios::out | std::ios::trunc);
    LogLine("START");

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (RegisterClassW(&window_class) == 0) {
        LogLine("REGISTER_CLASS_FAILED");
        return 1;
    }

    HWND window = CreateWindowExW(
        WS_EX_TOPMOST,
        kWindowClassName,
        L"shared_km click target probe",
        WS_POPUP | WS_VISIBLE,
        kWindowLeft,
        kWindowTop,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (window == nullptr) {
        LogLine("CREATE_WINDOW_FAILED");
        return 1;
    }

    ShowWindow(window, show_command == 0 ? SW_SHOW : show_command);
    UpdateWindow(window);
    SetForegroundWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
