#pragma once

#include "shared_km/protocol/message.hpp"

#include <string>

namespace shared_km::service {

class InputEventSink {
public:
    virtual ~InputEventSink() = default;

    virtual bool MoveMouseAbsolute(int x, int y) = 0;
    virtual bool MoveMouseRelative(int dx, int dy) = 0;
    virtual bool EdgeEnter(protocol::EdgeSide side, int y, int sender_height) = 0;
    virtual bool EdgeLeave() = 0;
    virtual bool LeftButtonDown() = 0;
    virtual bool LeftButtonUp() = 0;
    virtual bool RightButtonDown() = 0;
    virtual bool RightButtonUp() = 0;
    virtual bool MouseWheel(short delta) = 0;
    virtual bool KeyDown(unsigned int vk, unsigned int scan, unsigned int flags) = 0;
    virtual bool KeyUp(unsigned int vk, unsigned int scan, unsigned int flags) = 0;
    virtual unsigned long LastError() const = 0;
};

struct DispatchResult {
    bool ok = false;
    std::string message;
};

DispatchResult DispatchInputEvent(
    const shared_km::protocol::InputEventPayload& input_event,
    InputEventSink& sink
);

}  // namespace shared_km::service
