#include "DeliveryTracker.h"

DeliveryTracker& DeliveryTracker::getInstance() {
    static DeliveryTracker instance;
    return instance;
}

DeliveryTracker::DeliveryTracker() {
    // In a real Arduino environment, we would use a timer library
    // to call cleanupOldDeliveries() periodically.
}

void DeliveryTracker::trackMessage(const BitchatMessage& message, const std::string& recipientID, const std::string& recipientNickname, bool isFavorite) {
    if (!message.isPrivate) return;

    PendingDelivery delivery;
    delivery.messageID = message.id;
    delivery.sentAt = millis();
    delivery.recipientID = recipientID;
    delivery.recipientNickname = recipientNickname;
    delivery.retryCount = 0;
    delivery.isFavorite = isFavorite;
    pendingDeliveries[message.id] = delivery;
}

void DeliveryTracker::processDeliveryAck(const DeliveryAck& ack) {
    for (const auto& id : receivedAckIDs) {
        if (id == ack.ackID) return; // Duplicate
    }
    receivedAckIDs.push_back(ack.ackID);
    pendingDeliveries.erase(ack.originalMessageID);
}

DeliveryAck DeliveryTracker::generateAck(const BitchatMessage& message, const std::string& myPeerID, const std::string& myNickname, uint8_t hopCount) {
    DeliveryAck ack;
    ack.originalMessageID = message.id;
    ack.ackID = "some_random_uuid"; // Should be generated
    ack.recipientID = myPeerID;
    ack.recipientNickname = myNickname;
    ack.timestamp = millis();
    ack.hopCount = hopCount;
    return ack;
}

void DeliveryTracker::clearDeliveryStatus(const std::string& messageID) {
    pendingDeliveries.erase(messageID);
}

void DeliveryTracker::handleTimeout(const std::string& messageID) {
    auto it = pendingDeliveries.find(messageID);
    if (it == pendingDeliveries.end()) return;

    if (it->second.retryCount < 3 && it->second.isFavorite) {
        retryDelivery(messageID);
    } else {
        pendingDeliveries.erase(it);
    }
}

void DeliveryTracker::retryDelivery(const std::string& messageID) {
    auto it = pendingDeliveries.find(messageID);
    if (it == pendingDeliveries.end()) return;

    it->second.retryCount++;
    // In a real implementation, we would re-send the message here.
}

void DeliveryTracker::cleanupOldDeliveries() {
    unsigned long now = millis();
    for (auto it = pendingDeliveries.begin(); it != pendingDeliveries.end(); ) {
        if (now - it->second.sentAt > 3600000) { // 1 hour
            it = pendingDeliveries.erase(it);
        } else {
            ++it;
        }
    }
    if (receivedAckIDs.size() > 1000) {
        receivedAckIDs.clear();
    }
    if (sentAckIDs.size() > 1000) {
        sentAckIDs.clear();
    }
}


MessageRetryService& MessageRetryService::getInstance() {
    static MessageRetryService instance;
    return instance;
}

MessageRetryService::MessageRetryService() {
    // In a real Arduino environment, we would use a timer library
    // to call processRetryQueue() periodically.
}

void MessageRetryService::addMessageForRetry(const std::string& content, const std::vector<std::string>& mentions, bool isPrivate, const std::string& recipientPeerID, const std::string& recipientNickname, const std::string& originalMessageID, unsigned long originalTimestamp) {
    if (retryQueue.size() >= 50) return;

    for(const auto& msg : retryQueue) {
        if (msg.originalMessageID == originalMessageID) return;
    }

    RetryableMessage msg;
    msg.id = "some_random_uuid"; // Should be generated
    msg.originalMessageID = originalMessageID;
    msg.originalTimestamp = originalTimestamp;
    msg.content = content;
    msg.mentions = mentions;
    msg.isPrivate = isPrivate;
    msg.recipientPeerID = recipientPeerID;
    msg.recipientNickname = recipientNickname;
    msg.retryCount = 0;
    msg.nextRetryTime = millis() + 2000;
    retryQueue.push_back(msg);
}

void MessageRetryService::processRetryQueue() {
    unsigned long now = millis();
    std::vector<RetryableMessage> toRetry;
    std::vector<RetryableMessage> remaining;

    for (const auto& msg : retryQueue) {
        if (now >= msg.nextRetryTime) {
            toRetry.push_back(msg);
        } else {
            remaining.push_back(msg);
        }
    }
    retryQueue = remaining;

    for (auto& msg : toRetry) {
        if (msg.retryCount >= 3) continue;
        // In a real implementation, we would re-send the message here.
        msg.retryCount++;
        msg.nextRetryTime = millis() + 2000 * (msg.retryCount + 1);
        retryQueue.push_back(msg);
    }
}

void MessageRetryService::clearRetryQueue() {
    retryQueue.clear();
}
