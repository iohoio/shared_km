#pragma once

#include <mutex>
#include <string>

namespace shared_km::core {

class Logger {
public:
    static Logger& Instance();

    void Info(const std::string& message);
    void Warn(const std::string& message);
    void Error(const std::string& message);

private:
    Logger() = default;

    void Write(const char* level, const std::string& message);

    std::mutex mutex_;
};

}  // namespace shared_km::core
