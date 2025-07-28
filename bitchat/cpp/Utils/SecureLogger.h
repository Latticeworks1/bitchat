#pragma once

#include <string>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class SecureLogger {
public:
    static void log(const std::string& message, LogLevel level = LogLevel::INFO);
};
