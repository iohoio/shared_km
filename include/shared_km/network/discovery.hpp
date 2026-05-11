#pragma once

#include <functional>
#include <string>
#include <vector>

namespace shared_km::network {

struct DiscoveredPeer {
    std::string ip;
    std::uint16_t port;
    std::string device_name;
};

// Discovery service that can both respond to discovery requests (server/responder role)
// and broadcast discovery requests (client role).
class DiscoveryService final {
public:
    using PeerCallback = std::function<void(const DiscoveredPeer&)>;

    DiscoveryService() = default;
    ~DiscoveryService();

    DiscoveryService(const DiscoveryService&) = delete;
    DiscoveryService& operator=(const DiscoveryService&) = delete;

    // Start responder: listens for discovery requests on UDP port and responds
    bool StartResponder(std::uint16_t discover_port,
                        const std::string& device_name,
                        std::uint16_t service_port);

    // Scan: broadcast a discovery request and collect responses (blocking, up to timeout_ms)
    bool Scan(std::uint16_t discover_port, PeerCallback callback,
              int timeout_ms = 2000);

    void Stop();

    bool IsRunning() const { return socket_ != static_cast<std::uintptr_t>(-1); }
    std::string LastError() const { return last_error_; }

private:
    bool CreateAndBind(std::uint16_t port);

    std::uintptr_t socket_ = static_cast<std::uintptr_t>(-1);
    std::string last_error_;
};

// Helper: get the first non-loopback IPv4 address of this machine
std::string GetLocalIP();

} // namespace shared_km::network
