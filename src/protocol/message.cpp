#include "shared_km/protocol/message.hpp"

#include <array>
#include <cstring>

namespace shared_km::protocol {

namespace {

template <typename T>
void AppendValue(std::vector<std::byte>& output, const T& value) {
    const auto* raw = reinterpret_cast<const std::byte*>(&value);
    output.insert(output.end(), raw, raw + sizeof(T));
}

template <typename T>
std::optional<T> ReadValue(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset + sizeof(T) > bytes.size()) {
        return std::nullopt;
    }

    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::vector<std::byte> SerializeString(std::string_view value) {
    std::vector<std::byte> bytes;
    const auto size = static_cast<std::uint32_t>(value.size());
    bytes.reserve(sizeof(size) + value.size());
    AppendValue(bytes, size);
    const auto* raw = reinterpret_cast<const std::byte*>(value.data());
    bytes.insert(bytes.end(), raw, raw + value.size());
    return bytes;
}

std::optional<std::string> TryParseString(std::span<const std::byte> bytes) {
    const auto size = ReadValue<std::uint32_t>(bytes, 0);
    if (!size) {
        return std::nullopt;
    }

    const auto required = sizeof(std::uint32_t) + static_cast<std::size_t>(*size);
    if (bytes.size() != required) {
        return std::nullopt;
    }

    const auto* begin = reinterpret_cast<const char*>(bytes.data() + sizeof(std::uint32_t));
    return std::string(begin, begin + *size);
}

}  // namespace

HeaderBytes SerializeHeader(MessageKind kind, std::uint32_t payload_size) {
    const MessageHeader header{
        .magic = kProtocolMagic,
        .version = kProtocolVersion,
        .kind = static_cast<std::uint16_t>(kind),
        .payload_size = payload_size,
    };

    HeaderBytes bytes{};
    std::memcpy(bytes.data(), &header, sizeof(header));
    return bytes;
}

std::optional<ParsedHeader> TryParseHeader(std::span<const std::byte> bytes) {
    if (bytes.size() != sizeof(MessageHeader)) {
        return std::nullopt;
    }

    MessageHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));

    if (header.magic != kProtocolMagic || header.version != kProtocolVersion) {
        return std::nullopt;
    }

    const auto kind = static_cast<MessageKind>(header.kind);
    switch (kind) {
    case MessageKind::Hello:
    case MessageKind::Auth:
    case MessageKind::AuthResult:
    case MessageKind::Heartbeat:
    case MessageKind::InputEvent:
    case MessageKind::ClipboardData:
        return ParsedHeader{header, kind};
    default:
        return std::nullopt;
    }
}

std::vector<std::byte> SerializeHelloPayload(const HelloPayload& payload) {
    return SerializeString(payload.device_name);
}

std::optional<HelloPayload> TryParseHelloPayload(std::span<const std::byte> bytes) {
    const auto value = TryParseString(bytes);
    if (!value) {
        return std::nullopt;
    }
    return HelloPayload{.device_name = *value};
}

std::vector<std::byte> SerializeAuthPayload(const AuthPayload& payload) {
    return SerializeString(payload.token);
}

std::optional<AuthPayload> TryParseAuthPayload(std::span<const std::byte> bytes) {
    const auto value = TryParseString(bytes);
    if (!value) {
        return std::nullopt;
    }
    return AuthPayload{.token = *value};
}

std::vector<std::byte> SerializeAuthResultPayload(const AuthResultPayload& payload) {
    std::vector<std::byte> bytes;
    bytes.reserve(sizeof(std::uint8_t) + sizeof(std::uint32_t) + payload.message.size());

    const auto accepted = static_cast<std::uint8_t>(payload.accepted ? 1 : 0);
    AppendValue(bytes, accepted);

    const auto message_bytes = SerializeString(payload.message);
    bytes.insert(bytes.end(), message_bytes.begin(), message_bytes.end());
    return bytes;
}

std::optional<AuthResultPayload> TryParseAuthResultPayload(std::span<const std::byte> bytes) {
    const auto accepted = ReadValue<std::uint8_t>(bytes, 0);
    if (!accepted || bytes.size() < sizeof(std::uint8_t) + sizeof(std::uint32_t)) {
        return std::nullopt;
    }

    const auto message = TryParseString(bytes.subspan(sizeof(std::uint8_t)));
    if (!message) {
        return std::nullopt;
    }

    return AuthResultPayload{
        .accepted = *accepted != 0,
        .message = *message,
    };
}

std::vector<std::byte> SerializeHeartbeatPayload(const HeartbeatPayload& payload) {
    std::vector<std::byte> bytes;
    bytes.reserve(sizeof(payload.timestamp_ms));
    AppendValue(bytes, payload.timestamp_ms);
    return bytes;
}

std::optional<HeartbeatPayload> TryParseHeartbeatPayload(std::span<const std::byte> bytes) {
    if (bytes.size() != sizeof(std::uint64_t)) {
        return std::nullopt;
    }

    const auto timestamp = ReadValue<std::uint64_t>(bytes, 0);
    if (!timestamp) {
        return std::nullopt;
    }

    return HeartbeatPayload{.timestamp_ms = *timestamp};
}

std::vector<std::byte> SerializeClipboardDataPayload(const ClipboardDataPayload& payload) {
    return SerializeString(payload.text);
}

std::optional<ClipboardDataPayload> TryParseClipboardDataPayload(std::span<const std::byte> bytes) {
    const auto value = TryParseString(bytes);
    if (!value) {
        return std::nullopt;
    }
    return ClipboardDataPayload{.text = *value};
}

std::vector<std::byte> SerializeInputEventPayload(const InputEventPayload& payload) {
    std::vector<std::byte> bytes;
    bytes.reserve(sizeof(std::uint16_t) + sizeof(std::int32_t) * 2 + sizeof(std::uint16_t));

    const auto type = static_cast<std::uint16_t>(payload.type);
    AppendValue(bytes, type);

    switch (payload.type) {
    case InputEventType::MouseMove:
        AppendValue(bytes, payload.mouse_move.x);
        AppendValue(bytes, payload.mouse_move.y);
        break;
    case InputEventType::EdgeEnter:
        AppendValue(bytes, static_cast<std::uint16_t>(payload.edge_side));
        AppendValue(bytes, payload.dy);  // sender's y-position at crossing
        AppendValue(bytes, payload.dx);  // sender's screen height
        break;
    case InputEventType::EdgeLeave:
        AppendValue(bytes, static_cast<std::uint16_t>(payload.edge_side));
        break;
    case InputEventType::MouseMoveRelative:
        AppendValue(bytes, payload.dx);
        AppendValue(bytes, payload.dy);
        break;
    case InputEventType::LeftButtonDown:
    case InputEventType::LeftButtonUp:
    case InputEventType::RightButtonDown:
    case InputEventType::RightButtonUp:
        break;
    case InputEventType::KeyDown:
    case InputEventType::KeyUp:
        AppendValue(bytes, payload.key_event.virtual_key_code);
        AppendValue(bytes, payload.key_event.scan_code);
        AppendValue(bytes, payload.key_event.flags);
        break;
    case InputEventType::MouseWheel:
        AppendValue(bytes, payload.wheel_delta);
        break;
    }

    return bytes;
}

std::optional<InputEventPayload> TryParseInputEventPayload(std::span<const std::byte> bytes) {
    const auto raw_type = ReadValue<std::uint16_t>(bytes, 0);
    if (!raw_type) {
        return std::nullopt;
    }

    const auto type = static_cast<InputEventType>(*raw_type);
    switch (type) {
    case InputEventType::MouseMove: {
        if (bytes.size() != sizeof(std::uint16_t) + sizeof(std::int32_t) * 2) {
            return std::nullopt;
        }

        const auto x = ReadValue<std::int32_t>(bytes, sizeof(std::uint16_t));
        const auto y = ReadValue<std::int32_t>(bytes, sizeof(std::uint16_t) + sizeof(std::int32_t));
        if (!x || !y) {
            return std::nullopt;
        }

        return InputEventPayload{
            .type = type,
            .mouse_move = MouseMovePayload{
                .x = *x,
                .y = *y,
            }
        };
    }
    case InputEventType::EdgeEnter: {
        if (bytes.size() != sizeof(std::uint16_t) * 2 + sizeof(std::int32_t) * 2) {
            return std::nullopt;
        }

        const auto raw_side = ReadValue<std::uint16_t>(bytes, sizeof(std::uint16_t));
        if (!raw_side) {
            return std::nullopt;
        }

        const auto y = ReadValue<std::int32_t>(bytes, sizeof(std::uint16_t) * 2);
        if (!y) {
            return std::nullopt;
        }

        const auto h = ReadValue<std::int32_t>(bytes, sizeof(std::uint16_t) * 2 + sizeof(std::int32_t));
        if (!h) {
            return std::nullopt;
        }

        const auto side = static_cast<EdgeSide>(*raw_side);
        switch (side) {
        case EdgeSide::Left:
        case EdgeSide::Right:
        case EdgeSide::Top:
        case EdgeSide::Bottom:
            return InputEventPayload{
                .type = type,
                .edge_side = side,
                .dx = *h,
                .dy = *y,
            };
        default:
            return std::nullopt;
        }
    }
    case InputEventType::EdgeLeave: {
        if (bytes.size() != sizeof(std::uint16_t) * 2) {
            return std::nullopt;
        }

        const auto raw_side = ReadValue<std::uint16_t>(bytes, sizeof(std::uint16_t));
        if (!raw_side) {
            return std::nullopt;
        }

        const auto side = static_cast<EdgeSide>(*raw_side);
        switch (side) {
        case EdgeSide::Left:
        case EdgeSide::Right:
        case EdgeSide::Top:
        case EdgeSide::Bottom:
            return InputEventPayload{
                .type = type,
                .edge_side = side,
            };
        default:
            return std::nullopt;
        }
    }
    case InputEventType::MouseMoveRelative: {
        if (bytes.size() != sizeof(std::uint16_t) + sizeof(std::int32_t) * 2) {
            return std::nullopt;
        }

        const auto dx = ReadValue<std::int32_t>(bytes, sizeof(std::uint16_t));
        const auto dy = ReadValue<std::int32_t>(bytes, sizeof(std::uint16_t) + sizeof(std::int32_t));
        if (!dx || !dy) {
            return std::nullopt;
        }

        return InputEventPayload{
            .type = type,
            .dx = *dx,
            .dy = *dy,
        };
    }
    case InputEventType::LeftButtonDown:
    case InputEventType::LeftButtonUp:
    case InputEventType::RightButtonDown:
    case InputEventType::RightButtonUp:
        if (bytes.size() != sizeof(std::uint16_t)) {
            return std::nullopt;
        }
        return InputEventPayload{.type = type};
    case InputEventType::KeyDown:
    case InputEventType::KeyUp: {
        if (bytes.size() != sizeof(std::uint16_t) + sizeof(std::uint16_t) * 3) {
            return std::nullopt;
        }

        const auto vk = ReadValue<std::uint16_t>(bytes, sizeof(std::uint16_t));
        const auto scan = ReadValue<std::uint16_t>(bytes, sizeof(std::uint16_t) + sizeof(std::uint16_t));
        const auto flags = ReadValue<std::uint16_t>(bytes, sizeof(std::uint16_t) + sizeof(std::uint16_t) * 2);
        if (!vk || !scan || !flags) {
            return std::nullopt;
        }

        return InputEventPayload{
            .type = type,
            .key_event = KeyEventPayload{
                .virtual_key_code = *vk,
                .scan_code = *scan,
                .flags = *flags,
            }
        };
    }
    case InputEventType::MouseWheel: {
        if (bytes.size() != sizeof(std::uint16_t) + sizeof(std::int16_t)) {
            return std::nullopt;
        }
        const auto delta = ReadValue<std::int16_t>(bytes, sizeof(std::uint16_t));
        if (!delta) {
            return std::nullopt;
        }
        return InputEventPayload{
            .type = type,
            .wheel_delta = *delta,
        };
    }
    default:
        return std::nullopt;
    }
}

std::string ToString(MessageKind kind) {
    switch (kind) {
    case MessageKind::Hello:
        return "Hello";
    case MessageKind::Auth:
        return "Auth";
    case MessageKind::AuthResult:
        return "AuthResult";
    case MessageKind::Heartbeat:
        return "Heartbeat";
    case MessageKind::InputEvent:
        return "InputEvent";
    case MessageKind::ClipboardData:
        return "ClipboardData";
    default:
        return "Unknown";
    }
}

std::string ToString(InputEventType type) {
    switch (type) {
    case InputEventType::MouseMove:
        return "MouseMove";
    case InputEventType::LeftButtonDown:
        return "LeftButtonDown";
    case InputEventType::LeftButtonUp:
        return "LeftButtonUp";
    case InputEventType::RightButtonDown:
        return "RightButtonDown";
    case InputEventType::RightButtonUp:
        return "RightButtonUp";
    case InputEventType::EdgeEnter:
        return "EdgeEnter";
    case InputEventType::EdgeLeave:
        return "EdgeLeave";
    case InputEventType::MouseMoveRelative:
        return "MouseMoveRelative";
    case InputEventType::KeyDown:
        return "KeyDown";
    case InputEventType::KeyUp:
        return "KeyUp";
    case InputEventType::MouseWheel:
        return "MouseWheel";
    default:
        return "Unknown";
    }
}

std::string ToString(EdgeSide side) {
    switch (side) {
    case EdgeSide::Left:
        return "Left";
    case EdgeSide::Right:
        return "Right";
    case EdgeSide::Top:
        return "Top";
    case EdgeSide::Bottom:
        return "Bottom";
    default:
        return "Unknown";
    }
}

}  // namespace shared_km::protocol
