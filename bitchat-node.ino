#include "Arduino.h"
#include <vector>
#include <string>
#include <cstdint>

namespace Bitchat {

namespace Constants {
    constexpr uint8_t PROTOCOL_VERSION = 1;
    constexpr size_t SENDER_ID_SIZE = 8;
    constexpr size_t RECIPIENT_ID_SIZE = 8;
    constexpr size_t SIGNATURE_SIZE = 64;
}

enum class MessageType : uint8_t {
    ANNOUNCE = 0x01,
    LEAVE = 0x03,
    MESSAGE = 0x04,
    FRAGMENT_START = 0x05,
    FRAGMENT_CONTINUE = 0x06,
    FRAGMENT_END = 0x07,
    DELIVERY_ACK = 0x0A,
    DELIVERY_STATUS_REQUEST = 0x0B,
    READ_RECEIPT = 0x0C,
    NOISE_HANDSHAKE_INIT = 0x10,
    NOISE_HANDSHAKE_RESP = 0x11,
    NOISE_ENCRYPTED = 0x12,
    NOISE_IDENTITY_ANNOUNCE = 0x13,
    VERSION_HELLO = 0x20,
    VERSION_ACK = 0x21,
    PROTOCOL_ACK = 0x22,
    PROTOCOL_NACK = 0x23,
    SYSTEM_VALIDATION = 0x24
};

namespace Flags {
    constexpr uint8_t HAS_RECIPIENT = 0x01;
    constexpr uint8_t HAS_SIGNATURE = 0x02;
    constexpr uint8_t IS_COMPRESSED = 0x04;
}

struct BitchatPacket {
    uint8_t version;
    uint8_t type;
    uint8_t ttl;
    uint64_t timestamp;
    uint8_t flags;
    uint16_t payloadLength;
    uint8_t senderID[Constants::SENDER_ID_SIZE];
    uint8_t recipientID[Constants::RECIPIENT_ID_SIZE];
    uint8_t payload[2048]; // Max payload size
    uint8_t signature[Constants::SIGNATURE_SIZE];
} __attribute__((packed, aligned(4)));

} // namespace Bitchat


class BLEMeshService {
public:
    BLEMeshService();
    void begin(const std::string& deviceName);
    void update();
    void sendBroadcast(const std::string& message);
    void setReceiveCallback(void (*callback)(const std::string& message, int rssi));

private:
    void (*receiveCallback)(const std::string& message, int rssi);
    std::string deviceName;
};

BLEMeshService::BLEMeshService() : receiveCallback(nullptr) {}

void BLEMeshService::begin(const std::string& deviceName) {
    this->deviceName = deviceName;
    Serial.printf("BLE Mesh Service started with device name: %s\n", deviceName.c_str());
}

void BLEMeshService::update() {
    if (millis() % 15000 == 0) {
        if (receiveCallback) {
            Bitchat::BitchatPacket packet;
            packet.version = Bitchat::Constants::PROTOCOL_VERSION;
            packet.type = (uint8_t)Bitchat::MessageType::MESSAGE;
            packet.ttl = 10;
            packet.timestamp = millis();
            packet.flags = 0;
            packet.payloadLength = 12;
            memcpy(packet.senderID, "sender", 7);
            memcpy(packet.payload, "Hello World!", 12);

            std::string msg((char*)&packet, sizeof(packet));
            receiveCallback(msg, -50);
        }
    }
}

void BLEMeshService::sendBroadcast(const std::string& message) {
    Serial.printf("Broadcasting message of size %d\n", message.length());
}

void BLEMeshService::setReceiveCallback(void (*callback)(const std::string& message, int rssi)) {
    receiveCallback = callback;
}


BLEMeshService meshService;
unsigned long lastStatusReport = 0;
int forwardedMessages = 0;
int lastRssi = 0;

void handleSerialCommand(const std::string& command);

void receivedCallback(const std::string& msg, int rssi) {
  Serial.printf("Received packet of size %d (RSSI: %d)\n", msg.length(), rssi);
  meshService.sendBroadcast(msg);
  forwardedMessages++;
  lastRssi = rssi;
}

void setup() {
  Serial.begin(115200);
  meshService.begin("bitchat-relay");
  meshService.setReceiveCallback(receivedCallback);
  Serial.println("Bitchat Relay Node Started");
  Serial.println("Enter 'help' for a list of commands.");
}

void loop() {
  meshService.update();

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    handleSerialCommand(command.c_str());
  }

  if (millis() - lastStatusReport > 30000) {
    lastStatusReport = millis();
    Serial.printf("Status: %d messages forwarded, last RSSI: %d\n", forwardedMessages, lastRssi);
  }
}

void handleSerialCommand(const std::string& command) {
  if (command == "help") {
    Serial.println("Available commands:");
    Serial.println("  help - Show this help message");
    Serial.println("  status - Show the current status of the relay");
  } else if (command == "status") {
    Serial.printf("Status: %d messages forwarded, last RSSI: %d\n", forwardedMessages, lastRssi);
  } else {
    Serial.printf("Unknown command: %s\n", command.c_str());
  }
}
