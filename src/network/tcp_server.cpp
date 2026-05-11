#include "shared_km/network/tcp_server.hpp"

#include <array>
#include <cstring>
#include <utility>

#include <winsock2.h>
#include <ws2tcpip.h>

namespace shared_km::network {

namespace {

constexpr auto kInvalidSocket = static_cast<std::uintptr_t>(INVALID_SOCKET);

}  // namespace

TcpConnection::TcpConnection(std::uintptr_t socket) : socket_(socket) {
}

TcpConnection::TcpConnection(std::uintptr_t socket, const sockaddr_in& client_addr,
                             const std::byte* pending_data, std::size_t pending_size)
    : socket_(socket), has_client_(true) {
    client_addr_ = client_addr;
    if (pending_data && pending_size > 0) {
        pending_.assign(pending_data, pending_data + pending_size);
    }
}

TcpConnection::~TcpConnection() {
    Close();
}

TcpConnection::TcpConnection(TcpConnection&& other) noexcept
    : socket_(other.socket_),
      client_addr_(other.client_addr_),
      has_client_(other.has_client_),
      pending_(std::move(other.pending_)) {
    other.socket_ = kInvalidSocket;
    other.has_client_ = false;
}

TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept {
    if (this != &other) {
        Close();
        socket_ = other.socket_;
        client_addr_ = other.client_addr_;
        has_client_ = other.has_client_;
        pending_ = std::move(other.pending_);
        other.socket_ = kInvalidSocket;
        other.has_client_ = false;
    }
    return *this;
}

bool TcpConnection::IsOpen() const {
    return socket_ != kInvalidSocket;
}

bool TcpConnection::Send(protocol::MessageKind kind, const std::vector<std::byte>& payload) {
    if (!IsOpen() || !has_client_) {
        return false;
    }

    const auto header = protocol::SerializeHeader(kind, static_cast<std::uint32_t>(payload.size()));
    std::vector<std::byte> msg(header.size() + payload.size());
    std::memcpy(msg.data(), header.data(), header.size());
    if (!payload.empty()) {
        std::memcpy(msg.data() + header.size(), payload.data(), payload.size());
    }

    const auto sock = static_cast<SOCKET>(socket_);
    const int sent = sendto(sock, reinterpret_cast<const char*>(msg.data()),
                            static_cast<int>(msg.size()), 0,
                            reinterpret_cast<const SOCKADDR*>(&client_addr_),
                            sizeof(client_addr_));
    return sent == static_cast<int>(msg.size());
}

bool TcpConnection::Receive(protocol::ParsedHeader& header, std::vector<std::byte>& payload) {
    if (!IsOpen()) {
        return false;
    }

    // Serve from pending buffer first (data already received during Accept)
    if (!pending_.empty()) {
        auto buf = std::move(pending_);
        pending_.clear();
        return ParseMessage(buf, header, payload);
    }

    // Receive a UDP datagram
    std::array<std::byte, 65536> buf;
    const auto sock = static_cast<SOCKET>(socket_);
    sockaddr_in from_addr{};
    int from_len = sizeof(from_addr);
    const int received = recvfrom(sock, reinterpret_cast<char*>(buf.data()),
                                  static_cast<int>(buf.size()), 0,
                                  reinterpret_cast<SOCKADDR*>(&from_addr), &from_len);
    if (received <= 0) {
        return false;
    }

    // If we have a connected client, ignore datagrams from other senders
    if (has_client_) {
        if (from_addr.sin_addr.s_addr != client_addr_.sin_addr.s_addr ||
            from_addr.sin_port != client_addr_.sin_port) {
            return false;
        }
    }

    return ParseMessage({buf.data(), buf.data() + received}, header, payload);
}

void TcpConnection::Close() {
    socket_ = kInvalidSocket;
    has_client_ = false;
    pending_.clear();
}

bool TcpConnection::ParseMessage(const std::vector<std::byte>& buf,
                                  protocol::ParsedHeader& header,
                                  std::vector<std::byte>& payload) {
    if (buf.size() < protocol::HeaderSize()) {
        return false;
    }

    protocol::HeaderBytes hbytes;
    std::memcpy(hbytes.data(), buf.data(), hbytes.size());

    const auto parsed = protocol::TryParseHeader(hbytes);
    if (!parsed) {
        return false;
    }

    if (buf.size() < protocol::HeaderSize() + parsed->header.payload_size) {
        return false;
    }

    payload.assign(buf.begin() + protocol::HeaderSize(),
                   buf.begin() + protocol::HeaderSize() + parsed->header.payload_size);
    header = *parsed;
    return true;
}

// --- TcpServer (UDP implementation) ---

TcpServer::TcpServer() = default;

TcpServer::~TcpServer() {
    Close();
}

bool TcpServer::Listen(const std::string& host, std::uint16_t port) {
    Close();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* result = nullptr;
    const auto port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
        return false;
    }

    bool success = false;
    for (auto* current = result; current != nullptr; current = current->ai_next) {
        const auto candidate = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (candidate == INVALID_SOCKET) {
            continue;
        }

        constexpr BOOL reuse_addr = TRUE;
        setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse_addr), sizeof(reuse_addr));

        if (bind(candidate, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
            listen_socket_ = static_cast<std::uintptr_t>(candidate);
            success = true;
            break;
        }

        closesocket(candidate);
    }

    freeaddrinfo(result);
    return success;
}

TcpConnection TcpServer::Accept() {
    if (listen_socket_ == kInvalidSocket) {
        return TcpConnection{kInvalidSocket};
    }

    const auto sock = static_cast<SOCKET>(listen_socket_);
    sockaddr_in client_addr{};
    int addr_len = sizeof(client_addr);
    std::array<std::byte, 65536> buf;

    const int received = recvfrom(sock, reinterpret_cast<char*>(buf.data()),
                                  static_cast<int>(buf.size()), 0,
                                  reinterpret_cast<SOCKADDR*>(&client_addr), &addr_len);
    if (received <= 0) {
        return TcpConnection{kInvalidSocket};
    }

    return TcpConnection{static_cast<std::uintptr_t>(sock), client_addr,
                         buf.data(), static_cast<std::size_t>(received)};
}

void TcpServer::Close() {
    if (listen_socket_ != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(listen_socket_));
        listen_socket_ = kInvalidSocket;
    }
}

}  // namespace shared_km::network
