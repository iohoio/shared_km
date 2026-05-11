#include "shared_km/service/sender_app.hpp"

#include "shared_km/core/config.hpp"
#include "shared_km/core/logger.hpp"
#include "shared_km/input/clipboard_monitor.hpp"
#include "shared_km/input/input_injector.hpp"
#include "shared_km/input/keyboard_hook.hpp"
#include "shared_km/input/mouse_hook.hpp"
#include "shared_km/input/raw_input_reader.hpp"
#include "shared_km/network/socket_runtime.hpp"
#include "shared_km/network/tcp_client.hpp"
#include "shared_km/protocol/message.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <atomic>
#include <array>
#include <string>
#include <thread>

#include <windows.h>

namespace shared_km::service {

namespace {

std::string DesktopNameFromHandle(HDESK desktop) {
    if (desktop == nullptr) {
        return "<null>";
    }

    std::array<wchar_t, 256> name{};
    DWORD needed = 0;
    if (!GetUserObjectInformationW(
            desktop,
            UOI_NAME,
            name.data(),
            static_cast<DWORD>(name.size() * sizeof(wchar_t)),
            &needed
        )) {
        return "<unknown>";
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, name.data(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return "<unknown>";
    }

    std::string utf8(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name.data(), -1, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string CurrentInputDesktopName() {
    HDESK desktop = OpenInputDesktop(0, FALSE, GENERIC_READ);
    if (desktop == nullptr) {
        return "<open-failed>";
    }

    const auto name = DesktopNameFromHandle(desktop);
    CloseDesktop(desktop);
    return name;
}

std::string CurrentThreadDesktopName() {
    return DesktopNameFromHandle(GetThreadDesktop(GetCurrentThreadId()));
}

unsigned long CurrentSessionId() {
    DWORD session_id = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) {
        return static_cast<unsigned long>(-1);
    }
    return session_id;
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

bool IsEnvEnabled(const char* name) {
    char* value_raw = nullptr;
    std::size_t value_len = 0;
    _dupenv_s(&value_raw, &value_len, name);
    const bool enabled = value_raw != nullptr && value_raw[0] != '\0' && value_raw[0] != '0';
    free(value_raw);
    return enabled;
}

std::atomic<bool> g_sender_exit_requested = false;

BOOL WINAPI SenderCtrlHandler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
        g_sender_exit_requested = true;
        return TRUE;
    }
    return FALSE;
}

}  // namespace

int SenderApp::Run() {
    const auto config = shared_km::core::LoadConfigOrDefaults("config\\sender.json");
    auto& logger = shared_km::core::Logger::Instance();
    shared_km::input::MouseHook mouse_hook;
    shared_km::input::KeyboardHook keyboard_hook;
    shared_km::input::ClipboardMonitor clipboard_monitor;
    shared_km::input::RawInputReader raw_reader;
    shared_km::network::SocketRuntime sockets;
    shared_km::network::TcpClient client;

    logger.Info("shared_km_sender starting");

    if (!SetConsoleCtrlHandler(SenderCtrlHandler, TRUE)) {
        logger.Warn("failed to install Ctrl+C handler");
    } else {
        logger.Info("Ctrl+C handler installed; press Ctrl+C to quit");
    }
    logger.Info("process session id=" + std::to_string(CurrentSessionId()));
    logger.Info("current thread desktop=" + CurrentThreadDesktopName());
    logger.Info("current input desktop=" + CurrentInputDesktopName());
    shared_km::protocol::ParsedHeader header{};
    std::vector<std::byte> payload;

    logger.Info("connecting to " + config.host + ":" + std::to_string(config.port));

    {
        int retry_delay = 1000;
        for (;;) {
            if (client.Connect(config.host, config.port)) {
                const auto hello = shared_km::protocol::SerializeHelloPayload({.device_name = "shared-km-sender"});
                const auto auth = shared_km::protocol::SerializeAuthPayload({.token = config.token});
                if (client.Send(shared_km::protocol::MessageKind::Hello, hello) &&
                    client.Send(shared_km::protocol::MessageKind::Auth, auth) &&
                    client.Receive(header, payload) &&
                    header.kind == shared_km::protocol::MessageKind::AuthResult) {
                    const auto auth_result = shared_km::protocol::TryParseAuthResultPayload(payload);
                    if (auth_result && auth_result->accepted) {
                        break;
                    }
                }
                client.Close();
            }

            logger.Warn("connection failed, retrying in " + std::to_string(retry_delay) + "ms...");
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay));
            retry_delay = std::min(retry_delay * 2, 30000);
        }
    }

    logger.Info("connected and authenticated");

    const auto heartbeat = shared_km::protocol::SerializeHeartbeatPayload({
        .timestamp_ms = 1
    });
    if (!client.Send(shared_km::protocol::MessageKind::Heartbeat, heartbeat)) {
        logger.Error("failed to send heartbeat");
        return 1;
    }

    logger.Info("heartbeat sent");
    const bool test_click_with_move = IsEnvEnabled("SHAREDKM_TEST_CLICK_WITH_MOVE");
    if (test_click_with_move) {
        const int target_x = ReadIntEnvOrDefault("SHAREDKM_TEST_CLICK_X", 240);
        const int target_y = ReadIntEnvOrDefault("SHAREDKM_TEST_CLICK_Y", 180);
        const auto move_event = shared_km::protocol::SerializeInputEventPayload({
            .type = shared_km::protocol::InputEventType::MouseMove,
            .mouse_move = {
                .x = target_x,
                .y = target_y,
            }
        });
        if (!client.Send(shared_km::protocol::MessageKind::InputEvent, move_event)) {
            logger.Error("failed to send pre-click mouse move event");
            return 1;
        }
        logger.Info(
            "pre-click mouse move event sent: (" +
            std::to_string(target_x) +
            ", " +
            std::to_string(target_y) +
            ")"
        );

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    const auto left_down = shared_km::protocol::SerializeInputEventPayload({
        .type = shared_km::protocol::InputEventType::LeftButtonDown
    });
    if (!client.Send(shared_km::protocol::MessageKind::InputEvent, left_down)) {
        logger.Error("failed to send left button down event");
        return 1;
    }
    logger.Info("left button down event sent");

    const auto left_up = shared_km::protocol::SerializeInputEventPayload({
        .type = shared_km::protocol::InputEventType::LeftButtonUp
    });
    if (!client.Send(shared_km::protocol::MessageKind::InputEvent, left_up)) {
        logger.Error("failed to send left button up event");
        return 1;
    }
    logger.Info("left button up event sent");

    if (test_click_with_move) {
        logger.Info("test click mode complete; skipping mouse hook startup");
        return 0;
    }

    const bool edge_switching = IsEnvEnabled("SHAREDKM_EDGE_SWITCHING");
    if (edge_switching) {
        logger.Info("edge switching enabled; right edge triggers transition");
    }

    logger.Info("tracking mouse movement; press Esc to quit sender");

    char* runtime_ms_raw = nullptr;
    std::size_t runtime_ms_len = 0;
    _dupenv_s(&runtime_ms_raw, &runtime_ms_len, "SHAREDKM_TEST_DURATION_MS");
    const int runtime_ms = runtime_ms_raw != nullptr ? std::atoi(runtime_ms_raw) : 0;
    free(runtime_ms_raw);
    const auto started_at = std::chrono::steady_clock::now();
    std::atomic<bool> send_failed = false;
    std::atomic<bool> edge_switched = false;
    // Physical screen dimensions (WH_MOUSE_LL reports physical pixels, not DPI-scaled logical pixels)
    HDC hdc = GetDC(nullptr);
    const int physical_width = GetDeviceCaps(hdc, DESKTOPHORZRES);
    const int physical_height = GetDeviceCaps(hdc, DESKTOPVERTRES);
    ReleaseDC(nullptr, hdc);
    const int sticky_threshold = ReadIntEnvOrDefault("SHAREDKM_STICKY_THRESHOLD", 20);
    constexpr int kPollIntervalMs = 8;

    // State machine
    bool in_sticky_zone = false;
    int sticky_accumulated = 0;
    int virtual_dx = 0;  // net displacement since crossing; auto-disengage when < -threshold
    RECT clip_rect = {0, 0, 0, 0};
    // Coalesced mouse move (copy mode): hook stores latest, main loop sends at fixed rate
    std::atomic<int> coalesced_mouse_x{-1};
    std::atomic<int> coalesced_mouse_y{-1};
    std::atomic<bool> coalesced_mouse_pending{false};

    // Helper: send, returns false on failure
    auto SendEvent = [&](const std::vector<std::byte>& payload) -> bool {
        if (!client.Send(shared_km::protocol::MessageKind::InputEvent, payload)) {
            send_failed = true;
            return false;
        }
        return true;
    };

    auto Disengage = [&]() {
        edge_switched = false;
        in_sticky_zone = false;
        sticky_accumulated = 0;
        virtual_dx = 0;
        ClipCursor(nullptr);
        SetCursorPos(physical_width - 10, physical_height / 2);
        const auto leave = shared_km::protocol::SerializeInputEventPayload({
            .type = shared_km::protocol::InputEventType::EdgeLeave
        });
        SendEvent(leave);
        logger.Info("cross-screen disengaged");
    };

    if (edge_switching) {
        raw_reader.Start();
    }

    if (!mouse_hook.Start([&](const shared_km::input::MouseEvent& event) -> bool {
            if (send_failed.load()) {
                return false;
            }

            const bool is_remote = edge_switching && edge_switched.load();

            // -- Button clicks --
            auto ForwardClick = [&](shared_km::protocol::InputEventType et) {
                if (!edge_switching) {
                    // Non-edge mode: always forward clicks
                    const auto ev = shared_km::protocol::SerializeInputEventPayload({.type = et});
                    if (!SendEvent(ev)) return;
                    logger.Info(std::string(et == shared_km::protocol::InputEventType::LeftButtonDown ||
                                            et == shared_km::protocol::InputEventType::LeftButtonUp ? "left" : "right") +
                                " button " +
                                std::string(et == shared_km::protocol::InputEventType::LeftButtonDown ||
                                            et == shared_km::protocol::InputEventType::RightButtonDown ? "down" : "up") +
                                " sent");
                    return;
                }
                // Edge-switching mode: only forward when remote
                if (!edge_switched.load()) return;
                const auto ev = shared_km::protocol::SerializeInputEventPayload({.type = et});
                if (!SendEvent(ev)) return;
                logger.Info(std::string(et == shared_km::protocol::InputEventType::LeftButtonDown ||
                                        et == shared_km::protocol::InputEventType::LeftButtonUp ? "left" : "right") +
                            " button " +
                            std::string(et == shared_km::protocol::InputEventType::LeftButtonDown ||
                                        et == shared_km::protocol::InputEventType::RightButtonDown ? "down" : "up") +
                            " sent");
            };

            if (event.type == shared_km::input::MouseEventType::LeftDown) {
                ForwardClick(shared_km::protocol::InputEventType::LeftButtonDown);
                return is_remote;
            }
            if (event.type == shared_km::input::MouseEventType::LeftUp) {
                ForwardClick(shared_km::protocol::InputEventType::LeftButtonUp);
                return is_remote;
            }
            if (event.type == shared_km::input::MouseEventType::RightDown) {
                ForwardClick(shared_km::protocol::InputEventType::RightButtonDown);
                return is_remote;
            }
            if (event.type == shared_km::input::MouseEventType::RightUp) {
                ForwardClick(shared_km::protocol::InputEventType::RightButtonUp);
                return is_remote;
            }

            // -- Mouse wheel --
            if (event.type == shared_km::input::MouseEventType::Wheel) {
                if (edge_switching && !edge_switched.load()) return false;
                const auto ev = shared_km::protocol::SerializeInputEventPayload({
                    .type = shared_km::protocol::InputEventType::MouseWheel,
                    .wheel_delta = static_cast<std::int16_t>(event.wheel_delta),
                });
                if (!SendEvent(ev)) {
                    logger.Warn("failed to send mouse wheel event");
                    return is_remote;
                }
                logger.Info("mouse wheel sent: delta=" + std::to_string(event.wheel_delta));
                return is_remote;
            }

            // -- Mouse move only beyond this point --
            if (event.type != shared_km::input::MouseEventType::Move) return false;

            if (edge_switching) {
                if (edge_switched.load()) {
                    // Remote state: cursor is clipped to 1×1.
                    // Movement comes from raw input (polled in main loop).
                    return true;
                }

                // Local state: detect edge crossing with sticky threshold
                if (event.x >= physical_width - sticky_threshold) {
                    if (!in_sticky_zone) {
                        in_sticky_zone = true;
                        sticky_accumulated = 0;
                    }

                    int raw_dx = 0, raw_dy = 0;
                    raw_reader.ReadDelta(raw_dx, raw_dy);
                    if (raw_dx > 0) {
                        sticky_accumulated += raw_dx;
                    }

                    if (sticky_accumulated >= sticky_threshold) {
                        // Cross!
                        in_sticky_zone = false;
                        sticky_accumulated = 0;
                        virtual_dx = 0;
                        edge_switched = true;

                        ClipCursor(&clip_rect);
                        SetCursorPos(0, 0);

                        const auto enter = shared_km::protocol::SerializeInputEventPayload({
                            .type = shared_km::protocol::InputEventType::EdgeEnter,
                            .edge_side = shared_km::protocol::EdgeSide::Right,
                            .dx = physical_height,
                            .dy = event.y,
                        });
                        SendEvent(enter);
                        logger.Info(">> cross-screen <<");
                    }
                    return false;
                }

                if (in_sticky_zone) {
                    in_sticky_zone = false;
                    sticky_accumulated = 0;
                }
                return false; // not edge-switched, don't forward
            }

            // No edge switching: coalesce mouse moves — store latest, main loop sends at fixed rate
            coalesced_mouse_x.store(event.x);
            coalesced_mouse_y.store(event.y);
            coalesced_mouse_pending.store(true);
            return false;
        })) {
        logger.Error("failed to start mouse hook, win32 error=" + std::to_string(mouse_hook.LastError()));
        return 1;
    }
    logger.Info("mouse hook started successfully");
    logger.Info("mouse hook thread id=" + std::to_string(mouse_hook.ThreadId()));
    logger.Info("mouse hook thread desktop=" + mouse_hook.DesktopName());

    if (!keyboard_hook.Start([&](const shared_km::input::KeyEventInfo& info) {
            if (send_failed.load()) {
                return false;
            }
            // In edge-switching mode, Escape disengages cross-screen locally
            if (edge_switching && edge_switched.load() &&
                info.virtual_key_code == VK_ESCAPE && info.message == WM_KEYDOWN) {
                Disengage();
                return true;  // block Escape locally
            }
            // In edge-switching mode, don't forward keys until edge-switched
            if (edge_switching && !edge_switched.load()) return false;

            const bool is_down = info.message == WM_KEYDOWN || info.message == WM_SYSKEYDOWN;
            const auto event_type = is_down
                ? shared_km::protocol::InputEventType::KeyDown
                : shared_km::protocol::InputEventType::KeyUp;

            const auto event = shared_km::protocol::SerializeInputEventPayload({
                .type = event_type,
                .key_event = shared_km::protocol::KeyEventPayload{
                    .virtual_key_code = static_cast<std::uint16_t>(info.virtual_key_code),
                    .scan_code = static_cast<std::uint16_t>(info.scan_code),
                    .flags = static_cast<std::uint16_t>(info.flags),
                }
            });
            if (!SendEvent(event)) return true;

            logger.Info(
                std::string(is_down ? "key down" : "key up") +
                " event sent: vk=" + std::to_string(info.virtual_key_code) +
                " scan=" + std::to_string(info.scan_code)
            );
            // Block key locally when in remote mode
            return edge_switching && edge_switched.load();
        })) {
        logger.Error("failed to start keyboard hook, win32 error=" + std::to_string(keyboard_hook.LastError()));
        return 1;
    }
    logger.Info("keyboard hook started successfully");
    logger.Info("keyboard hook thread id=" + std::to_string(keyboard_hook.ThreadId()));
    logger.Info("keyboard hook thread desktop=" + keyboard_hook.DesktopName());

    {
        static std::string s_last_sent_clipboard;
        if (clipboard_monitor.Start([&](const std::string& text) {
                if (send_failed.load() || text.empty() || text == s_last_sent_clipboard) {
                    return;
                }
                const auto data = shared_km::protocol::SerializeClipboardDataPayload({.text = text});
                if (client.Send(shared_km::protocol::MessageKind::ClipboardData, data)) {
                    s_last_sent_clipboard = text;
                    logger.Info("clipboard data sent: " + std::to_string(text.size()) + " bytes");
                } else {
                    send_failed = true;
                }
            })) {
            logger.Info("clipboard monitor started successfully");
        } else {
            logger.Warn("clipboard monitor failed to start: " + clipboard_monitor.LastError());
        }
    }

    // Main loop: heartbeat + poll raw deltas in remote mode
    auto last_heartbeat = std::chrono::steady_clock::now();
    while (true) {
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0 || g_sender_exit_requested.load()) {
            if (g_sender_exit_requested.load()) {
                logger.Info("Ctrl+C received, stopping sender");
            } else {
                logger.Info("escape pressed, stopping sender");
            }
            break;
        }
        if (send_failed.load()) {
            logger.Warn("connection lost, attempting reconnect...");
            edge_switched = false;
            ClipCursor(nullptr);

            int retry_delay = 1000;
            while (true) {
                if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0 || g_sender_exit_requested.load()) {
                    logger.Info("interrupted during reconnect, stopping sender");
                    send_failed = false;
                    goto done;
                }

                client.Close();
                if (client.Connect(config.host, config.port)) {
                    const auto hello = shared_km::protocol::SerializeHelloPayload({.device_name = "shared-km-sender"});
                    const auto auth = shared_km::protocol::SerializeAuthPayload({.token = config.token});
                    shared_km::protocol::ParsedHeader hdr;
                    std::vector<std::byte> pl;
                    if (client.Send(shared_km::protocol::MessageKind::Hello, hello) &&
                        client.Send(shared_km::protocol::MessageKind::Auth, auth) &&
                        client.Receive(hdr, pl) &&
                        hdr.kind == shared_km::protocol::MessageKind::AuthResult) {
                        const auto auth_result = shared_km::protocol::TryParseAuthResultPayload(pl);
                        if (auth_result && auth_result->accepted) {
                            logger.Info("reconnect successful");
                            send_failed = false;
                            break;
                        }
                    }
                    client.Close();
                }

                logger.Warn("reconnect attempt failed, retrying in " + std::to_string(retry_delay) + "ms...");
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay));
                retry_delay = std::min(retry_delay * 2, 30000);
            }
            continue;
        }
        if (runtime_ms > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at
            );
            if (elapsed.count() >= runtime_ms) {
                logger.Info("test duration elapsed, stopping sender");
                break;
            }
        }

        auto now = std::chrono::steady_clock::now();

        // Heartbeat every 5 s
        if (now - last_heartbeat >= std::chrono::seconds(5)) {
            const auto hb = shared_km::protocol::SerializeHeartbeatPayload({.timestamp_ms = 1});
            if (!client.Send(shared_km::protocol::MessageKind::Heartbeat, hb)) {
                send_failed = true;
                continue;
            }
            last_heartbeat = now;
        }

        // Remote mode: poll raw deltas and forward as relative movement
        if (edge_switched.load()) {
            int dx = 0, dy = 0;
            raw_reader.ReadDelta(dx, dy);
            if (dx != 0 || dy != 0) {
                virtual_dx += dx;
                const auto rel = shared_km::protocol::SerializeInputEventPayload({
                    .type = shared_km::protocol::InputEventType::MouseMoveRelative,
                    .dx = dx,
                    .dy = dy,
                });
                if (!SendEvent(rel)) continue;
                logger.Info("mouse move relative sent: (" + std::to_string(dx) + ", " + std::to_string(dy) + ")");

                // Auto-disengage: user moved left significantly past the entry point
                if (virtual_dx < -sticky_threshold) {
                    Disengage();
                }
            }
        }

        // Copy mode: send coalesced mouse move
        if (!edge_switching && coalesced_mouse_pending.exchange(false)) {
            int cx = coalesced_mouse_x.load();
            int cy = coalesced_mouse_y.load();
            const auto cm = shared_km::protocol::SerializeInputEventPayload({
                .type = shared_km::protocol::InputEventType::MouseMove,
                .mouse_move = {cx, cy},
            });
            if (!SendEvent(cm)) continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }

done:
    clipboard_monitor.Stop();
    keyboard_hook.Stop();
    mouse_hook.Stop();
    raw_reader.Stop();
    ClipCursor(nullptr);
    logger.Info(
        std::string("mouse hook stopped; seen_mouse_move=") +
        (mouse_hook.HasSeenMouseMove() ? "true" : "false")
    );
    logger.Info(
        std::string("mouse hook ignored injected events=") +
        (mouse_hook.HasIgnoredInjectedMouseMove() ? "true" : "false")
    );
    return send_failed.load() ? 1 : 0;
}

}  // namespace shared_km::service
