#include "shared_km/service/input_event_dispatcher.hpp"

#include <string>

namespace shared_km::service {

DispatchResult DispatchInputEvent(
    const shared_km::protocol::InputEventPayload& input_event,
    InputEventSink& sink
) {
    switch (input_event.type) {
    case shared_km::protocol::InputEventType::MouseMove:
        if (!sink.MoveMouseAbsolute(input_event.mouse_move.x, input_event.mouse_move.y)) {
            return {
                .ok = false,
                .message = "failed to inject mouse move, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message =
                "mouse move event received: (" +
                std::to_string(input_event.mouse_move.x) +
                ", " +
                std::to_string(input_event.mouse_move.y) +
                ")"
        };
    case shared_km::protocol::InputEventType::EdgeEnter:
        if (!sink.EdgeEnter(input_event.edge_side, input_event.dy, input_event.dx)) {
            return {
                .ok = false,
                .message = "failed to handle edge enter, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message = "edge enter event received: " + shared_km::protocol::ToString(input_event.edge_side)
        };
    case shared_km::protocol::InputEventType::EdgeLeave:
        if (!sink.EdgeLeave()) {
            return {
                .ok = false,
                .message = "failed to handle edge leave, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message = "edge leave event received"
        };
    case shared_km::protocol::InputEventType::MouseMoveRelative:
        if (!sink.MoveMouseRelative(input_event.dx, input_event.dy)) {
            return {
                .ok = false,
                .message = "failed to inject relative mouse move, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message =
                "mouse move relative event received: (" +
                std::to_string(input_event.dx) +
                ", " +
                std::to_string(input_event.dy) +
                ")"
        };
    case shared_km::protocol::InputEventType::LeftButtonDown:
        if (!sink.LeftButtonDown()) {
            return {
                .ok = false,
                .message = "failed to inject left button down, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message = "left button down event received"
        };
    case shared_km::protocol::InputEventType::LeftButtonUp:
        if (!sink.LeftButtonUp()) {
            return {
                .ok = false,
                .message = "failed to inject left button up, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message = "left button up event received"
        };
    case shared_km::protocol::InputEventType::RightButtonDown:
        if (!sink.RightButtonDown()) {
            return {
                .ok = false,
                .message = "failed to inject right button down, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message = "right button down event received"
        };
    case shared_km::protocol::InputEventType::RightButtonUp:
        if (!sink.RightButtonUp()) {
            return {
                .ok = false,
                .message = "failed to inject right button up, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message = "right button up event received"
        };
    case shared_km::protocol::InputEventType::MouseWheel:
        if (!sink.MouseWheel(input_event.wheel_delta)) {
            return {
                .ok = false,
                .message = "failed to inject mouse wheel, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message = "mouse wheel event received: delta=" + std::to_string(input_event.wheel_delta)
        };
    case shared_km::protocol::InputEventType::KeyDown:
        if (!sink.KeyDown(input_event.key_event.virtual_key_code,
                          input_event.key_event.scan_code,
                          input_event.key_event.flags)) {
            return {
                .ok = false,
                .message = "failed to inject key down, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message =
                "key down event received: vk=" +
                std::to_string(input_event.key_event.virtual_key_code) +
                " scan=" + std::to_string(input_event.key_event.scan_code)
        };
    case shared_km::protocol::InputEventType::KeyUp:
        if (!sink.KeyUp(input_event.key_event.virtual_key_code,
                        input_event.key_event.scan_code,
                        input_event.key_event.flags)) {
            return {
                .ok = false,
                .message = "failed to inject key up, win32 error=" + std::to_string(sink.LastError())
            };
        }
        return {
            .ok = true,
            .message =
                "key up event received: vk=" +
                std::to_string(input_event.key_event.virtual_key_code) +
                " scan=" + std::to_string(input_event.key_event.scan_code)
        };
    }

    return {
        .ok = false,
        .message = "unsupported input event"
    };
}

}  // namespace shared_km::service
