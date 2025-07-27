#include "ChatController.h"

ChatController& ChatController::getInstance() {
    static ChatController instance;
    return instance;
}

ChatController::ChatController() {
    // Load nickname from EEPROM or some persistent storage
    nickname = "anon" + String(random(1000, 9999));
}

void ChatController::begin() {
    // Initialize services
    DeliveryTracker::getInstance();
    SecureIdentityStateManager::getInstance();
}

void ChatController::loop() {
    // Simulate incoming messages for testing
    if (millis() % 10000 == 0) {
        simulateIncomingMessage();
    }
}

void ChatController::sendMessage(const std::string& content) {
    if (content.empty()) return;

    BitchatMessage msg;
    msg.id = "some_random_uuid"; // Should be generated
    msg.sender = nickname;
    msg.content = content;
    msg.timestamp = millis();
    msg.isRelay = false;
    msg.isPrivate = !selectedPrivateChatPeer.empty();
    msg.recipientNickname = selectedPrivateChatPeer;

    if (msg.isPrivate) {
        privateChats[selectedPrivateChatPeer].push_back(msg);
    } else {
        messages.push_back(msg);
    }
    // In a real implementation, we would send the message over BLE here.
}

void ChatController::startPrivateChat(const std::string& peerID) {
    selectedPrivateChatPeer = peerID;
}

void ChatController::endPrivateChat() {
    selectedPrivateChatPeer = "";
}

const std::vector<BitchatMessage>& ChatController::getMessages() const {
    return messages;
}

const std::map<std::string, std::vector<BitchatMessage>>& ChatController::getPrivateChats() const {
    return privateChats;
}

const std::string& ChatController::getNickname() const {
    return nickname;
}

void ChatController::setNickname(const std::string& newNickname) {
    nickname = newNickname;
    // Save nickname to persistent storage
}

const std::vector<std::string>& ChatController::getConnectedPeers() const {
    return connectedPeers;
}

const std::string& ChatController::getSelectedPrivateChatPeer() const {
    return selectedPrivateChatPeer;
}

void ChatController::simulateIncomingMessage() {
    BitchatMessage msg;
    msg.id = "some_random_uuid"; // Should be generated
    msg.sender = "peer" + String(random(1000, 9999));
    msg.content = "Hello from " + String(msg.sender.c_str());
    msg.timestamp = millis();
    msg.isRelay = false;
    msg.isPrivate = random(2) == 0;

    if (msg.isPrivate) {
        if (connectedPeers.empty()) {
            connectedPeers.push_back(msg.sender);
        }
        msg.recipientNickname = nickname;
        privateChats[msg.sender].push_back(msg);
    } else {
        messages.push_back(msg);
    }
}
