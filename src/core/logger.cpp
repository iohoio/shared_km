#include "shared_km/core/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace shared_km::core {

Logger& Logger::Instance() {
    static Logger logger;
    return logger;
}

void Logger::Info(const std::string& message) {
    Write("INFO", message);
}

void Logger::Warn(const std::string& message) {
    Write("WARN", message);
}

void Logger::Error(const std::string& message) {
    Write("ERROR", message);
}

void Logger::Write(const char* level, const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
    localtime_s(&local_time, &time);

    std::ostringstream buffer;
    buffer << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
           << " [" << level << "] "
           << message
           << '\n';

    std::scoped_lock lock(mutex_);
    std::cout << buffer.str() << std::flush;
}

}  // namespace shared_km::core
