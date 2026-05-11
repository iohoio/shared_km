#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shared_km::protocol {

inline constexpr std::uint32_t kProtocolMagic = 0x4D4B4853;  // "SHKM"
inline constexpr std::uint16_t kProtocolVersion = 1;

enum class MessageKind : std::uint16_t {
    Hello = 1,
    Auth = 2,
    AuthResult = 3,
    Heartbeat = 4,
    InputEvent = 5,
    ClipboardData = 6,
};

struct MessageHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t kind;
    std::uint32_t payload_size;
};

struct ParsedHeader {
    MessageHeader header;
    MessageKind kind;
};

struct HelloPayload {
    std::string device_name;
};

struct AuthPayload {
    std::string token;
};

struct AuthResultPayload {
    bool accepted = false;
    std::string message;
};

struct HeartbeatPayload {
    std::uint64_t timestamp_ms = 0;
};

struct ClipboardDataPayload {
    std::string text;
};

enum class EdgeSide : std::uint16_t {
    Left = 1,
    Right = 2,
    Top = 3,
    Bottom = 4,
};

enum class InputEventType : std::uint16_t {
    MouseMove = 1,
    LeftButtonDown = 2,
    LeftButtonUp = 3,
    EdgeEnter = 4,
    EdgeLeave = 5,
    MouseMoveRelative = 6,
    KeyDown = 7,
    KeyUp = 8,
    RightButtonDown = 9,
    RightButtonUp = 10,
    MouseWheel = 11,
};

struct MouseMovePayload {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct MouseMoveRelativePayload {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
};

struct KeyEventPayload {
    std::uint16_t virtual_key_code = 0;
    std::uint16_t scan_code = 0;
    std::uint16_t flags = 0;
};

struct InputEventPayload {
    InputEventType type = InputEventType::MouseMove;
    MouseMovePayload mouse_move{};
    EdgeSide edge_side = EdgeSide::Right;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    KeyEventPayload key_event{};
    std::int16_t wheel_delta = 0;
};

using HeaderBytes = std::array<std::byte, sizeof(MessageHeader)>;

constexpr std::size_t HeaderSize() {
    return sizeof(MessageHeader);
}

HeaderBytes SerializeHeader(MessageKind kind, std::uint32_t payload_size);
std::optional<ParsedHeader> TryParseHeader(std::span<const std::byte> bytes);

std::vector<std::byte> SerializeHelloPayload(const HelloPayload& payload);
std::optional<HelloPayload> TryParseHelloPayload(std::span<const std::byte> bytes);

std::vector<std::byte> SerializeAuthPayload(const AuthPayload& payload);
std::optional<AuthPayload> TryParseAuthPayload(std::span<const std::byte> bytes);

std::vector<std::byte> SerializeAuthResultPayload(const AuthResultPayload& payload);
std::optional<AuthResultPayload> TryParseAuthResultPayload(std::span<const std::byte> bytes);

std::vector<std::byte> SerializeHeartbeatPayload(const HeartbeatPayload& payload);
std::optional<HeartbeatPayload> TryParseHeartbeatPayload(std::span<const std::byte> bytes);

std::vector<std::byte> SerializeClipboardDataPayload(const ClipboardDataPayload& payload);
std::optional<ClipboardDataPayload> TryParseClipboardDataPayload(std::span<const std::byte> bytes);

std::vector<std::byte> SerializeInputEventPayload(const InputEventPayload& payload);
std::optional<InputEventPayload> TryParseInputEventPayload(std::span<const std::byte> bytes);

std::string ToString(MessageKind kind);
std::string ToString(InputEventType type);
std::string ToString(EdgeSide side);

}  // namespace shared_km::protocol
