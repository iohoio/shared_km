#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <string>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "shared_km/network/discovery.hpp"
#include "shared_km/network/file_transfer.hpp"
#include "shared_km/network/socket_runtime.hpp"
#include "shared_km/network/tcp_server.hpp"
#include "shared_km/network/tcp_client.hpp"
#include "shared_km/input/input_injector.hpp"
#include "shared_km/input/keyboard_hook.hpp"
#include "shared_km/input/mouse_hook.hpp"
#include "shared_km/input/raw_input_reader.hpp"
#include "shared_km/input/clipboard_monitor.hpp"
#include "shared_km/protocol/message.hpp"
#include "shared_km/service/input_event_dispatcher.hpp"
#include "shared_km/service/receiver_app.hpp"
#include "shared_km/service/sender_app.hpp"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

namespace {

// -- Window identifiers --
constexpr UINT_PTR kIdTrayIcon = 1;
constexpr UINT_PTR kIdTimerStatus = 2;
constexpr UINT kMsgFileTransferDone = WM_APP + 2;

enum CtrlId {
    kEditHost = 100,
    kEditPort,
    kEditToken,
    kRadioCopy,
    kRadioExtend,
    kBtnSaveConfig,
    kBtnLaunchReceiver,
    kBtnLaunchSender,
    kBtnStopAll,
    kBtnDetectIP,
    kBtnDiscover,
    kStaticStatus,
    kListDevices,
    kListFileTransfers,
};

// -- Global state --
HINSTANCE g_instance = nullptr;
HWND g_main_wnd = nullptr;
HWND g_edit_host = nullptr;
HWND g_edit_port = nullptr;
HWND g_edit_token = nullptr;
HWND g_radio_copy = nullptr;
HWND g_radio_extend = nullptr;
HWND g_static_status = nullptr;
HWND g_list_devices = nullptr;
HWND g_receiver_status = nullptr;
HWND g_sender_status = nullptr;
NOTIFYICONDATAW g_notify{};
UINT g_taskbar_created = 0;

std::wstring g_config_host = L"127.0.0.1";
std::wstring g_config_port = L"8765";
std::wstring g_config_token = L"demo-token";
// false = copy mode, true = extend mode
bool g_config_extend_mode = false;

// -- File transfer history --
struct FileTransferEntry {
    std::wstring filename;
    std::wstring size_str;
    bool success;
};
std::vector<FileTransferEntry> g_file_transfer_history;
std::mutex g_file_transfer_mutex;
HWND g_list_file_transfers = nullptr;
shared_km::network::TcpFileServer g_file_server;
std::thread g_file_server_thread;

// Forward declarations
void OnFileTransferComplete(const shared_km::network::FileTransferResult& result);
void RefreshFileTransferList();

// Current receiver address (updated when sender connects)
std::string g_current_receiver_host;
int g_current_receiver_port = 0;

// -- In-process service state --
std::atomic<bool> g_receiver_active{false};
std::atomic<bool> g_sender_active{false};
std::thread g_receiver_thread;
std::thread g_sender_thread;
shared_km::network::TcpServer g_receiver_server;

// -- Helpers --
std::string WStringToUTF8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring UTF8ToWString(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring w(static_cast<std::size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    auto pos = path.find_last_of(L"\\");
    if (pos != std::wstring::npos) path.resize(pos + 1);
    return path;
}

std::wstring FormatFileSize(std::uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB"};
    double size = static_cast<double>(bytes);
    int unit = 0;
    while (size >= 1024.0 && unit < 3) {
        size /= 1024.0;
        unit++;
    }
    wchar_t buf[32];
    if (unit == 0) {
        std::swprintf(buf, 32, L"%llu %s", bytes, units[unit]);
    } else {
        std::swprintf(buf, 32, L"%.1f %s", size, units[unit]);
    }
    return buf;
}

std::wstring SenderConfigPath() {
    return ExeDir() + L"config\\sender.json";
}

std::wstring ReceiverConfigPath() {
    return ExeDir() + L"config\\receiver.json";
}

void WriteJsonFile(const std::wstring& path,
                   const std::wstring& host,
                   const std::wstring& port,
                   const std::wstring& token,
                   bool extend_mode) {
    auto file = _wfopen(path.c_str(), L"w");
    if (!file) return;

    auto h = WStringToUTF8(host);
    auto p = WStringToUTF8(port);
    auto t = WStringToUTF8(token);

    fprintf(file, "{\n");
    fprintf(file, "    \"token\": \"%s\",\n", t.c_str());
    fprintf(file, "    \"host\": \"%s\",\n", h.c_str());
    fprintf(file, "    \"port\": %s,\n", p.c_str());
    fprintf(file, "    \"screen_mode\": \"%s\"\n", extend_mode ? "extend" : "copy");
    fprintf(file, "}\n");
    fclose(file);
}

// -- In-process service management --

void ReadControls() {
    wchar_t buf[256];
    GetWindowTextW(g_edit_host, buf, 256); g_config_host = buf;
    GetWindowTextW(g_edit_port, buf, 256); g_config_port = buf;
    GetWindowTextW(g_edit_token, buf, 256); g_config_token = buf;
    g_config_extend_mode = SendMessageW(g_radio_extend, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

struct ServiceConfig {
    std::string host;
    int port;
    std::string token;
    bool extend_mode;  // false=copy, true=extend
};

ServiceConfig GetServiceConfig() {
    ReadControls();
    return {
        .host = WStringToUTF8(g_config_host),
        .port = static_cast<int>(std::wcstol(g_config_port.c_str(), nullptr, 10)),
        .token = WStringToUTF8(g_config_token),
        .extend_mode = g_config_extend_mode,
    };
}

// --- Receiver ---

void ReceiverThreadFunc(ServiceConfig cfg, std::atomic<bool>& running) {
    shared_km::input::InputInjector injector;
    shared_km::network::SocketRuntime sockets;
    shared_km::network::DiscoveryService discovery;

    // Simple input sink wrapping InputInjector
    class Sink : public shared_km::service::InputEventSink {
        shared_km::input::InputInjector& inj;
    public:
        explicit Sink(shared_km::input::InputInjector& i) : inj(i) {}
        bool MoveMouseAbsolute(int x, int y) override { return inj.MoveMouseAbsolute(x, y); }
        bool MoveMouseRelative(int dx, int dy) override { return inj.MoveMouseRelative(dx, dy); }
        bool EdgeEnter(shared_km::protocol::EdgeSide side, int y, int sender_height) override {
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            if (sw <= 0 || sh <= 0) return false;
            int tx = 0, ty = 0;
            switch (side) {
            case shared_km::protocol::EdgeSide::Left:
                tx = sw - 1;
                ty = sender_height > 0 ? (std::min)(y * sh / sender_height, sh - 1) : (std::min)(y, sh - 1);
                break;
            case shared_km::protocol::EdgeSide::Right:
                tx = 0;
                ty = sender_height > 0 ? (std::min)(y * sh / sender_height, sh - 1) : (std::min)(y, sh - 1);
                break;
            case shared_km::protocol::EdgeSide::Top:
                tx = sender_height > 0 ? (std::min)(y * sw / sender_height, sw - 1) : (std::min)(y, sw - 1);
                ty = sh - 1;
                break;
            case shared_km::protocol::EdgeSide::Bottom:
                tx = sender_height > 0 ? (std::min)(y * sw / sender_height, sw - 1) : (std::min)(y, sw - 1);
                ty = 0;
                break;
            }
            return inj.MoveMouseAbsolute(tx, ty);
        }
        bool EdgeLeave() override { return true; }
        bool LeftButtonDown() override { return inj.LeftButtonDown(); }
        bool LeftButtonUp() override { return inj.LeftButtonUp(); }
        bool RightButtonDown() override { return inj.RightButtonDown(); }
        bool RightButtonUp() override { return inj.RightButtonUp(); }
        bool MouseWheel(short delta) override { return inj.MouseWheel(delta); }
        bool KeyDown(unsigned int vk, unsigned int scan, unsigned int flags) override {
            return inj.KeyDown(vk, scan, flags);
        }
        bool KeyUp(unsigned int vk, unsigned int scan, unsigned int flags) override {
            return inj.KeyUp(vk, scan, flags);
        }
        unsigned long LastError() const override { return inj.LastError(); }
    } input_sink(injector);

    if (!g_receiver_server.Listen(cfg.host, cfg.port)) {
        SetWindowTextW(g_receiver_status, L"listen failed");
        running = false;
        return;
    }

    {
        wchar_t buf[64];
        std::swprintf(buf, 64, L"listening on :%d", cfg.port);
        SetWindowTextW(g_receiver_status, buf);
    }
    discovery.StartResponder(8767, "shared-km-receiver", static_cast<std::uint16_t>(cfg.port));

    shared_km::protocol::ParsedHeader header;
    std::vector<std::byte> payload;

    while (running) {
        auto conn = g_receiver_server.Accept();
        if (!conn.IsOpen()) break;
        if (!running) break;

        SetWindowTextW(g_receiver_status, L"client connecting...");

        if (!conn.Receive(header, payload) || header.kind != shared_km::protocol::MessageKind::Hello)
            continue;

        const auto hello = shared_km::protocol::TryParseHelloPayload(payload);
        if (!hello) continue;

        if (!conn.Receive(header, payload) || header.kind != shared_km::protocol::MessageKind::Auth)
            continue;

        const auto auth = shared_km::protocol::TryParseAuthPayload(payload);
        const bool accepted = auth && auth->token == cfg.token;

        const auto auth_result = shared_km::protocol::SerializeAuthResultPayload({
            .accepted = accepted,
            .message = accepted ? "accepted" : "invalid token"
        });
        if (!conn.Send(shared_km::protocol::MessageKind::AuthResult, auth_result))
            continue;

        if (!accepted) continue;

        SetWindowTextW(g_receiver_status, L"client authenticated");

        while (running && conn.Receive(header, payload)) {
            if (header.kind == shared_km::protocol::MessageKind::Heartbeat) continue;
            if (header.kind == shared_km::protocol::MessageKind::ClipboardData) {
                const auto data = shared_km::protocol::TryParseClipboardDataPayload(payload);
                if (!data) break;
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
                continue;
            }
            if (header.kind != shared_km::protocol::MessageKind::InputEvent) continue;

            const auto input_event = shared_km::protocol::TryParseInputEventPayload(payload);
            if (!input_event) break;

            const auto result = shared_km::service::DispatchInputEvent(*input_event, input_sink);
            if (!result.ok) {
                wchar_t err[256];
                auto msg = UTF8ToWString(result.message);
                std::swprintf(err, 256, L"dispatch error: %s", msg.c_str());
                SetWindowTextW(g_receiver_status, err);
                continue;
            }
        }
        SetWindowTextW(g_receiver_status, L"client disconnected");
    }
}

void StartReceiver() {
    if (g_receiver_active) return;
    g_receiver_active = true;
    auto cfg = GetServiceConfig();
    cfg.host = "0.0.0.0";  // listen on all interfaces so remote senders can connect
    g_receiver_thread = std::thread(ReceiverThreadFunc, cfg, std::ref(g_receiver_active));
    SetThreadPriority(g_receiver_thread.native_handle(), THREAD_PRIORITY_HIGHEST);

    // Start file server for drag-and-drop file transfers (port+1)
    auto save_dir = WStringToUTF8(ExeDir()) + "received_files\\";
    auto file_port = static_cast<std::uint16_t>(cfg.port + 1);
    if (g_file_server.Start("0.0.0.0", file_port, save_dir, OnFileTransferComplete))
    {
        g_file_server_thread = std::thread([&]() { g_file_server.RunAcceptLoop(); });
        SetThreadPriority(g_file_server_thread.native_handle(), THREAD_PRIORITY_HIGHEST);
    }
}

void StopReceiver() {
    g_receiver_active = false;
    g_receiver_server.Close();
    if (g_receiver_thread.joinable()) g_receiver_thread.join();
    g_file_server.Stop();
    if (g_file_server_thread.joinable()) g_file_server_thread.join();
    SetWindowTextW(g_receiver_status, L"stopped");
}

// --- Sender ---

std::mutex g_sender_mutex;
shared_km::network::TcpClient g_sender_client;
shared_km::input::MouseHook g_mouse_hook;
shared_km::input::KeyboardHook g_keyboard_hook;
shared_km::input::ClipboardMonitor g_clipboard_monitor;
shared_km::input::RawInputReader g_raw_reader;
std::atomic<bool> g_sender_paused{false};
// Coalesced mouse move (copy mode): hook stores latest, main loop sends at fixed rate
std::atomic<int> g_coalesced_mouse_x{-1};
std::atomic<int> g_coalesced_mouse_y{-1};
std::atomic<bool> g_coalesced_mouse_pending{false};

void SenderConnectThreadFunc(ServiceConfig cfg, std::atomic<bool>& running) {
    shared_km::network::SocketRuntime sockets;

    // Physical screen dimensions (WH_MOUSE_LL reports physical pixels)
    HDC hdc = GetDC(nullptr);
    const int physical_width = GetDeviceCaps(hdc, DESKTOPHORZRES);
    const int physical_height = GetDeviceCaps(hdc, DESKTOPVERTRES);
    ReleaseDC(nullptr, hdc);
    const int sticky_threshold = 20;       // pixels of "push" past edge before crossing
    constexpr int kPollIntervalMs = 8;     // ~125 Hz polling for raw deltas

    // State machine
    std::atomic<bool> edge_switched{false};
    bool in_sticky_zone = false;
    int sticky_accumulated = 0;
    int virtual_dx = 0;  // net displacement since crossing; used for auto-disengage

    // 1×1 clip rect at (0,0) to physically anchor cursor in remote mode
    RECT clip_rect = {0, 0, 0, 0};

    // Helper: send under mutex
    auto SendEvent = [&](const std::vector<std::byte>& payload) {
        std::lock_guard<std::mutex> lock(g_sender_mutex);
        g_sender_client.Send(shared_km::protocol::MessageKind::InputEvent, payload);
    };

    auto Disengage = [&]() {
        edge_switched = false;
        in_sticky_zone = false;
        sticky_accumulated = 0;
        virtual_dx = 0;
        ClipCursor(nullptr);               // release clip
        // Place cursor near sender's right edge (just inside) so user can re-cross
        SetCursorPos(physical_width - 10, physical_height / 2);
        const auto leave = shared_km::protocol::SerializeInputEventPayload({
            .type = shared_km::protocol::InputEventType::EdgeLeave
        });
        SendEvent(leave);
        SetWindowTextW(g_sender_status, L"cross-screen ended");
    };

    while (running) {
        // Auto-discover receivers on the network
        std::string target_host = cfg.host;
        int target_port = cfg.port;
        bool found_receiver = false;

        SetWindowTextW(g_sender_status, L"scanning...");

        {
            shared_km::network::DiscoveryService disco;
            disco.Scan(8767, [&](const shared_km::network::DiscoveredPeer& peer) {
                target_host = peer.ip;
                target_port = peer.port;
                found_receiver = true;
            }, 2000);

            if (!found_receiver) {
                SetWindowTextW(g_sender_status, L"no receivers found, retrying...");
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
        }

        {
            wchar_t buf[128];
            auto h = UTF8ToWString(target_host);
            std::swprintf(buf, 128, L"connecting %s:%d...", h.c_str(), target_port);
            SetWindowTextW(g_sender_status, buf);
        }
        if (g_sender_client.Connect(target_host, static_cast<std::uint16_t>(target_port))) {
            // Store receiver address for file transfers
            g_current_receiver_host = target_host;
            g_current_receiver_port = target_port;
            SetWindowTextW(g_sender_status, L"authenticating...");
            const auto hello = shared_km::protocol::SerializeHelloPayload({.device_name = "shared-km-sender"});
            const auto auth = shared_km::protocol::SerializeAuthPayload({.token = cfg.token});
            shared_km::protocol::ParsedHeader header;
            std::vector<std::byte> payload;

            if (g_sender_client.Send(shared_km::protocol::MessageKind::Hello, hello) &&
                g_sender_client.Send(shared_km::protocol::MessageKind::Auth, auth) &&
                g_sender_client.Receive(header, payload) &&
                header.kind == shared_km::protocol::MessageKind::AuthResult) {
                const auto result = shared_km::protocol::TryParseAuthResultPayload(payload);
                if (result && result->accepted) {
                    SetWindowTextW(g_sender_status, L"starting hooks...");

                    // Start raw input reader (captures mouse deltas regardless of cursor clip)
                    g_raw_reader.Start();

                    g_mouse_hook.Start([&](const shared_km::input::MouseEvent& event) -> bool {
                        if (!running) return false;
                        if (g_sender_paused.load()) return true;

                        const bool is_remote = cfg.extend_mode && edge_switched.load();

                        // -- Button clicks (forward only in extend+remote or always in copy) --
                        auto ForwardClick = [&](shared_km::protocol::InputEventType et) {
                            if (cfg.extend_mode && !edge_switched.load()) return;
                            SendEvent(shared_km::protocol::SerializeInputEventPayload({.type = et}));
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
                            if (cfg.extend_mode && !edge_switched.load()) return false;
                            SendEvent(shared_km::protocol::SerializeInputEventPayload({
                                .type = shared_km::protocol::InputEventType::MouseWheel,
                                .wheel_delta = static_cast<std::int16_t>(event.wheel_delta),
                            }));
                            return is_remote;
                        }

                        // -- Mouse move --
                        if (event.type != shared_km::input::MouseEventType::Move) return false;

                        // Copy mode: coalesce mouse moves — store latest, main loop sends at fixed rate
                        if (!cfg.extend_mode) {
                            g_coalesced_mouse_x.store(event.x);
                            g_coalesced_mouse_y.store(event.y);
                            g_coalesced_mouse_pending.store(true);
                            return false;
                        }

                        // Extend mode below =====================================

                        if (edge_switched.load()) {
                            // Remote state: cursor is clipped to 1×1.
                            // Movement comes from raw input (polled in the main loop),
                            // so WH_MOUSE_LL move events here are just noise — block.
                            return true;
                        }

                        // Local state: detect edge crossing with sticky threshold
                        if (event.x >= physical_width - sticky_threshold) {
                            if (!in_sticky_zone) {
                                in_sticky_zone = true;
                                sticky_accumulated = 0;
                            }

                            // Read raw deltas to detect "push beyond the physical edge"
                            int raw_dx = 0, raw_dy = 0;
                            g_raw_reader.ReadDelta(raw_dx, raw_dy);
                            if (raw_dx > 0) {
                                sticky_accumulated += raw_dx;
                            }

                            if (sticky_accumulated >= sticky_threshold) {
                                // Cross! Enter remote mode.
                                in_sticky_zone = false;
                                sticky_accumulated = 0;
                                virtual_dx = 0;
                                edge_switched = true;

                                // Physically anchor cursor to 1×1 corner
                                ClipCursor(&clip_rect);
                                SetCursorPos(0, 0);

                                const auto enter = shared_km::protocol::SerializeInputEventPayload({
                                    .type = shared_km::protocol::InputEventType::EdgeEnter,
                                    .edge_side = shared_km::protocol::EdgeSide::Right,
                                    .dx = physical_height,
                                    .dy = event.y,
                                });
                                SendEvent(enter);
                                SetWindowTextW(g_sender_status, L">> cross-screen <<");
                            }
                            return false;
                        }

                        // Not near edge in local mode — nothing to forward in extend mode
                        if (in_sticky_zone) {
                            in_sticky_zone = false;
                            sticky_accumulated = 0;
                        }
                        return false;
                    });

                    g_keyboard_hook.Start([&](const shared_km::input::KeyEventInfo& info) {
                        if (!running) return false;

                        // Scroll Lock toggles pause
                        if (info.virtual_key_code == VK_SCROLL && info.message == WM_KEYDOWN) {
                            g_sender_paused = !g_sender_paused.load();
                            SetWindowTextW(g_sender_status,
                                g_sender_paused.load() ? L"** PAUSED **" : L"forwarding input...");
                            return true;
                        }
                        if (g_sender_paused.load()) return false;

                        // In extend mode, Escape disengages cross-screen
                        if (cfg.extend_mode && edge_switched.load() &&
                            info.virtual_key_code == VK_ESCAPE && info.message == WM_KEYDOWN) {
                            Disengage();
                            return true;  // block Escape locally too
                        }
                        // In extend mode, don't forward keys until edge-switched
                        if (cfg.extend_mode && !edge_switched.load()) return false;
                        std::lock_guard<std::mutex> lock(g_sender_mutex);
                        bool is_down = info.message == WM_KEYDOWN || info.message == WM_SYSKEYDOWN;
                        auto ev = shared_km::protocol::SerializeInputEventPayload({
                            .type = is_down ? shared_km::protocol::InputEventType::KeyDown
                                            : shared_km::protocol::InputEventType::KeyUp,
                            .key_event = {
                                static_cast<std::uint16_t>(info.virtual_key_code),
                                static_cast<std::uint16_t>(info.scan_code),
                                static_cast<std::uint16_t>(info.flags),
                            }
                        });
                        g_sender_client.Send(shared_km::protocol::MessageKind::InputEvent, ev);
                        // Block key locally when in remote mode (prevents double-processing)
                        return cfg.extend_mode && edge_switched.load();
                    });

                    // Start clipboard monitor (sender → receiver)
                    {
                        static std::string s_last_sent_clipboard;
                        g_clipboard_monitor.Start([&](const std::string& text) {
                            if (!running || text.empty() || text == s_last_sent_clipboard) return;
                            const auto data = shared_km::protocol::SerializeClipboardDataPayload({.text = text});
                            {
                                std::lock_guard<std::mutex> lock(g_sender_mutex);
                                g_sender_client.Send(shared_km::protocol::MessageKind::ClipboardData, data);
                            }
                            s_last_sent_clipboard = text;
                        });
                    }

                    SetWindowTextW(g_sender_status, L"forwarding input...");

                    // Main loop: heartbeat + poll raw deltas in remote mode
                    auto last_heartbeat = std::chrono::steady_clock::now();
                    while (running) {
                        auto now = std::chrono::steady_clock::now();

                        // Heartbeat every 5 s
                        if (now - last_heartbeat >= std::chrono::seconds(5)) {
                            auto hb = shared_km::protocol::SerializeHeartbeatPayload({.timestamp_ms = 1});
                            {
                                std::lock_guard<std::mutex> lock(g_sender_mutex);
                                if (!g_sender_client.Send(shared_km::protocol::MessageKind::Heartbeat, hb))
                                    break;
                            }
                            last_heartbeat = now;
                        }

                        // Remote mode: poll raw deltas and forward as relative movement
                        if (edge_switched.load()) {
                            int dx = 0, dy = 0;
                            g_raw_reader.ReadDelta(dx, dy);
                            if (dx != 0 || dy != 0) {
                                virtual_dx += dx;
                                SendEvent(shared_km::protocol::SerializeInputEventPayload({
                                    .type = shared_km::protocol::InputEventType::MouseMoveRelative,
                                    .dx = dx,
                                    .dy = dy,
                                }));

                                // Auto-disengage: user moved left significantly past the entry point
                                if (virtual_dx < -sticky_threshold) {
                                    Disengage();
                                }
                            }

                            // Escape key as alternative disengage
                            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                                Disengage();
                            }
                        }

                        // Copy mode: send coalesced mouse move
                        if (!cfg.extend_mode && g_coalesced_mouse_pending.exchange(false)) {
                            int cx = g_coalesced_mouse_x.load();
                            int cy = g_coalesced_mouse_y.load();
                            SendEvent(shared_km::protocol::SerializeInputEventPayload({
                                .type = shared_km::protocol::InputEventType::MouseMove,
                                .mouse_move = {cx, cy}
                            }));
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
                    }
                } else {
                    SetWindowTextW(g_sender_status, L"auth rejected!");
                }
                g_raw_reader.Stop();
                g_mouse_hook.Stop();
                g_keyboard_hook.Stop();
                edge_switched = false;
                ClipCursor(nullptr);
                SetWindowTextW(g_sender_status, L"disconnected");
            } else {
                SetWindowTextW(g_sender_status, L"hello/auth exchange failed");
            }
            g_sender_client.Close();
        }
        SetWindowTextW(g_sender_status, L"reconnecting...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void StartSender() {
    if (g_sender_active) return;
    g_sender_active = true;
    auto cfg = GetServiceConfig();
    g_sender_thread = std::thread(SenderConnectThreadFunc, cfg, std::ref(g_sender_active));
    SetThreadPriority(g_sender_thread.native_handle(), THREAD_PRIORITY_HIGHEST);
}

void StopSender() {
    g_sender_active = false;
    g_mouse_hook.Stop();
    g_keyboard_hook.Stop();
    g_clipboard_monitor.Stop();
    {
        std::lock_guard<std::mutex> lock(g_sender_mutex);
        g_sender_client.Close();
    }
    if (g_sender_thread.joinable()) g_sender_thread.join();
    SetWindowTextW(g_sender_status, L"stopped");
}

void StopAllServices() {
    StopReceiver();
    StopSender();
}

std::wstring GetStatusText() {
    std::wstring s;
    s += g_receiver_active.load() ? L"● Receiver: Running" : L"○ Receiver: Stopped";
    s += L"  |  ";
    s += g_sender_active.load() ? L"● Sender: Running" : L"○ Sender: Stopped";
    return s;
}

void UpdateStatusBar() {
    if (g_static_status) {
        SetWindowTextW(g_static_status, GetStatusText().c_str());
    }
    wcsncpy_s(g_notify.szTip, GetStatusText().c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_notify);
}

void UpdateToggleButton(int id) {
    HWND btn = GetDlgItem(g_main_wnd, id);
    if (!btn) return;
    bool running = (id == kBtnLaunchReceiver) ? g_receiver_active.load() : g_sender_active.load();
    const wchar_t* name = (id == kBtnLaunchReceiver) ? L"Receiver" : L"Sender";
    wchar_t text[48];
    std::swprintf(text, 48, running ? L"● %s" : L"○ %s", name);
    SetWindowTextW(btn, text);
    SendMessageW(btn, BM_SETCHECK, running ? BST_CHECKED : BST_UNCHECKED, 0);
}

void OnFileTransferComplete(const shared_km::network::FileTransferResult& result) {
    FileTransferEntry entry;
    entry.filename = UTF8ToWString(result.filename);
    entry.size_str = FormatFileSize(result.size);
    entry.success = result.success;
    {
        std::lock_guard<std::mutex> lock(g_file_transfer_mutex);
        g_file_transfer_history.push_back(std::move(entry));
    }
    PostMessageW(g_main_wnd, kMsgFileTransferDone, 0, 0);
}

void RefreshFileTransferList() {
    if (!g_list_file_transfers) return;
    std::lock_guard<std::mutex> lock(g_file_transfer_mutex);
    SendMessageW(g_list_file_transfers, LB_RESETCONTENT, 0, 0);
    for (const auto& entry : g_file_transfer_history) {
        wchar_t line[512];
        std::swprintf(line, 512, L"%s (%s) %s",
                       entry.filename.c_str(),
                       entry.size_str.c_str(),
                       entry.success ? L"✓ done" : L"✗ failed");
        SendMessageW(g_list_file_transfers, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(line));
    }
}

void ShowTrayNotification(const std::wstring& title, const std::wstring& msg) {
    g_notify.uFlags = NIF_INFO;
    wcsncpy_s(g_notify.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(g_notify.szInfo, msg.c_str(), _TRUNCATE);
    g_notify.dwInfoFlags = NIIF_INFO;
    g_notify.uTimeout = 3000;
    Shell_NotifyIconW(NIM_MODIFY, &g_notify);
    g_notify.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

bool SetupTrayIcon(HWND hwnd) {
    g_notify.cbSize = sizeof(NOTIFYICONDATAW);
    g_notify.hWnd = hwnd;
    g_notify.uID = kIdTrayIcon;
    g_notify.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_notify.uCallbackMessage = WM_APP + 1;
    g_notify.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    int ver_num = 0;
    FILE* vf = _wfopen((ExeDir() + L"version.txt").c_str(), L"r");
    if (!vf) vf = fopen("version.txt", "r");
    if (vf) { fscanf(vf, "%d", &ver_num); fclose(vf); }
    wchar_t tip[128];
    std::swprintf(tip, 128, L"Shared KM Controller v0.%02d", ver_num);
    wcsncpy_s(g_notify.szTip, tip, _TRUNCATE);
    return Shell_NotifyIconW(NIM_ADD, &g_notify) != FALSE;
}

// -- Create controls --
void CreateControls(HWND hwnd) {
    const int margin = 12;
    const int group_w = 360;
    const int label_w = 50;
    const int edit_w = group_w - label_w - margin - 6;
    const int ctrl_h = 22;
    const int row_h = 28;

    auto MakeGroup = [&](const wchar_t* title, int y) -> int {
        CreateWindowExW(0, L"BUTTON", title,
                        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                        margin, y, group_w, 90, hwnd, nullptr, g_instance, nullptr);
        return y + 16;
    };

    auto MakeEdit = [&](const wchar_t* label, int x, int& y, int content_h) -> HWND {
        CreateWindowExW(0, L"STATIC", label,
                        WS_CHILD | WS_VISIBLE,
                        x + 8, y + 4, label_w, ctrl_h,
                        hwnd, nullptr, g_instance, nullptr);
        auto edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                    x + 8 + label_w, y, edit_w, ctrl_h,
                                    hwnd, nullptr, g_instance, nullptr);
        y += content_h;
        return edit;
    };

    // Receiver Settings
    int gy = MakeGroup(L"Receiver Settings", margin);
    g_edit_host = MakeEdit(L"Host:", margin, gy, row_h);
    g_edit_port = MakeEdit(L"Port:", margin, gy, row_h + 12);

    // Shared Settings
    gy = MakeGroup(L"Shared Settings", gy + margin);
    g_edit_token = MakeEdit(L"Token:", margin, gy, row_h + 12);

    // Sender Settings
    gy = MakeGroup(L"Sender Settings", gy + margin);
    CreateWindowExW(0, L"STATIC", L"Mode:",
                    WS_CHILD | WS_VISIBLE,
                    margin + 8, gy + 4, label_w, ctrl_h,
                    hwnd, nullptr, g_instance, nullptr);
    g_radio_copy = CreateWindowExW(0, L"BUTTON", L"Copy (Mirror)",
                                   WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                                   margin + 8 + label_w, gy, 100, ctrl_h,
                                   hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRadioCopy)),
                                   g_instance, nullptr);
    g_radio_extend = CreateWindowExW(0, L"BUTTON", L"Extend (Edge)",
                                     WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                     margin + 8 + label_w + 106, gy, 110, ctrl_h,
                                     hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRadioExtend)),
                                     g_instance, nullptr);
    gy += row_h + 8;

    // -- Toggle switches --
    int ctrl_y = gy + 6;
    const int toggle_w = (group_w - 8) / 2;
    const int toggle_h = 34;
    CreateWindowExW(0, L"BUTTON", L"○ Receiver",
                    WS_CHILD | WS_VISIBLE | BS_PUSHLIKE | BS_CHECKBOX,
                    margin, ctrl_y, toggle_w, toggle_h,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnLaunchReceiver)),
                    g_instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"○ Sender",
                    WS_CHILD | WS_VISIBLE | BS_PUSHLIKE | BS_CHECKBOX,
                    margin + toggle_w + 8, ctrl_y, toggle_w, toggle_h,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnLaunchSender)),
                    g_instance, nullptr);
    ctrl_y += toggle_h + 6;

    // -- Status labels under toggle switches --
    const int status_h = 16;
    const int status_text_w = toggle_w - 4;
    g_receiver_status = CreateWindowExW(0, L"STATIC", L"",
                                         WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                                         margin + 4, ctrl_y, status_text_w, status_h,
                                         hwnd, nullptr, g_instance, nullptr);
    g_sender_status = CreateWindowExW(0, L"STATIC", L"",
                                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                                       margin + toggle_w + 8 + 4, ctrl_y, status_text_w, status_h,
                                       hwnd, nullptr, g_instance, nullptr);
    SetWindowTextW(g_receiver_status, L"idle");
    SetWindowTextW(g_sender_status, L"idle");
    ctrl_y += status_h + 4;

    // -- Device discovery list --
    const int dev_h = 110;
    CreateWindowExW(0, L"BUTTON", L"Discovered Receivers",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    margin, ctrl_y, group_w, dev_h,
                    hwnd, nullptr, g_instance, nullptr);
    g_list_devices = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                     WS_CHILD | WS_VISIBLE | LBS_NOINTEGRALHEIGHT |
                                     WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY,
                                     margin + 8, ctrl_y + 16, group_w - 16, dev_h - 24,
                                     hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListDevices)),
                                     g_instance, nullptr);
    ctrl_y += dev_h + 6;

    // -- Status bar --
    g_static_status = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC",
                                       L"Receiver: Stopped | Sender: Stopped",
                                       WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                       margin, ctrl_y, group_w, ctrl_h + 4,
                                       hwnd, nullptr, g_instance, nullptr);
    ctrl_y += ctrl_h + 8;

    // -- Utility buttons --
    int util_w = (group_w - 12) / 3;
    CreateWindowExW(0, L"BUTTON", L"Save Config",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    margin, ctrl_y, util_w, ctrl_h,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnSaveConfig)),
                    g_instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Detect IP",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    margin + util_w + 6, ctrl_y, util_w, ctrl_h,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnDetectIP)),
                    g_instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Discover",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    margin + (util_w + 6) * 2, ctrl_y, util_w, ctrl_h,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnDiscover)),
                    g_instance, nullptr);

    // -- File transfer history --
    const int ft_h = 100;
    int ft_y = ctrl_y + ctrl_h + margin;
    CreateWindowExW(0, L"BUTTON", L"File Transfers (drag files onto this window)",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    margin, ft_y, group_w, ft_h,
                    hwnd, nullptr, g_instance, nullptr);
    g_list_file_transfers = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                             WS_CHILD | WS_VISIBLE | LBS_NOINTEGRALHEIGHT |
                                             WS_VSCROLL | WS_HSCROLL,
                                             margin + 8, ft_y + 16, group_w - 16, ft_h - 24,
                                             hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListFileTransfers)),
                                             g_instance, nullptr);
}

// -- Config loading --
void LoadConfigFromFiles() {
    auto path = SenderConfigPath();
    FILE* file = _wfopen(path.c_str(), L"r");
    if (!file) {
        path = ReceiverConfigPath();
        file = _wfopen(path.c_str(), L"r");
        if (!file) return;
    }

    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    fseek(file, 0, SEEK_SET);
    std::string text(static_cast<std::size_t>(len), '\0');
    fread(text.data(), 1, static_cast<std::size_t>(len), file);
    fclose(file);

    auto ExtractField = [&](const std::string& key) -> std::string {
        auto pos = text.find("\"" + key + "\"");
        if (pos == std::string::npos) return {};
        pos = text.find(':', pos);
        if (pos == std::string::npos) return {};
        pos = text.find_first_not_of(" \t\r\n", pos + 1);
        if (pos == std::string::npos) return {};
        if (text[pos] == '"') {
            pos++;
            auto end = text.find('"', pos);
            if (end == std::string::npos) return {};
            return text.substr(pos, end - pos);
        }
        auto end = text.find_first_of(",}\n\r", pos);
        if (end == std::string::npos) end = text.size();
        auto val = text.substr(pos, end - pos);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\r' || val.back() == '\n'))
            val.pop_back();
        return val;
    };

    auto host = ExtractField("host");
    auto port = ExtractField("port");
    auto token = ExtractField("token");
    auto edge = ExtractField("edge_switching_enabled");  // old format
    auto mode = ExtractField("screen_mode");              // new format

    if (!host.empty()) g_config_host = UTF8ToWString(host);
    if (!port.empty()) g_config_port = UTF8ToWString(port);
    if (!token.empty()) g_config_token = UTF8ToWString(token);
    if (!mode.empty()) {
        g_config_extend_mode = mode == "extend";
    } else {
        g_config_extend_mode = edge == "true";  // fallback to old format
    }
}

void PopulateControls() {
    SetWindowTextW(g_edit_host, g_config_host.c_str());
    SetWindowTextW(g_edit_port, g_config_port.c_str());
    SetWindowTextW(g_edit_token, g_config_token.c_str());
    SendMessageW(g_radio_copy, BM_SETCHECK, g_config_extend_mode ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessageW(g_radio_extend, BM_SETCHECK, g_config_extend_mode ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SaveConfig() {
    ReadControls();
    WriteJsonFile(SenderConfigPath(), g_config_host, g_config_port, g_config_token, g_config_extend_mode);
    WriteJsonFile(ReceiverConfigPath(),
                  g_config_host == L"127.0.0.1" ? L"0.0.0.0" : g_config_host,
                  g_config_port, g_config_token, g_config_extend_mode);
    ShowTrayNotification(L"Configuration Saved", L"Settings saved.");
}

wchar_t g_class_name[] = L"SharedKMUI_Window";

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == g_taskbar_created && g_taskbar_created != 0) {
        Shell_NotifyIconW(NIM_ADD, &g_notify);
        return 0;
    }

    if (msg == WM_APP + 1) {
        switch (lparam) {
        case WM_LBUTTONDBLCLK:
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            break;
        case WM_RBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Show");
            AppendMenuW(menu, MF_STRING, 2, L"Exit");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (cmd == 1) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
            else if (cmd == 2) { DestroyWindow(hwnd); }
            break;
        }
        }
        return 0;
    }

    if (msg == kMsgFileTransferDone) {
        RefreshFileTransferList();
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        LoadConfigFromFiles();
        CreateControls(hwnd);
        PopulateControls();
        UpdateStatusBar();
        SetTimer(hwnd, kIdTimerStatus, 2000, nullptr);
        break;

    case WM_TIMER:
        if (wparam == kIdTimerStatus) {
            UpdateStatusBar();
            UpdateToggleButton(kBtnLaunchReceiver);
            UpdateToggleButton(kBtnLaunchSender);
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wparam);
        switch (id) {
        case kBtnSaveConfig: SaveConfig(); break;
        case kBtnLaunchReceiver:
            if (g_receiver_active.load()) {
                StopReceiver();
            } else {
                StartReceiver();
            }
            UpdateToggleButton(kBtnLaunchReceiver);
            UpdateStatusBar();
            break;
        case kBtnLaunchSender:
            if (g_sender_active.load()) {
                StopSender();
            } else {
                StartSender();
            }
            UpdateToggleButton(kBtnLaunchSender);
            UpdateStatusBar();
            break;
        case kBtnDetectIP:
            {
                auto local_ip = shared_km::network::GetLocalIP();
                SetWindowTextW(g_edit_host, std::wstring(local_ip.begin(), local_ip.end()).c_str());
                g_config_host = std::wstring(local_ip.begin(), local_ip.end());
                ShowTrayNotification(L"Local IP", std::wstring(local_ip.begin(), local_ip.end()).c_str());
            }
            break;
        case kBtnDiscover:
            {
                // Free stored IP strings from previous scan
                int dc = static_cast<int>(SendMessageW(g_list_devices, LB_GETCOUNT, 0, 0));
                for (int i = 0; i < dc; i++) {
                    auto* ip = reinterpret_cast<std::wstring*>(
                        SendMessageW(g_list_devices, LB_GETITEMDATA, i, 0));
                    if (ip && ip != reinterpret_cast<std::wstring*>(LB_ERR))
                        delete ip;
                }
                SendMessageW(g_list_devices, LB_RESETCONTENT, 0, 0);
                SetWindowTextW(g_static_status, L"Scanning for receivers...");
                shared_km::network::DiscoveryService disco;
                bool found = disco.Scan(8767, [&](const shared_km::network::DiscoveredPeer& peer) {
                    auto wip = UTF8ToWString(peer.ip);
                    auto wname = UTF8ToWString(peer.device_name);
                    wchar_t entry[256];
                    std::swprintf(entry, 256, L"%s (%s:%u)", wname.c_str(), wip.c_str(), peer.port);
                    int idx = static_cast<int>(SendMessageW(g_list_devices, LB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(entry)));
                    // Store IP as item data for selection
                    auto* ip_copy = new std::wstring(wip);
                    SendMessageW(g_list_devices, LB_SETITEMDATA, idx,
                                 reinterpret_cast<LPARAM>(ip_copy));
                }, 3000);
                if (!found) {
                    SetWindowTextW(g_static_status, L"No receivers discovered");
                    ShowTrayNotification(L"Discovery", L"No receivers found on network");
                } else {
                    SetWindowTextW(g_static_status, L"Select a device from the list");
                    ShowTrayNotification(L"Discovery", L"Receivers found — select one from list");
                }
            }
            break;
        case kBtnStopAll:
            StopAllServices();
            UpdateToggleButton(kBtnLaunchReceiver);
            UpdateToggleButton(kBtnLaunchSender);
            UpdateStatusBar();
            break;
        }
        // Handle device list selection
        if (HIWORD(wparam) == LBN_SELCHANGE && LOWORD(wparam) == kListDevices) {
            int sel = static_cast<int>(SendMessageW(g_list_devices, LB_GETCURSEL, 0, 0));
            if (sel != LB_ERR) {
                auto* ip = reinterpret_cast<std::wstring*>(
                    SendMessageW(g_list_devices, LB_GETITEMDATA, sel, 0));
                if (ip && ip != reinterpret_cast<std::wstring*>(LB_ERR)) {
                    SetWindowTextW(g_edit_host, ip->c_str());
                    g_config_host = *ip;
                }
            }
        }
        break;
    }

    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (IsWindowVisible(hwnd)) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, kIdTimerStatus);
        Shell_NotifyIconW(NIM_DELETE, &g_notify);
        StopAllServices();
        PostQuitMessage(0);
        break;

    case WM_DROPFILES: {
        HDROP hDrop = reinterpret_cast<HDROP>(wparam);
        const int count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        // Need receiver address from sender connection
        std::string file_host = g_current_receiver_host;
        int file_port_val = g_current_receiver_port;
        if (file_host.empty() || file_port_val == 0) {
            ShowTrayNotification(L"File Transfer", L"No active receiver connection");
            DragFinish(hDrop);
            break;
        }
        for (int i = 0; i < count; i++) {
            wchar_t path[MAX_PATH];
            DragQueryFileW(hDrop, i, path, MAX_PATH);
            auto utf8_path = WStringToUTF8(path);
            auto file_port = static_cast<std::uint16_t>(file_port_val + 1);
            std::thread([utf8_path, file_host, file_port]() {
                shared_km::network::SendFileOverTcp(file_host, file_port, utf8_path,
                                                     OnFileTransferComplete);
            }).detach();
        }
        DragFinish(hDrop);
        break;
    }

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return 0;
}

} // namespace

static void KillExistingProcesses() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"shared_km_receiver.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"shared_km_sender.exe") == 0) {
                HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (proc) { TerminateProcess(proc, 1); CloseHandle(proc); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    // Check for --receiver or --sender mode (run as service, no UI)
    if (lpCmdLine && *lpCmdLine) {
        if (strstr(lpCmdLine, "--receiver") || strstr(lpCmdLine, "-r")) {
            AllocConsole();
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            SetConsoleTitleW(L"shared_km - Receiver");
            shared_km::service::ReceiverApp app;
            return app.Run();
        }
        if (strstr(lpCmdLine, "--sender") || strstr(lpCmdLine, "-s")) {
            AllocConsole();
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            SetConsoleTitleW(L"shared_km - Sender");
            shared_km::service::SenderApp app;
            return app.Run();
        }
    }

    KillExistingProcesses();
    g_instance = hInstance;
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = g_class_name;

    if (!RegisterClassExW(&wc)) return 1;

    int w = 400, h = 680;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    g_main_wnd = CreateWindowExW(0, g_class_name, L"Shared KM Controller",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  x, y, w, h, nullptr, nullptr, hInstance, nullptr);
    if (!g_main_wnd) return 1;

    // Read version.txt and update title
    {
        int ver_num = 0;
        FILE* vf = _wfopen((ExeDir() + L"version.txt").c_str(), L"r");
        if (!vf) vf = fopen("version.txt", "r");
        if (vf) { fscanf(vf, "%d", &ver_num); fclose(vf); }
        wchar_t title[128];
        std::swprintf(title, 128, L"Shared KM Controller v0.%02d", ver_num);
        SetWindowTextW(g_main_wnd, title);
    }

    SetupTrayIcon(g_main_wnd);
    DragAcceptFiles(g_main_wnd, TRUE);
    ShowWindow(g_main_wnd, nCmdShow);
    UpdateWindow(g_main_wnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
