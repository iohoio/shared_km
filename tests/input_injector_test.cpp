#include <stdexcept>
#include <string>

#include "shared_km/input/input_injector.hpp"

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool FakeGetCursorPosSuccess(shared_km::input::NativePoint& point) {
    point.x = 321;
    point.y = 654;
    return true;
}

bool FakeGetCursorPosFailure(shared_km::input::NativePoint& point) {
    point.x = 999;
    point.y = 999;
    return false;
}

void TestGetCurrentMousePositionReturnsCoordinatesFromAdapter() {
    const auto position = shared_km::input::GetCurrentMousePosition(FakeGetCursorPosSuccess);
    Expect(position.x == 321, "mouse x should come from adapter");
    Expect(position.y == 654, "mouse y should come from adapter");
}

void TestGetCurrentMousePositionReturnsZeroOnFailure() {
    const auto position = shared_km::input::GetCurrentMousePosition(FakeGetCursorPosFailure);
    Expect(position.x == 0, "mouse x should be zero when adapter fails");
    Expect(position.y == 0, "mouse y should be zero when adapter fails");
}

void TestGetCurrentMousePositionReturnsZeroOnNullAdapter() {
    const auto position = shared_km::input::GetCurrentMousePosition(nullptr);
    Expect(position.x == 0, "mouse x should be zero when adapter is null");
    Expect(position.y == 0, "mouse y should be zero when adapter is null");
}

}  // namespace

int main() {
    TestGetCurrentMousePositionReturnsCoordinatesFromAdapter();
    TestGetCurrentMousePositionReturnsZeroOnFailure();
    TestGetCurrentMousePositionReturnsZeroOnNullAdapter();
    return 0;
}
