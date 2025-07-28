#include "Arduino.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <vector>

// =================================================================
// Bitchat Protocol Definitions
// =================================================================

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

// =================================================================
// BLE Mesh Service
// =================================================================

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

class BLEMeshService; // Forward declaration

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice);
};

class BLEMeshService : public BLEServerCallbacks, public BLEClientCallbacks {
public:
    BLEMeshService();
    void begin(const char* deviceName);
    void update();
    void sendBroadcast(const char* message, size_t length);
    void setReceiveCallback(void (*callback)(const char* message, size_t length, int rssi));
    bool isClientConnected(BLEAddress address);
    void addClient(BLEClient* client);

private:
    void onConnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer) override;
    void onConnect(BLEClient* pClient) override;
    void onDisconnect(BLEClient* pClient) override;

    void (*receiveCallback)(const char* message, size_t length, int rssi);
    const char* deviceName;
    BLEServer* pServer = NULL;
    BLEScan* pScan = NULL;
    std::vector<BLEClient*> clients;
    std::vector<uint64_t> seenPacketHashes;
};

MyAdvertisedDeviceCallbacks* advertisedDeviceCallbacks;
BLEMeshService* meshServiceInstance;

void MyAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(BLEUUID(SERVICE_UUID))) {
        if (!meshServiceInstance->isClientConnected(advertisedDevice.getAddress())) {
            advertisedDevice.getScan()->stop();
            BLEClient* pClient = BLEDevice::createClient();
            pClient->setCallbacks(meshServiceInstance);
            pClient->connect(&advertisedDevice);
            meshServiceInstance->addClient(pClient);
        }
    }
}


BLEMeshService::BLEMeshService() : receiveCallback(nullptr) {
    meshServiceInstance = this;
}

void BLEMeshService::begin(const char* deviceName) {
    this->deviceName = deviceName;
    BLEDevice::init(deviceName);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(this);
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE
    );
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    pScan = BLEDevice::getScan();
    advertisedDeviceCallbacks = new MyAdvertisedDeviceCallbacks();
    pScan->setAdvertisedDeviceCallbacks(advertisedDeviceCallbacks);
    pScan->setActiveScan(true);
    pScan->start(5, false);
}

void BLEMeshService::update() {
    if (!pScan->isScanning()) {
        pScan->start(5, false);
    }
}

void BLEMeshService::sendBroadcast(const char* message, size_t length) {
    Bitchat::BitchatPacket* packet = (Bitchat::BitchatPacket*)message;
    uint64_t hash = 0;
    for(size_t i = 0; i < sizeof(packet->senderID); ++i) hash = (hash << 8) | packet->senderID[i];
    for(size_t i = 0; i < sizeof(packet->timestamp); ++i) hash = (hash << 8) | ((uint8_t*)&packet->timestamp)[i];

    for(auto seenHash : seenPacketHashes) {
        if (seenHash == hash) return;
    }
    seenPacketHashes.push_back(hash);
    if (seenPacketHashes.size() > 100) {
        seenPacketHashes.erase(seenPacketHashes.begin());
    }

    for (auto client : clients) {
        if (client->isConnected()) {
            BLERemoteService* pRemoteService = client->getService(SERVICE_UUID);
            if (pRemoteService != nullptr) {
                BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
                if (pRemoteCharacteristic != nullptr) {
                    pRemoteCharacteristic->writeValue((uint8_t*)message, length);
                }
            }
        }
    }
}

void BLEMeshService::setReceiveCallback(void (*callback)(const char* message, size_t length, int rssi)) {
    receiveCallback = callback;
}

void BLEMeshService::onConnect(BLEServer* pServer) {
    Serial.println("Device connected");
}

void BLEMeshService::onDisconnect(BLEServer* pServer) {
    Serial.println("Device disconnected");
    BLEDevice::startAdvertising();
}

void BLEMeshService::onConnect(BLEClient* pClient) {
    Serial.println("Connected to server");
}

void BLEMeshService::onDisconnect(BLEClient* pClient) {
    Serial.println("Disconnected from server");
    clients.erase(std::remove(clients.begin(), clients.end(), pClient), clients.end());
}

bool BLEMeshService::isClientConnected(BLEAddress address) {
    for (auto client : clients) {
        if (client->getPeerAddress().equals(address)) {
            return true;
        }
    }
    return false;
}

void BLEMeshService::addClient(BLEClient* client) {
    clients.push_back(client);
}


// =================================================================
// Main Application Logic
// =================================================================

BLEMeshService meshService;
unsigned long lastStatusReport = 0;
int forwardedMessages = 0;
int lastRssi = 0;

void handleSerialCommand(String command);

void receivedCallback(const char* msg, size_t length, int rssi) {
  Serial.printf("Received packet of size %d (RSSI: %d)\n", length, rssi);
  meshService.sendBroadcast(msg, length);
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
    handleSerialCommand(command);
  }

  if (millis() - lastStatusReport > 30000) {
    lastStatusReport = millis();
    Serial.printf("Status: %d messages forwarded, last RSSI: %d\n", forwardedMessages, lastRssi);
  }
}

void handleSerialCommand(String command) {
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
