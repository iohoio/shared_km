#include "shared_km/network/socket_runtime.hpp"

#include <stdexcept>

#include <winsock2.h>

namespace shared_km::network {

SocketRuntime::SocketRuntime() {
    WSADATA data{};
    const auto result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
}

SocketRuntime::~SocketRuntime() {
    WSACleanup();
}

}  // namespace shared_km::network
