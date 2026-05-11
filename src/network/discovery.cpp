#include "shared_km/network/discovery.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")

namespace shared_km::network {

namespace {

constexpr std::uint32_t kMagic = 0x444D4B53; // "SKMD"
constexpr std::size_t kHeaderSize = sizeof(std::uint32_t) + sizeof(std::uint8_t);

enum class PacketKind : std::uint8_t {
    Request = 1,
    Response = 2,
};

bool SendTo(SOCKET s, sockaddr_in* dest,
            PacketKind kind,
            const std::string& device_name = {},
            std::uint16_t port = 0) {
    std::vector<std::byte> packet;
    auto magic = kMagic;
    packet.insert(packet.end(), reinterpret_cast<std::byte*>(&magic),
                  reinterpret_cast<std::byte*>(&magic + 1));
    auto kind_byte = static_cast<std::uint8_t>(kind);
    packet.push_back(static_cast<std::byte>(kind_byte));

    if (kind == PacketKind::Response) {
        uint16_t name_len = htons(static_cast<uint16_t>(device_name.size()));
        packet.insert(packet.end(), reinterpret_cast<std::byte*>(&name_len),
                      reinterpret_cast<std::byte*>(&name_len + 1));
        packet.insert(packet.end(),
                      reinterpret_cast<const std::byte*>(device_name.data()),
                      reinterpret_cast<const std::byte*>(device_name.data() + device_name.size()));
        uint16_t net_port = htons(port);
        packet.insert(packet.end(), reinterpret_cast<std::byte*>(&net_port),
                      reinterpret_cast<std::byte*>(&net_port + 1));
    }

    return sendto(s, reinterpret_cast<const char*>(packet.data()),
                  static_cast<int>(packet.size()), 0,
                  reinterpret_cast<sockaddr*>(dest), sizeof(*dest)) != SOCKET_ERROR;
}

} // namespace

// ----- GetLocalIP -----

std::string GetLocalIP() {
    ULONG bufsize = 0;
    GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &bufsize);

    std::vector<std::byte> buf(bufsize);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());

    if (GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &bufsize) != NO_ERROR) {
        return "127.0.0.1";
    }

    for (auto* a = adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            if (sa && sa->sin_family == AF_INET) {
                char ip[64];
                inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
                if (std::strcmp(ip, "127.0.0.1") != 0) {
                    return ip;
                }
            }
        }
    }
    return "127.0.0.1";
}

// ----- DiscoveryService -----

DiscoveryService::~DiscoveryService() {
    Stop();
}

bool DiscoveryService::CreateAndBind(std::uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        last_error_ = "socket() failed";
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    BOOL broadcast = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        last_error_ = "bind() failed";
        return false;
    }

    socket_ = static_cast<std::uintptr_t>(s);
    return true;
}

bool DiscoveryService::StartResponder(std::uint16_t discover_port,
                                       const std::string& device_name,
                                       std::uint16_t service_port) {
    Stop();

    if (!CreateAndBind(discover_port)) {
        return false;
    }

    SOCKET s = static_cast<SOCKET>(socket_);

    // Create a listener thread
    std::thread([s, device_name, service_port]() {
        std::vector<std::byte> buf(1024);
        while (true) {
            sockaddr_in from{};
            int from_len = sizeof(from);
            int received = recvfrom(s, reinterpret_cast<char*>(buf.data()),
                                    static_cast<int>(buf.size()), 0,
                                    reinterpret_cast<sockaddr*>(&from), &from_len);
            if (received == SOCKET_ERROR) {
                break; // socket closed or error
            }

            if (static_cast<std::size_t>(received) < kHeaderSize) continue;

            std::uint32_t magic = 0;
            std::memcpy(&magic, buf.data(), sizeof(magic));
            if (magic != kMagic) continue;

            auto kind = static_cast<PacketKind>(buf[kHeaderSize - 1]);
            if (kind != PacketKind::Request) continue;

            // Respond
            SendTo(s, &from, PacketKind::Response, device_name, service_port);
        }
    }).detach();

    return true;
}

bool DiscoveryService::Scan(std::uint16_t discover_port, PeerCallback callback,
                             int timeout_ms) {
    Stop();
    if (!CreateAndBind(0)) { // port 0 = system assigned
        return false;
    }

    SOCKET s = static_cast<SOCKET>(socket_);

    // Send broadcast request
    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(discover_port);
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    if (!SendTo(s, &broadcast_addr, PacketKind::Request)) {
        last_error_ = "sendto() failed";
        Stop();
        return false;
    }

    // Set receive timeout
    DWORD timeout = static_cast<DWORD>(timeout_ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    // Collect responses
    std::vector<std::byte> buf(1024);
    bool found_any = false;
    while (true) {
        sockaddr_in from{};
        int from_len = sizeof(from);
        int received = recvfrom(s, reinterpret_cast<char*>(buf.data()),
                                static_cast<int>(buf.size()), 0,
                                reinterpret_cast<sockaddr*>(&from), &from_len);
        if (received == SOCKET_ERROR) break;

        if (static_cast<std::size_t>(received) < kHeaderSize) continue;

        std::uint32_t magic = 0;
        std::memcpy(&magic, buf.data(), sizeof(magic));
        if (magic != kMagic) continue;

        auto kind = static_cast<PacketKind>(buf[kHeaderSize - 1]);
        if (kind != PacketKind::Response) continue;

        // Parse response: name_len(2) + name + port(2)
        std::size_t offset = kHeaderSize;
        if (offset + sizeof(uint16_t) > static_cast<std::size_t>(received)) continue;

        uint16_t name_len = 0;
        std::memcpy(&name_len, buf.data() + offset, sizeof(name_len));
        name_len = ntohs(name_len);
        offset += sizeof(name_len);

        if (offset + name_len + sizeof(uint16_t) > static_cast<std::size_t>(received)) continue;

        std::string device_name(reinterpret_cast<const char*>(buf.data() + offset), name_len);
        offset += name_len;

        uint16_t peer_port = 0;
        std::memcpy(&peer_port, buf.data() + offset, sizeof(peer_port));
        peer_port = ntohs(peer_port);

        char ip_str[64];
        inet_ntop(AF_INET, &from.sin_addr, ip_str, sizeof(ip_str));

        found_any = true;
        if (callback) {
            callback({ip_str, peer_port, device_name});
        }
    }

    Stop();
    return found_any;
}

void DiscoveryService::Stop() {
    if (socket_ != static_cast<std::uintptr_t>(-1)) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = static_cast<std::uintptr_t>(-1);
    }
}

} // namespace shared_km::network
