#include "shared_km/service/input_event_dispatcher.hpp"

#include <stdexcept>
#include <string>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class FakeInputSink final : public shared_km::service::InputEventSink {
public:
    bool move_called = false;
    bool move_relative_called = false;
    bool edge_enter_called = false;
    bool edge_leave_called = false;
    bool left_down_called = false;
    bool left_up_called = false;
    bool right_down_called = false;
    bool right_up_called = false;
    bool key_down_called = false;
    bool key_up_called = false;
    bool wheel_called = false;
    int last_x = 0;
    int last_y = 0;
    int last_dx = 0;
    int last_dy = 0;
    unsigned int last_vk = 0;
    unsigned int last_scan = 0;
    unsigned int last_key_flags = 0;
    shared_km::protocol::EdgeSide last_edge_side = shared_km::protocol::EdgeSide::Right;
    bool move_result = true;
    bool move_relative_result = true;
    bool edge_enter_result = true;
    bool edge_leave_result = true;
    bool left_down_result = true;
    bool left_up_result = true;
    bool right_down_result = true;
    bool right_up_result = true;
    bool key_down_result = true;
    bool key_up_result = true;
    unsigned long last_error = 0;
    short last_wheel_delta = 0;
    bool wheel_result = true;

    bool MoveMouseAbsolute(int x, int y) override {
        move_called = true;
        last_x = x;
        last_y = y;
        return move_result;
    }

    bool MoveMouseRelative(int dx, int dy) override {
        move_relative_called = true;
        last_dx = dx;
        last_dy = dy;
        return move_relative_result;
    }

    bool EdgeEnter(shared_km::protocol::EdgeSide side, int, int) override {
        edge_enter_called = true;
        last_edge_side = side;
        return edge_enter_result;
    }

    bool EdgeLeave() override {
        edge_leave_called = true;
        return edge_leave_result;
    }

    bool LeftButtonDown() override {
        left_down_called = true;
        return left_down_result;
    }

    bool LeftButtonUp() override {
        left_up_called = true;
        return left_up_result;
    }

    bool RightButtonDown() override {
        right_down_called = true;
        return right_down_result;
    }

    bool RightButtonUp() override {
        right_up_called = true;
        return right_up_result;
    }

    bool MouseWheel(short delta) override {
        wheel_called = true;
        last_wheel_delta = delta;
        return wheel_result;
    }

    bool KeyDown(unsigned int vk, unsigned int scan, unsigned int flags) override {
        key_down_called = true;
        last_vk = vk;
        last_scan = scan;
        last_key_flags = flags;
        return key_down_result;
    }

    bool KeyUp(unsigned int vk, unsigned int scan, unsigned int flags) override {
        key_up_called = true;
        last_vk = vk;
        last_scan = scan;
        last_key_flags = flags;
        return key_up_result;
    }

    unsigned long LastError() const override {
        return last_error;
    }
};

void TestMouseMoveDispatchSuccess() {
    FakeInputSink sink;
    const auto result = shared_km::service::DispatchInputEvent(
        {
            .type = shared_km::protocol::InputEventType::MouseMove,
            .mouse_move = {.x = 640, .y = 360},
        },
        sink
    );

    Expect(result.ok, "mouse move dispatch should succeed");
    Expect(sink.move_called, "mouse move handler should be called");
    Expect(sink.last_x == 640 && sink.last_y == 360, "mouse move coordinates should match");
    Expect(result.message == "mouse move event received: (640, 360)", "mouse move log should match");
}

void TestEdgeEnterDispatchSuccess() {
    FakeInputSink sink;
    const auto result = shared_km::service::DispatchInputEvent(
        {
            .type = shared_km::protocol::InputEventType::EdgeEnter,
            .edge_side = shared_km::protocol::EdgeSide::Right,
        },
        sink
    );

    Expect(result.ok, "edge enter dispatch should succeed");
    Expect(sink.edge_enter_called, "edge enter handler should be called");
    Expect(sink.last_edge_side == shared_km::protocol::EdgeSide::Right, "edge side should match");
    Expect(result.message == "edge enter event received: Right", "edge enter log should match");
}

void TestEdgeLeaveDispatchSuccess() {
    FakeInputSink sink;
    const auto result = shared_km::service::DispatchInputEvent(
        {
            .type = shared_km::protocol::InputEventType::EdgeLeave,
            .edge_side = shared_km::protocol::EdgeSide::Left,
        },
        sink
    );

    Expect(result.ok, "edge leave dispatch should succeed");
    Expect(sink.edge_leave_called, "edge leave handler should be called");
    Expect(result.message == "edge leave event received", "edge leave log should match");
}

void TestMouseMoveRelativeDispatchSuccess() {
    FakeInputSink sink;
    const auto result = shared_km::service::DispatchInputEvent(
        {
            .type = shared_km::protocol::InputEventType::MouseMoveRelative,
            .dx = 15,
            .dy = -8,
        },
        sink
    );

    Expect(result.ok, "mouse move relative dispatch should succeed");
    Expect(sink.move_relative_called, "mouse move relative handler should be called");
    Expect(sink.last_dx == 15 && sink.last_dy == -8, "mouse move relative deltas should match");
    Expect(result.message == "mouse move relative event received: (15, -8)", "mouse move relative log should match");
}

void TestLeftButtonDownDispatchSuccess() {
    FakeInputSink sink;
    const auto result = shared_km::service::DispatchInputEvent(
        {.type = shared_km::protocol::InputEventType::LeftButtonDown},
        sink
    );

    Expect(result.ok, "left button down dispatch should succeed");
    Expect(sink.left_down_called, "left button down handler should be called");
    Expect(result.message == "left button down event received", "left button down log should match");
}

void TestLeftButtonUpDispatchSuccess() {
    FakeInputSink sink;
    const auto result = shared_km::service::DispatchInputEvent(
        {.type = shared_km::protocol::InputEventType::LeftButtonUp},
        sink
    );

    Expect(result.ok, "left button up dispatch should succeed");
    Expect(sink.left_up_called, "left button up handler should be called");
    Expect(result.message == "left button up event received", "left button up log should match");
}

void TestLeftButtonUpDispatchFailure() {
    FakeInputSink sink;
    sink.left_up_result = false;
    sink.last_error = 5;

    const auto result = shared_km::service::DispatchInputEvent(
        {.type = shared_km::protocol::InputEventType::LeftButtonUp},
        sink
    );

    Expect(!result.ok, "left button up dispatch should fail");
    Expect(sink.left_up_called, "left button up handler should be called on failure");
    Expect(result.message == "failed to inject left button up, win32 error=5", "left button up error should match");
}

}  // namespace

void TestKeyDownDispatchSuccess() {
    FakeInputSink sink;
    const auto result = shared_km::service::DispatchInputEvent(
        {
            .type = shared_km::protocol::InputEventType::KeyDown,
            .key_event = {.virtual_key_code = 0x41, .scan_code = 0x1E, .flags = 0},
        },
        sink
    );

    Expect(result.ok, "key down dispatch should succeed");
    Expect(sink.key_down_called, "key down handler should be called");
    Expect(sink.last_vk == 0x41, "key down vk should match");
    Expect(sink.last_scan == 0x1E, "key down scan should match");
    Expect(result.message == "key down event received: vk=65 scan=30", "key down log should match");
}

void TestKeyDownDispatchFailure() {
    FakeInputSink sink;
    sink.key_down_result = false;
    sink.last_error = 5;

    const auto result = shared_km::service::DispatchInputEvent(
        {
            .type = shared_km::protocol::InputEventType::KeyDown,
            .key_event = {.virtual_key_code = 0x41, .scan_code = 0x1E, .flags = 0},
        },
        sink
    );

    Expect(!result.ok, "key down dispatch should fail");
    Expect(sink.key_down_called, "key down handler should be called on failure");
    Expect(result.message == "failed to inject key down, win32 error=5", "key down error should match");
}

void TestKeyUpDispatchSuccess() {
    FakeInputSink sink;
    const auto result = shared_km::service::DispatchInputEvent(
        {
            .type = shared_km::protocol::InputEventType::KeyUp,
            .key_event = {.virtual_key_code = 0x42, .scan_code = 0x30, .flags = 1},
        },
        sink
    );

    Expect(result.ok, "key up dispatch should succeed");
    Expect(sink.key_up_called, "key up handler should be called");
    Expect(sink.last_vk == 0x42, "key up vk should match");
    Expect(sink.last_key_flags == 1, "key up flags should match");
    Expect(result.message == "key up event received: vk=66 scan=48", "key up log should match");
}

void TestMouseWheelDispatchSuccess() {
    FakeInputSink sink;
    const auto result = shared_km::service::DispatchInputEvent(
        {
            .type = shared_km::protocol::InputEventType::MouseWheel,
            .wheel_delta = 120,
        },
        sink
    );

    Expect(result.ok, "mouse wheel dispatch should succeed");
    Expect(sink.wheel_called, "mouse wheel handler should be called");
    Expect(sink.last_wheel_delta == 120, "mouse wheel delta should match");
    Expect(result.message == "mouse wheel event received: delta=120", "mouse wheel log should match");
}

void TestMouseWheelDispatchFailure() {
    FakeInputSink sink;
    sink.wheel_result = false;
    sink.last_error = 5;

    const auto result = shared_km::service::DispatchInputEvent(
        {
            .type = shared_km::protocol::InputEventType::MouseWheel,
            .wheel_delta = -120,
        },
        sink
    );

    Expect(!result.ok, "mouse wheel dispatch should fail");
    Expect(sink.wheel_called, "mouse wheel handler should be called on failure");
    Expect(result.message == "failed to inject mouse wheel, win32 error=5", "mouse wheel error should match");
}

int main() {
    TestMouseMoveDispatchSuccess();
    TestEdgeEnterDispatchSuccess();
    TestEdgeLeaveDispatchSuccess();
    TestMouseMoveRelativeDispatchSuccess();
    TestLeftButtonDownDispatchSuccess();
    TestLeftButtonUpDispatchSuccess();
    TestLeftButtonUpDispatchFailure();
    TestKeyDownDispatchSuccess();
    TestKeyDownDispatchFailure();
    TestKeyUpDispatchSuccess();
    TestMouseWheelDispatchSuccess();
    TestMouseWheelDispatchFailure();
    return 0;
}
