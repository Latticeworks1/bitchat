#include "BluetoothMeshService.h"
#include "../BitchatProtocol.h"

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(BLEUUID(SERVICE_UUID))) {
            if (!((BluetoothMeshService*)BLEDevice::getServer()->getCallbacks())->isClientConnected(advertisedDevice.getAddress())) {
                advertisedDevice.getScan()->stop();
                BLEClient* pClient = BLEDevice::createClient();
                pClient->setCallbacks((BluetoothMeshService*)BLEDevice::getServer()->getCallbacks());
                pClient->connect(&advertisedDevice);
                ((BluetoothMeshService*)BLEDevice::getServer()->getCallbacks())->addClient(pClient);
            }
        }
    }
};

BluetoothMeshService::BluetoothMeshService() : receiveCallback(nullptr) {}

void BluetoothMeshService::begin(const std::string& deviceName) {
    this->deviceName = deviceName;
    BLEDevice::init(deviceName);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(this);
    BLEService *pService = pServer->createService(SERVICE_UUID);
    BLECharacteristic* pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE
    );
    pCharacteristic->setCallbacks(new BLECharacteristicCallbacks());
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pScan->setActiveScan(true);
    pScan->start(5, false);
}

void BluetoothMeshService::update() {
    if (!pScan->isScanning()) {
        pScan->start(5, false);
    }
}

void BluetoothMeshService::sendBroadcast(const std::string& message) {
    Bitchat::BitchatPacket* packet = (Bitchat::BitchatPacket*)message.c_str();
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
                    pRemoteCharacteristic->writeValue((uint8_t*)message.c_str(), message.length());
                }
            }
        }
    }
}

void BluetoothMeshService::setReceiveCallback(void (*callback)(const std::string& message, int rssi)) {
    receiveCallback = callback;
}

void BluetoothMeshService::onConnect(BLEServer* pServer) {
    Serial.println("Device connected");
}

void BluetoothMeshService::onDisconnect(BLEServer* pServer) {
    Serial.println("Device disconnected");
    BLEDevice::startAdvertising();
}

void BluetoothMeshService::onConnect(BLEClient* pClient) {
    Serial.println("Connected to server");
}

void BluetoothMeshService::onDisconnect(BLEClient* pClient) {
    Serial.println("Disconnected from server");
    clients.erase(std::remove(clients.begin(), clients.end(), pClient), clients.end());
}

bool BluetoothMeshService::isClientConnected(BLEAddress address) {
    for (auto client : clients) {
        if (client->getPeerAddress().equals(address)) {
            return true;
        }
    }
    return false;
}

void BluetoothMeshService::addClient(BLEClient* client) {
    clients.push_back(client);
}
