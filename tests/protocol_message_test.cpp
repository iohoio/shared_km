#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "shared_km/protocol/message.hpp"

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestHeaderRoundTrip() {
    const auto bytes = shared_km::protocol::SerializeHeader(
        shared_km::protocol::MessageKind::Auth,
        42
    );

    const auto parsed = shared_km::protocol::TryParseHeader(bytes);
    Expect(parsed.has_value(), "header should parse");
    Expect(parsed->kind == shared_km::protocol::MessageKind::Auth, "header kind mismatch");
    Expect(parsed->header.magic == shared_km::protocol::kProtocolMagic, "header magic mismatch");
    Expect(parsed->header.version == shared_km::protocol::kProtocolVersion, "header version mismatch");
    Expect(parsed->header.payload_size == 42, "header payload size mismatch");
}

void TestHeaderRejectsBadMagic() {
    auto bytes = shared_km::protocol::SerializeHeader(
        shared_km::protocol::MessageKind::Heartbeat,
        8
    );

    std::uint32_t bad_magic = 0;
    std::memcpy(bytes.data(), &bad_magic, sizeof(bad_magic));

    const auto parsed = shared_km::protocol::TryParseHeader(bytes);
    Expect(!parsed.has_value(), "header with bad magic should fail");
}

void TestHelloPayloadRoundTrip() {
    const shared_km::protocol::HelloPayload payload{
        .device_name = "desk-main"
    };

    const auto bytes = shared_km::protocol::SerializeHelloPayload(payload);
    const auto parsed = shared_km::protocol::TryParseHelloPayload(bytes);

    Expect(parsed.has_value(), "hello payload should parse");
    Expect(parsed->device_name == payload.device_name, "hello device name mismatch");
}

void TestAuthPayloadRoundTrip() {
    const shared_km::protocol::AuthPayload payload{
        .token = "top-secret-token"
    };

    const auto bytes = shared_km::protocol::SerializeAuthPayload(payload);
    const auto parsed = shared_km::protocol::TryParseAuthPayload(bytes);

    Expect(parsed.has_value(), "auth payload should parse");
    Expect(parsed->token == payload.token, "auth token mismatch");
}

void TestAuthResultPayloadRoundTrip() {
    const shared_km::protocol::AuthResultPayload payload{
        .accepted = true,
        .message = "accepted"
    };

    const auto bytes = shared_km::protocol::SerializeAuthResultPayload(payload);
    const auto parsed = shared_km::protocol::TryParseAuthResultPayload(bytes);

    Expect(parsed.has_value(), "auth result payload should parse");
    Expect(parsed->accepted == payload.accepted, "auth result accepted mismatch");
    Expect(parsed->message == payload.message, "auth result message mismatch");
}

void TestHeartbeatPayloadRoundTrip() {
    const shared_km::protocol::HeartbeatPayload payload{
        .timestamp_ms = 123456789ULL
    };

    const auto bytes = shared_km::protocol::SerializeHeartbeatPayload(payload);
    const auto parsed = shared_km::protocol::TryParseHeartbeatPayload(bytes);

    Expect(parsed.has_value(), "heartbeat payload should parse");
    Expect(parsed->timestamp_ms == payload.timestamp_ms, "heartbeat timestamp mismatch");
}

void TestMouseMoveInputEventRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::MouseMove,
        .mouse_move = {
            .x = 640,
            .y = 360
        }
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "input event payload should parse");
    Expect(parsed->type == payload.type, "input event type mismatch");
    Expect(parsed->mouse_move.x == payload.mouse_move.x, "mouse move x mismatch");
    Expect(parsed->mouse_move.y == payload.mouse_move.y, "mouse move y mismatch");
}

void TestLeftButtonDownInputEventRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::LeftButtonDown
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "left button down event should parse");
    Expect(parsed->type == payload.type, "left button down type mismatch");
}

void TestEdgeEnterInputEventRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::EdgeEnter,
        .edge_side = shared_km::protocol::EdgeSide::Right,
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "edge enter event should parse");
    Expect(parsed->type == payload.type, "edge enter type mismatch");
    Expect(parsed->edge_side == payload.edge_side, "edge side mismatch");
}

void TestEdgeLeaveInputEventRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::EdgeLeave,
        .edge_side = shared_km::protocol::EdgeSide::Left,
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "edge leave event should parse");
    Expect(parsed->type == payload.type, "edge leave type mismatch");
    Expect(parsed->edge_side == payload.edge_side, "edge side mismatch");
}

void TestMouseMoveRelativeInputEventRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::MouseMoveRelative,
        .dx = 10,
        .dy = -5,
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "mouse move relative event should parse");
    Expect(parsed->type == payload.type, "mouse move relative type mismatch");
    Expect(parsed->dx == payload.dx, "mouse move relative dx mismatch");
    Expect(parsed->dy == payload.dy, "mouse move relative dy mismatch");
}

void TestLeftButtonUpInputEventRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::LeftButtonUp
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "left button up event should parse");
    Expect(parsed->type == payload.type, "left button up type mismatch");
}

void TestKeyDownInputEventRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::KeyDown,
        .key_event = {
            .virtual_key_code = 0x41,
            .scan_code = 0x1E,
            .flags = 0,
        }
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "key down event should parse");
    Expect(parsed->type == payload.type, "key down type mismatch");
    Expect(parsed->key_event.virtual_key_code == 0x41, "key down vk mismatch");
    Expect(parsed->key_event.scan_code == 0x1E, "key down scan mismatch");
}

void TestKeyUpInputEventRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::KeyUp,
        .key_event = {
            .virtual_key_code = 0x42,
            .scan_code = 0x30,
            .flags = 1,
        }
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "key up event should parse");
    Expect(parsed->type == payload.type, "key up type mismatch");
    Expect(parsed->key_event.virtual_key_code == 0x42, "key up vk mismatch");
    Expect(parsed->key_event.scan_code == 0x30, "key up scan mismatch");
    Expect(parsed->key_event.flags == 1, "key up flags mismatch");
}

void TestMouseWheelInputEventRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::MouseWheel,
        .wheel_delta = 120,
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "mouse wheel event should parse");
    Expect(parsed->type == payload.type, "mouse wheel type mismatch");
    Expect(parsed->wheel_delta == payload.wheel_delta, "mouse wheel delta mismatch");
}

void TestMouseWheelNegativeDeltaRoundTrip() {
    const shared_km::protocol::InputEventPayload payload{
        .type = shared_km::protocol::InputEventType::MouseWheel,
        .wheel_delta = -120,
    };

    const auto bytes = shared_km::protocol::SerializeInputEventPayload(payload);
    const auto parsed = shared_km::protocol::TryParseInputEventPayload(bytes);

    Expect(parsed.has_value(), "mouse wheel negative delta should parse");
    Expect(parsed->wheel_delta == -120, "mouse wheel negative delta mismatch");
}

}  // namespace

int main() {
    TestHeaderRoundTrip();
    TestHeaderRejectsBadMagic();
    TestHelloPayloadRoundTrip();
    TestAuthPayloadRoundTrip();
    TestAuthResultPayloadRoundTrip();
    TestHeartbeatPayloadRoundTrip();
    TestMouseMoveInputEventRoundTrip();
    TestLeftButtonDownInputEventRoundTrip();
    TestLeftButtonUpInputEventRoundTrip();
    TestEdgeEnterInputEventRoundTrip();
    TestEdgeLeaveInputEventRoundTrip();
    TestMouseMoveRelativeInputEventRoundTrip();
    TestKeyDownInputEventRoundTrip();
    TestKeyUpInputEventRoundTrip();
    TestMouseWheelInputEventRoundTrip();
    TestMouseWheelNegativeDeltaRoundTrip();
    return 0;
}
