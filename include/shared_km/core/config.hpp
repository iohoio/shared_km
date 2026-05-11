#pragma once

#include <filesystem>
#include <string>

namespace shared_km::core {

struct RuntimeConfig {
    std::string token = "demo-token";
    std::string host = "0.0.0.0";
    std::uint16_t port = 8765;
    bool edge_switching_enabled = false;
    std::filesystem::path config_path;
};

RuntimeConfig LoadConfigOrDefaults(const std::filesystem::path& path);

}  // namespace shared_km::core
