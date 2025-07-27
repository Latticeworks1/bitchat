#pragma once

#include "Arduino.h"
#include "NoiseProtocol.h"
#include <string>
#include <map>
#include <vector>

enum class HandshakeState {
    IDLE,
    WAITING_TO_INITIATE,
    INITIATING,
    RESPONDING,
    WAITING_FOR_RESPONSE,
    ESTABLISHED,
    FAILED
};

class NoiseHandshakeCoordinator {
public:
    NoiseHandshakeCoordinator();
    NoiseRole determineHandshakeRole(const std::string& myPeerID, const std::string& remotePeerID);
    bool shouldInitiateHandshake(const std::string& myPeerID, const std::string& remotePeerID, bool forceIfStale = false);
    void recordHandshakeInitiation(const std::string& peerID);
    void recordHandshakeResponse(const std::string& peerID);
    void recordHandshakeSuccess(const std::string& peerID);
    void recordHandshakeFailure(const std::string& peerID, const std::string& reason);
    bool shouldAcceptHandshakeInitiation(const std::string& myPeerID, const std::string& remotePeerID);
    bool isDuplicateHandshakeMessage(const std::vector<uint8_t>& data);
    unsigned long getRetryDelay(const std::string& peerID);
    void resetHandshakeState(const std::string& peerID);
    std::vector<std::string> cleanupStaleHandshakes(unsigned long staleTimeout = 30000);
    HandshakeState getHandshakeState(const std::string& peerID);
    int getRetryCount(const std::string& peerID);
    void incrementRetryCount(const std::string& peerID);
    void clearAllHandshakeStates();

private:
    struct State {
        HandshakeState state;
        unsigned long timestamp;
        int attempt;
        bool canRetry;
        std::string reason;
    };
    std::map<std::string, State> handshakeStates;
    std::vector<std::vector<uint8_t>> processedHandshakeMessages;
    const int maxHandshakeAttempts = 3;
    const unsigned long handshakeTimeout = 10000;
    const unsigned long retryDelay = 2000;
    const unsigned long minTimeBetweenHandshakes = 1000;
    const int messageHistoryLimit = 100;

    int getCurrentAttempt(const std::string& peerID);
};
