#pragma once

#include "Arduino.h"
#include "BitchatProtocol.h"
#include "Identity.h"
#include "DeliveryTracker.h"
#include <vector>
#include <string>
#include <map>

class ChatController {
public:
    static ChatController& getInstance();
    void begin();
    void loop();
    void sendMessage(const std::string& content);
    void startPrivateChat(const std::string& peerID);
    void endPrivateChat();
    const std::vector<BitchatMessage>& getMessages() const;
    const std::map<std::string, std::vector<BitchatMessage>>& getPrivateChats() const;
    const std::string& getNickname() const;
    void setNickname(const std::string& nickname);
    const std::vector<std::string>& getConnectedPeers() const;
    const std::string& getSelectedPrivateChatPeer() const;

private:
    ChatController();
    std::vector<BitchatMessage> messages;
    std::map<std::string, std::vector<BitchatMessage>> privateChats;
    std::string nickname;
    std::vector<std::string> connectedPeers;
    std::string selectedPrivateChatPeer;
    // In a real implementation, we would have a BluetoothMeshService class
    // that handles the BLE communication.
    // For now, we'll just simulate it.
    void simulateIncomingMessage();
};
