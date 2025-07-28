#pragma once

#include "Arduino.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <vector>
#include <string>

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

class BluetoothMeshService : public BLEServerCallbacks, public BLEClientCallbacks {
public:
    BluetoothMeshService();
    void begin(const std::string& deviceName);
    void update();
    void sendBroadcast(const std::string& message);
    void setReceiveCallback(void (*callback)(const std::string& message, int rssi));
    bool isClientConnected(BLEAddress address);
    void addClient(BLEClient* client);

private:
    void onConnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer) override;
    void onConnect(BLEClient* pClient) override;
    void onDisconnect(BLEClient* pClient) override;

    void (*receiveCallback)(const std::string& message, int rssi);
    std::string deviceName;
    BLEServer* pServer = NULL;
    BLEScan* pScan = NULL;
    std::vector<BLEClient*> clients;
    std::vector<uint64_t> seenPacketHashes;
};
