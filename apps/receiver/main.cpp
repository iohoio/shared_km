#include <exception>
#include <iostream>

#include "shared_km/service/receiver_app.hpp"

int main() {
    try {
        shared_km::service::ReceiverApp app;
        return app.Run();
    } catch (const std::exception& ex) {
        std::cerr << "[receiver] fatal error: " << ex.what() << '\n';
        return 1;
    }
}
