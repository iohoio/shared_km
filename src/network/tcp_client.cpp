#include "shared_km/network/tcp_client.hpp"

#include <array>
#include <cstring>

#include <winsock2.h>
#include <ws2tcpip.h>

namespace shared_km::network {

namespace {

constexpr auto kInvalidSocket = static_cast<std::uintptr_t>(INVALID_SOCKET);

}  // namespace

TcpClient::TcpClient() = default;

TcpClient::~TcpClient() {
    Close();
}

bool TcpClient::Connect(const std::string& host, std::uint16_t port) {
    Close();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result = nullptr;
    const auto port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
        return false;
    }

    for (auto* current = result; current != nullptr; current = current->ai_next) {
        const auto candidate = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (candidate == INVALID_SOCKET) {
            continue;
        }

        // connect() on a DGRAM socket sets the default destination for send()/recv()
        if (connect(candidate, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
            socket_ = static_cast<std::uintptr_t>(candidate);
            freeaddrinfo(result);
            return true;
        }

        closesocket(candidate);
    }

    freeaddrinfo(result);
    return false;
}

bool TcpClient::Send(protocol::MessageKind kind, const std::vector<std::byte>& payload) {
    if (socket_ == kInvalidSocket) {
        return false;
    }

    const auto header = protocol::SerializeHeader(kind, static_cast<std::uint32_t>(payload.size()));
    std::vector<std::byte> msg(header.size() + payload.size());
    std::memcpy(msg.data(), header.data(), header.size());
    if (!payload.empty()) {
        std::memcpy(msg.data() + header.size(), payload.data(), payload.size());
    }

    const auto sock = static_cast<SOCKET>(socket_);
    const int sent = send(sock, reinterpret_cast<const char*>(msg.data()),
                          static_cast<int>(msg.size()), 0);
    return sent == static_cast<int>(msg.size());
}

bool TcpClient::Receive(protocol::ParsedHeader& header, std::vector<std::byte>& payload) {
    if (socket_ == kInvalidSocket) {
        return false;
    }

    std::array<std::byte, 65536> buf;
    const auto sock = static_cast<SOCKET>(socket_);
    const int received = recv(sock, reinterpret_cast<char*>(buf.data()),
                              static_cast<int>(buf.size()), 0);
    if (received <= 0) {
        return false;
    }

    if (static_cast<std::size_t>(received) < protocol::HeaderSize()) {
        return false;
    }

    protocol::HeaderBytes header_bytes;
    std::memcpy(header_bytes.data(), buf.data(), header_bytes.size());

    const auto parsed = protocol::TryParseHeader(header_bytes);
    if (!parsed) {
        return false;
    }

    const std::size_t expected = protocol::HeaderSize() + parsed->header.payload_size;
    if (static_cast<std::size_t>(received) < expected) {
        return false;
    }

    payload.assign(buf.begin() + protocol::HeaderSize(),
                   buf.begin() + protocol::HeaderSize() + parsed->header.payload_size);
    header = *parsed;
    return true;
}

void TcpClient::Close() {
    if (socket_ != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = kInvalidSocket;
    }
}

}  // namespace shared_km::network
