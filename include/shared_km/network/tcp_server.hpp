#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <winsock2.h>

#include "shared_km/protocol/message.hpp"

namespace shared_km::network {

class TcpConnection {
public:
    explicit TcpConnection(std::uintptr_t socket);
    TcpConnection(std::uintptr_t socket, const sockaddr_in& client_addr, const std::byte* pending_data, std::size_t pending_size);
    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& other) noexcept;
    TcpConnection& operator=(TcpConnection&& other) noexcept;

    bool IsOpen() const;
    bool Send(protocol::MessageKind kind, const std::vector<std::byte>& payload);
    bool Receive(protocol::ParsedHeader& header, std::vector<std::byte>& payload);
    void Close();

private:
    bool ParseMessage(const std::vector<std::byte>& buf, protocol::ParsedHeader& header, std::vector<std::byte>& payload);

    std::uintptr_t socket_ = static_cast<std::uintptr_t>(-1);
    sockaddr_in client_addr_{};
    bool has_client_ = false;
    std::vector<std::byte> pending_;
};

class TcpServer {
public:
    TcpServer();
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool Listen(const std::string& host, std::uint16_t port);
    TcpConnection Accept();
    void Close();

private:
    std::uintptr_t listen_socket_ = static_cast<std::uintptr_t>(-1);
};

}  // namespace shared_km::network
