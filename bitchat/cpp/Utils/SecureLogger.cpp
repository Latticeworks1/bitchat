#include "SecureLogger.h"
#include "Arduino.h"

void SecureLogger::log(const std::string& message, LogLevel level) {
    // In a real implementation, we might encrypt the logs or write them to a secure storage.
    // For now, we'll just print to the serial console.
    String levelStr;
    switch (level) {
        case LogLevel::DEBUG:
            levelStr = "DEBUG";
            break;
        case LogLevel::INFO:
            levelStr = "INFO";
            break;
        case LogLevel::WARNING:
            levelStr = "WARNING";
            break;
        case LogLevel::ERROR:
            levelStr = "ERROR";
            break;
    }
    Serial.printf("[%s] %s\n", levelStr.c_str(), message.c_str());
}
