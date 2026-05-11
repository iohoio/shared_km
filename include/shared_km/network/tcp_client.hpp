#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "shared_km/protocol/message.hpp"

namespace shared_km::network {

class TcpClient {
public:
    TcpClient();
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    bool Connect(const std::string& host, std::uint16_t port);
    bool Send(protocol::MessageKind kind, const std::vector<std::byte>& payload);
    bool Receive(protocol::ParsedHeader& header, std::vector<std::byte>& payload);
    void Close();

private:
    std::uintptr_t socket_ = static_cast<std::uintptr_t>(-1);
};

}  // namespace shared_km::network
