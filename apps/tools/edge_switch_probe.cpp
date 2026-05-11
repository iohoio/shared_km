#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <windows.h>

namespace {

void SendMouseRelative(int dx, int dy) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dwExtraInfo = 0;
    SendInput(1, &input, sizeof(INPUT));
}

void SendMouseAbsolute(int x, int y) {
    const auto screen_width = GetSystemMetrics(SM_CXSCREEN);
    const auto screen_height = GetSystemMetrics(SM_CYSCREEN);
    if (screen_width <= 0 || screen_height <= 0) {
        return;
    }

    const auto nx = static_cast<LONG>((static_cast<double>(x) * 65535.0) / (screen_width - 1));
    const auto ny = static_cast<LONG>((static_cast<double>(y) * 65535.0) / (screen_height - 1));

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = nx;
    input.mi.dy = ny;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    input.mi.dwExtraInfo = 0;
    SendInput(1, &input, sizeof(INPUT));
}

int ReadIntEnvOrDefault(const char* name, int default_value) {
    char* value_raw = nullptr;
    std::size_t value_len = 0;
    _dupenv_s(&value_raw, &value_len, name);
    if (value_raw == nullptr) {
        return default_value;
    }
    const int value = std::atoi(value_raw);
    free(value_raw);
    return value;
}

}  // namespace

int main() {
    const int screen_width = GetSystemMetrics(SM_CXSCREEN);
    const int screen_height = GetSystemMetrics(SM_CYSCREEN);
    const int center_x = screen_width / 2;
    const int center_y = screen_height / 2;
    const int edge_x = screen_width - 2;
    const int step_count = ReadIntEnvOrDefault("EDGE_PROBE_STEPS", 40);
    const int step_delay_ms = ReadIntEnvOrDefault("EDGE_PROBE_STEP_DELAY_MS", 8);
    const int hold_ms = ReadIntEnvOrDefault("EDGE_PROBE_HOLD_MS", 2000);

    std::cout << "screen=" << screen_width << "x" << screen_height << '\n';
    std::cout << "center=(" << center_x << "," << center_y << ")\n";
    std::cout << "edge_x=" << edge_x << '\n';
    std::cout << "steps=" << step_count << " delay=" << step_delay_ms << "ms hold=" << hold_ms << "ms\n";

    // Step 1: Move to center
    std::cout << "STEP1 moving to center...\n";
    SendMouseAbsolute(center_x, center_y);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Step 2: Move from center to right edge in small relative steps
    std::cout << "STEP2 moving to right edge...\n";
    const int total_dx = edge_x - center_x;
    const int step_dx = total_dx / step_count;
    for (int i = 0; i < step_count; ++i) {
        SendMouseRelative(step_dx, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(step_delay_ms));
    }

    // Step 3: Hold at edge
    std::cout << "STEP3 holding at edge for " << hold_ms << "ms...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));

    // Step 4: Move back left (exit edge)
    std::cout << "STEP4 moving left to exit edge...\n";
    for (int i = 0; i < 30; ++i) {
        SendMouseRelative(-8, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(step_delay_ms));
    }

    std::cout << "DONE\n";
    return 0;
}
