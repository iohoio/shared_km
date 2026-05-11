#include "shared_km/service/receiver_app.hpp"

#include "shared_km/core/config.hpp"
#include "shared_km/core/logger.hpp"
#include "shared_km/input/input_injector.hpp"
#include "shared_km/network/discovery.hpp"
#include "shared_km/network/socket_runtime.hpp"
#include "shared_km/network/tcp_server.hpp"
#include "shared_km/protocol/message.hpp"
#include "shared_km/service/input_event_dispatcher.hpp"

#include <algorithm>
#include <cstdlib>
#include <thread>

#include <atomic>

#include <windows.h>

namespace shared_km::service {

namespace {

bool DryRunInputEnabled() {
    char* value = nullptr;
    std::size_t length = 0;
    _dupenv_s(&value, &length, "SHAREDKM_DRY_RUN_INPUT");
    const bool enabled = value != nullptr && value[0] != '\0' && value[0] != '0';
    free(value);
    return enabled;
}

class ReceiverInputSink final : public shared_km::service::InputEventSink {
public:
    explicit ReceiverInputSink(shared_km::input::InputInjector& injector) : injector_(injector) {}

    bool MoveMouseAbsolute(int x, int y) override {
        return injector_.MoveMouseAbsolute(x, y);
    }

    bool MoveMouseRelative(int dx, int dy) override {
        return injector_.MoveMouseRelative(dx, dy);
    }

    bool EdgeEnter(shared_km::protocol::EdgeSide side, int y, int sender_height) override {
        const auto screen_width = GetSystemMetrics(SM_CXSCREEN);
        const auto screen_height = GetSystemMetrics(SM_CYSCREEN);
        if (screen_width <= 0 || screen_height <= 0) {
            return false;
        }

        int target_x = 0;
        int target_y = 0;
        switch (side) {
        case shared_km::protocol::EdgeSide::Left:
            target_x = screen_width - 1;
            target_y = sender_height > 0
                ? (std::min)(y * static_cast<int>(screen_height) / sender_height, static_cast<int>(screen_height) - 1)
                : (std::min)(y, static_cast<int>(screen_height) - 1);
            break;
        case shared_km::protocol::EdgeSide::Right:
            target_x = 0;
            target_y = sender_height > 0
                ? (std::min)(y * static_cast<int>(screen_height) / sender_height, static_cast<int>(screen_height) - 1)
                : (std::min)(y, static_cast<int>(screen_height) - 1);
            break;
        case shared_km::protocol::EdgeSide::Top:
            target_x = sender_height > 0
                ? (std::min)(y * static_cast<int>(screen_width) / sender_height, static_cast<int>(screen_width) - 1)
                : (std::min)(y, static_cast<int>(screen_width) - 1);
            target_y = screen_height - 1;
            break;
        case shared_km::protocol::EdgeSide::Bottom:
            target_x = sender_height > 0
                ? (std::min)(y * static_cast<int>(screen_width) / sender_height, static_cast<int>(screen_width) - 1)
                : (std::min)(y, static_cast<int>(screen_width) - 1);
            target_y = 0;
            break;
        }
        return injector_.MoveMouseAbsolute(target_x, target_y);
    }

    bool EdgeLeave() override {
        return true;
    }

    bool LeftButtonDown() override {
        return injector_.LeftButtonDown();
    }

    bool LeftButtonUp() override {
        return injector_.LeftButtonUp();
    }

    bool RightButtonDown() override {
        return injector_.RightButtonDown();
    }

    bool RightButtonUp() override {
        return injector_.RightButtonUp();
    }

    bool MouseWheel(short delta) override {
        return injector_.MouseWheel(delta);
    }

    bool KeyDown(unsigned int vk, unsigned int scan, unsigned int flags) override {
        return injector_.KeyDown(vk, scan, flags);
    }

    bool KeyUp(unsigned int vk, unsigned int scan, unsigned int flags) override {
        return injector_.KeyUp(vk, scan, flags);
    }

    unsigned long LastError() const override {
        return injector_.LastError();
    }

private:
    shared_km::input::InputInjector& injector_;
};

class DryRunInputSink final : public shared_km::service::InputEventSink {
public:
    bool MoveMouseAbsolute(int, int) override { return true; }
    bool MoveMouseRelative(int, int) override { return true; }
    bool EdgeEnter(shared_km::protocol::EdgeSide, int, int) override { return true; }
    bool EdgeLeave() override { return true; }
    bool LeftButtonDown() override { return true; }
    bool LeftButtonUp() override { return true; }
    bool RightButtonDown() override { return true; }
    bool RightButtonUp() override { return true; }
    bool MouseWheel(short) override { return true; }
    bool KeyDown(unsigned int, unsigned int, unsigned int) override { return true; }
    bool KeyUp(unsigned int, unsigned int, unsigned int) override { return true; }
    unsigned long LastError() const override { return 0; }
};

}  // namespace

std::atomic<bool> g_receiver_exit_requested = false;
shared_km::network::TcpServer* g_receiver_server = nullptr;

BOOL WINAPI ReceiverCtrlHandler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
        g_receiver_exit_requested = true;
        if (g_receiver_server != nullptr) {
            g_receiver_server->Close();
        }
        return TRUE;
    }
    return FALSE;
}

int ReceiverApp::Run() {
    const auto config = shared_km::core::LoadConfigOrDefaults("config\\receiver.json");
    auto& logger = shared_km::core::Logger::Instance();
    shared_km::input::InputInjector injector;
    ReceiverInputSink input_sink(injector);
    DryRunInputSink dry_run_input_sink;
    shared_km::network::SocketRuntime sockets;
    shared_km::network::TcpServer server;
    shared_km::network::DiscoveryService discovery;
    const bool dry_run_input = DryRunInputEnabled();

    logger.Info("shared_km_receiver starting");
    logger.Info("listening on " + config.host + ":" + std::to_string(config.port));
    if (dry_run_input) {
        logger.Info("dry-run input enabled; receiver will log input events without injecting them");
    }

    if (!SetConsoleCtrlHandler(ReceiverCtrlHandler, TRUE)) {
        logger.Warn("failed to install Ctrl+C handler");
    } else {
        logger.Info("Ctrl+C handler installed; press Ctrl+C to quit");
    }

    if (!server.Listen(config.host, config.port)) {
        logger.Error("failed to listen");
        return 1;
    }

    if (discovery.StartResponder(8767, "shared-km-receiver", config.port)) {
        logger.Info("discovery responder started on UDP port 8767");
    } else {
        logger.Warn("discovery responder failed: " + discovery.LastError());
    }

    g_receiver_server = &server;
    shared_km::protocol::ParsedHeader header{};
    std::vector<std::byte> payload;

    for (;;) {
        if (g_receiver_exit_requested.load()) {
            logger.Info("exit requested, stopping receiver");
            break;
        }

        auto connection = server.Accept();
        if (!connection.IsOpen()) {
            logger.Warn("failed to accept client, retrying in 1s...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        logger.Info("client connected");

        if (!connection.Receive(header, payload) || header.kind != shared_km::protocol::MessageKind::Hello) {
            logger.Warn("failed to receive hello from client, re-accepting...");
            continue;
        }

        const auto hello = shared_km::protocol::TryParseHelloPayload(payload);
        if (!hello) {
            logger.Warn("invalid hello payload, re-accepting...");
            continue;
        }
        logger.Info("hello from " + hello->device_name);

        if (!connection.Receive(header, payload) || header.kind != shared_km::protocol::MessageKind::Auth) {
            logger.Warn("failed to receive auth, re-accepting...");
            continue;
        }

        const auto auth = shared_km::protocol::TryParseAuthPayload(payload);
        const bool accepted = auth && auth->token == config.token;

        const auto auth_result = shared_km::protocol::SerializeAuthResultPayload({
            .accepted = accepted,
            .message = accepted ? "accepted" : "invalid token"
        });
        if (!connection.Send(shared_km::protocol::MessageKind::AuthResult, auth_result)) {
            logger.Warn("failed to send auth result, re-accepting...");
            continue;
        }

        if (!accepted) {
            logger.Warn("authentication rejected from client, re-accepting...");
            continue;
        }

        logger.Info("authentication accepted");

        while (connection.Receive(header, payload)) {
            if (header.kind == shared_km::protocol::MessageKind::Heartbeat) {
                const auto heartbeat = shared_km::protocol::TryParseHeartbeatPayload(payload);
                if (!heartbeat) {
                    logger.Warn("invalid heartbeat payload, re-accepting...");
                    break;
                }

                logger.Info("heartbeat received");
                continue;
            }

            if (header.kind == shared_km::protocol::MessageKind::ClipboardData) {
                const auto data = shared_km::protocol::TryParseClipboardDataPayload(payload);
                if (!data) {
                    logger.Warn("invalid clipboard data payload, re-accepting...");
                    break;
                }

                // Set clipboard text
                if (OpenClipboard(nullptr)) {
                    EmptyClipboard();
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, data->text.c_str(), -1, nullptr, 0);
                    if (wlen > 0) {
                        HGLOBAL hglob = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wlen) * sizeof(wchar_t));
                        if (hglob) {
                            auto* wtext = static_cast<wchar_t*>(GlobalLock(hglob));
                            MultiByteToWideChar(CP_UTF8, 0, data->text.c_str(), -1, wtext, wlen);
                            GlobalUnlock(hglob);
                            SetClipboardData(CF_UNICODETEXT, hglob);
                        }
                    }
                    CloseClipboard();
                }

                logger.Info("clipboard data received: " + std::to_string(data->text.size()) + " bytes");
                continue;
            }

            if (header.kind != shared_km::protocol::MessageKind::InputEvent) {
                logger.Warn("ignoring unexpected message kind: " + shared_km::protocol::ToString(header.kind));
                continue;
            }

            const auto input_event = shared_km::protocol::TryParseInputEventPayload(payload);
            if (!input_event) {
                logger.Warn("invalid input event payload, re-accepting...");
                break;
            }

            auto& sink = dry_run_input
                ? static_cast<shared_km::service::InputEventSink&>(dry_run_input_sink)
                : static_cast<shared_km::service::InputEventSink&>(input_sink);
            const auto dispatch_result = DispatchInputEvent(*input_event, sink);
            if (!dispatch_result.ok) {
                logger.Warn(dispatch_result.message + ", continuing...");
                continue;
            }

            logger.Info(dispatch_result.message);
        }

        logger.Warn("client disconnected, waiting for next client...");
    }

    return 0;
}

}  // namespace shared_km::service
