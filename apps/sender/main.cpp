#include <exception>
#include <iostream>

#include "shared_km/service/sender_app.hpp"

int main() {
    try {
        shared_km::service::SenderApp app;
        return app.Run();
    } catch (const std::exception& ex) {
        std::cerr << "[sender] fatal error: " << ex.what() << '\n';
        return 1;
    }
}
