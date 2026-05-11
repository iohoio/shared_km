#include "shared_km/network/file_transfer.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

namespace shared_km::network {

namespace {

constexpr auto kInvalidSocket = static_cast<std::uintptr_t>(INVALID_SOCKET);
constexpr std::size_t kChunkSize = 65536;  // 64 KB per write

// Write exactly count bytes from buf to the socket. Returns false on error.
bool SendAll(SOCKET sock, const char* data, std::size_t count) {
    while (count > 0) {
        int sent = send(sock, data, static_cast<int>(count), 0);
        if (sent <= 0) return false;
        data += sent;
        count -= static_cast<std::size_t>(sent);
    }
    return true;
}

// Read exactly count bytes from the socket into buf. Returns false on error/closed.
bool RecvAll(SOCKET sock, char* buf, std::size_t count) {
    while (count > 0) {
        int received = recv(sock, buf, static_cast<int>(count), 0);
        if (received <= 0) return false;
        buf += received;
        count -= static_cast<std::size_t>(received);
    }
    return true;
}

std::string ExtractFilename(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

}  // namespace

bool SendFileOverTcp(const std::string& host, std::uint16_t port,
                     const std::string& local_path,
                     FileTransferCallback callback) {
    // Open local file
    std::ifstream file(local_path, std::ios::binary | std::ios::ate);
    if (!file) {
        if (callback) {
            callback({false, ExtractFilename(local_path), 0, "cannot open local file"});
        }
        return false;
    }
    const auto file_size = static_cast<std::uint64_t>(file.tellg());
    file.seekg(0);

    // Resolve address
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const auto port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
        if (callback) {
            callback({false, ExtractFilename(local_path), file_size, "DNS resolution failed"});
        }
        return false;
    }

    SOCKET sock = INVALID_SOCKET;
    for (auto* current = result; current != nullptr; current = current->ai_next) {
        sock = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(result);

    if (sock == INVALID_SOCKET) {
        if (callback) {
            callback({false, ExtractFilename(local_path), file_size, "connection failed"});
        }
        return false;
    }

    // Build wire format: [filename_len:4][filename:N][file_size:8][data:file_size]
    const auto filename = ExtractFilename(local_path);
    const std::uint32_t name_len = static_cast<std::uint32_t>(filename.size());

    // Send filename length + filename + file size
    char header_buf[4 + 65536 + 8];  // large enough for any reasonable filename
    std::memcpy(header_buf, &name_len, 4);
    std::memcpy(header_buf + 4, filename.data(), name_len);
    std::memcpy(header_buf + 4 + name_len, &file_size, 8);

    bool ok = SendAll(sock, header_buf, 4 + name_len + 8);
    if (!ok) {
        closesocket(sock);
        if (callback) {
            callback({false, filename, file_size, "send metadata failed"});
        }
        return false;
    }

    // Send file data in chunks
    std::vector<char> chunk(kChunkSize);
    std::uint64_t remaining = file_size;
    while (ok && remaining > 0) {
        const auto to_read = (std::min)(static_cast<std::uint64_t>(chunk.size()), remaining);
        file.read(chunk.data(), static_cast<std::streamsize>(to_read));
        const auto read_bytes = static_cast<std::size_t>(file.gcount());
        if (read_bytes == 0) {
            ok = false;
            break;
        }
        ok = SendAll(sock, chunk.data(), read_bytes);
        remaining -= read_bytes;
    }

    closesocket(sock);

    if (callback) {
        if (ok) {
            callback({true, filename, file_size, "done"});
        } else {
            callback({false, filename, file_size, "send interrupted"});
        }
    }
    return ok;
}

// --- TcpFileServer ---

TcpFileServer::TcpFileServer() = default;

TcpFileServer::~TcpFileServer() {
    Stop();
}

bool TcpFileServer::Start(const std::string& host, std::uint16_t port,
                           const std::string& save_dir,
                           FileTransferCallback on_complete) {
    Stop();
    save_dir_ = save_dir;
    on_complete_ = std::move(on_complete);

    // Ensure save directory exists
    try {
        std::filesystem::create_directories(save_dir_);
    } catch (...) {
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* result = nullptr;
    const auto port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
        return false;
    }

    SOCKET server = INVALID_SOCKET;
    for (auto* current = result; current != nullptr; current = current->ai_next) {
        server = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (server == INVALID_SOCKET) continue;

        constexpr BOOL reuse = TRUE;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        if (bind(server, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
            if (listen(server, SOMAXCONN) == 0) {
                listen_socket_ = static_cast<std::uintptr_t>(server);
                freeaddrinfo(result);
                return true;
            }
        }
        closesocket(server);
    }

    freeaddrinfo(result);
    return false;
}

void TcpFileServer::Stop() {
    if (listen_socket_ != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(listen_socket_));
        listen_socket_ = kInvalidSocket;
    }
}

// The AcceptFile function is called from the receiver thread to handle one
// incoming file transfer. Returns true if a file was received.
static bool AcceptFile(SOCKET server_sock, const std::string& save_dir,
                        FileTransferCallback on_complete) {
    sockaddr_in client_addr{};
    int addr_len = sizeof(client_addr);
    SOCKET client = accept(server_sock, reinterpret_cast<SOCKADDR*>(&client_addr), &addr_len);
    if (client == INVALID_SOCKET) return false;

    // Read wire format: name_len(4) + name(name_len) + file_size(8) + data
    std::uint32_t name_len = 0;
    if (!RecvAll(client, reinterpret_cast<char*>(&name_len), 4)) {
        closesocket(client);
        return true;  // client disconnected — not an error
    }

    if (name_len > 65536) {
        closesocket(client);
        if (on_complete) {
            on_complete({false, "(invalid)", 0, "filename too long"});
        }
        return true;
    }

    std::string filename(name_len, '\0');
    if (!RecvAll(client, filename.data(), name_len)) {
        closesocket(client);
        return true;
    }

    std::uint64_t file_size = 0;
    if (!RecvAll(client, reinterpret_cast<char*>(&file_size), 8)) {
        closesocket(client);
        if (on_complete) {
            on_complete({false, filename, 0, "receive metadata failed"});
        }
        return true;
    }

    // Avoid path traversal
    auto safe_name = ExtractFilename(filename);
    auto out_path = std::filesystem::path(save_dir) / safe_name;

    // Receive file data and write to disk
    std::ofstream out_file(out_path, std::ios::binary);
    if (!out_file) {
        closesocket(client);
        if (on_complete) {
            on_complete({false, safe_name, file_size, "cannot create output file"});
        }
        return true;
    }

    std::vector<char> buf(65536);
    std::uint64_t remaining = file_size;
    bool ok = true;
    while (ok && remaining > 0) {
        const auto to_read = (std::min)(static_cast<std::uint64_t>(buf.size()), remaining);
        if (!RecvAll(client, buf.data(), to_read)) {
            ok = false;
            break;
        }
        out_file.write(buf.data(), static_cast<std::streamsize>(to_read));
        remaining -= to_read;
    }

    closesocket(client);
    out_file.close();

    if (on_complete) {
        if (ok) {
            on_complete({true, safe_name, file_size, "done"});
        } else {
            on_complete({false, safe_name, file_size, "receive interrupted"});
            // Delete partial file
            std::error_code ec;
            std::filesystem::remove(out_path, ec);
        }
    }
    return true;
}

void TcpFileServer::RunAcceptLoop() {
    const auto sock = static_cast<SOCKET>(listen_socket_);
    if (sock == INVALID_SOCKET) return;

    while (true) {
        sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);
        SOCKET client = accept(sock, reinterpret_cast<SOCKADDR*>(&client_addr), &addr_len);
        if (client == INVALID_SOCKET) break;  // socket closed or error

        // AcceptFile always closes the client socket
        if (!AcceptFile(client, save_dir_, on_complete_))
            break;  // fatal error
    }
}

}  // namespace shared_km::network
