#pragma once

#include "Arduino.h"
#include "BitchatProtocol.h"
#include <vector>
#include <string>
#include <map>

class DeliveryTracker {
public:
    static DeliveryTracker& getInstance();
    void trackMessage(const BitchatMessage& message, const std::string& recipientID, const std::string& recipientNickname, bool isFavorite = false);
    void processDeliveryAck(const DeliveryAck& ack);
    DeliveryAck generateAck(const BitchatMessage& message, const std::string& myPeerID, const std::string& myNickname, uint8_t hopCount);
    void clearDeliveryStatus(const std::string& messageID);

private:
    DeliveryTracker();
    struct PendingDelivery {
        std::string messageID;
        unsigned long sentAt;
        std::string recipientID;
        std::string recipientNickname;
        int retryCount;
        bool isFavorite;
    };
    std::map<std::string, PendingDelivery> pendingDeliveries;
    std::vector<std::string> receivedAckIDs;
    std::vector<std::string> sentAckIDs;
    void handleTimeout(const std::string& messageID);
    void retryDelivery(const std::string& messageID);
    void cleanupOldDeliveries();
};

class MessageRetryService {
public:
    static MessageRetryService& getInstance();
    void addMessageForRetry(const std::string& content, const std::vector<std::string>& mentions, bool isPrivate, const std::string& recipientPeerID, const std::string& recipientNickname, const std::string& originalMessageID, unsigned long originalTimestamp);
    void processRetryQueue();
    void clearRetryQueue();

private:
    MessageRetryService();
    struct RetryableMessage {
        std::string id;
        std::string originalMessageID;
        unsigned long originalTimestamp;
        std::string content;
        std::vector<std::string> mentions;
        bool isPrivate;
        std::string recipientPeerID;
        std::string recipientNickname;
        int retryCount;
        unsigned long nextRetryTime;
    };
    std::vector<RetryableMessage> retryQueue;
};
