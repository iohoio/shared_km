#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace shared_km::network {

struct FileTransferResult {
    bool success;
    std::string filename;
    std::uint64_t size;
    std::string message;  // "done" or error description
};

using FileTransferCallback = std::function<void(const FileTransferResult&)>;

// Sender: connect to receiver over TCP and send a single file.
// Blocks until transfer completes or fails.
bool SendFileOverTcp(const std::string& host, std::uint16_t port,
                     const std::string& local_path,
                     FileTransferCallback callback);

// Receiver: TCP server that accepts incoming file transfers and saves them
// to a specified directory.
class TcpFileServer {
public:
    TcpFileServer();
    ~TcpFileServer();

    TcpFileServer(const TcpFileServer&) = delete;
    TcpFileServer& operator=(const TcpFileServer&) = delete;

    bool Start(const std::string& host, std::uint16_t port,
               const std::string& save_dir,
               FileTransferCallback on_complete);
    void Stop();

    // Blocking accept loop. Call on a dedicated thread. Returns when
    // Stop() is called (accept fails) or on fatal error.
    void RunAcceptLoop();

    bool IsRunning() const { return listen_socket_ != static_cast<std::uintptr_t>(-1); }
    const std::string& SaveDir() const { return save_dir_; }

private:
    std::uintptr_t listen_socket_ = static_cast<std::uintptr_t>(-1);
    std::string save_dir_;
    FileTransferCallback on_complete_;
};

}  // namespace shared_km::network
