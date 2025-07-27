#include "NoiseHandshakeCoordinator.h"

NoiseHandshakeCoordinator::NoiseHandshakeCoordinator() {}

NoiseRole NoiseHandshakeCoordinator::determineHandshakeRole(const std::string& myPeerID, const std::string& remotePeerID) {
    return myPeerID < remotePeerID ? NoiseRole::INITIATOR : NoiseRole::RESPONDER;
}

bool NoiseHandshakeCoordinator::shouldInitiateHandshake(const std::string& myPeerID, const std::string& remotePeerID, bool forceIfStale) {
    auto it = handshakeStates.find(remotePeerID);
    if (it != handshakeStates.end()) {
        const auto& state = it->second;
        if (state.state != HandshakeState::IDLE && state.state != HandshakeState::ESTABLISHED && state.state != HandshakeState::FAILED) {
            if (forceIfStale) {
                if (state.state == HandshakeState::INITIATING && (millis() - state.timestamp > handshakeTimeout)) {
                    return true;
                }
            }
            return false;
        }
    }

    if (determineHandshakeRole(myPeerID, remotePeerID) != NoiseRole::INITIATOR) {
        return false;
    }

    if (it != handshakeStates.end()) {
        const auto& state = it->second;
        if (state.state == HandshakeState::FAILED) {
            if (!state.canRetry) {
                return false;
            }
            if (millis() - state.timestamp < retryDelay) {
                return false;
            }
        }
    }

    return true;
}

void NoiseHandshakeCoordinator::recordHandshakeInitiation(const std::string& peerID) {
    int attempt = getCurrentAttempt(peerID) + 1;
    handshakeStates[peerID] = {HandshakeState::INITIATING, millis(), attempt, false, ""};
}

void NoiseHandshakeCoordinator::recordHandshakeResponse(const std::string& peerID) {
    handshakeStates[peerID] = {HandshakeState::RESPONDING, millis(), 0, false, ""};
}

void NoiseHandshakeCoordinator::recordHandshakeSuccess(const std::string& peerID) {
    handshakeStates[peerID] = {HandshakeState::ESTABLISHED, millis(), 0, false, ""};
}

void NoiseHandshakeCoordinator::recordHandshakeFailure(const std::string& peerID, const std::string& reason) {
    int attempts = getCurrentAttempt(peerID);
    bool canRetry = attempts < maxHandshakeAttempts;
    handshakeStates[peerID] = {HandshakeState::FAILED, millis(), attempts, canRetry, reason};
}

bool NoiseHandshakeCoordinator::shouldAcceptHandshakeInitiation(const std::string& myPeerID, const std::string& remotePeerID) {
    auto it = handshakeStates.find(remotePeerID);
    if (it != handshakeStates.end()) {
        if (it->second.state == HandshakeState::ESTABLISHED) {
            return false;
        }
    }

    if (determineHandshakeRole(myPeerID, remotePeerID) == NoiseRole::INITIATOR) {
        if (it != handshakeStates.end() && it->second.state == HandshakeState::INITIATING) {
            return true;
        }
    }

    return true;
}

bool NoiseHandshakeCoordinator::isDuplicateHandshakeMessage(const std::vector<uint8_t>& data) {
    for (const auto& msg : processedHandshakeMessages) {
        if (msg == data) {
            return true;
        }
    }

    if (processedHandshakeMessages.size() >= messageHistoryLimit) {
        processedHandshakeMessages.clear();
    }
    processedHandshakeMessages.push_back(data);
    return false;
}

unsigned long NoiseHandshakeCoordinator::getRetryDelay(const std::string& peerID) {
    auto it = handshakeStates.find(peerID);
    if (it == handshakeStates.end()) return 0;

    const auto& state = it->second;
    if (state.state == HandshakeState::FAILED) {
        if (!state.canRetry) return -1;
        unsigned long timeSinceFailure = millis() - state.timestamp;
        if (timeSinceFailure >= retryDelay) {
            return 0;
        }
        return retryDelay - timeSinceFailure;
    } else if (state.state == HandshakeState::INITIATING) {
        unsigned long timeSinceAttempt = millis() - state.timestamp;
        if (timeSinceAttempt >= minTimeBetweenHandshakes) {
            return 0;
        }
        return minTimeBetweenHandshakes - timeSinceAttempt;
    }
    return 0;
}

void NoiseHandshakeCoordinator::resetHandshakeState(const std::string& peerID) {
    handshakeStates.erase(peerID);
}

std::vector<std::string> NoiseHandshakeCoordinator::cleanupStaleHandshakes(unsigned long staleTimeout) {
    std::vector<std::string> stalePeerIDs;
    unsigned long now = millis();
    for (auto const& [peerID, state] : handshakeStates) {
        bool isStale = false;
        if (state.state == HandshakeState::INITIATING || state.state == HandshakeState::RESPONDING) {
            if (now - state.timestamp > staleTimeout) {
                isStale = true;
            }
        }
        if (isStale) {
            stalePeerIDs.push_back(peerID);
        }
    }
    for (const auto& peerID : stalePeerIDs) {
        handshakeStates.erase(peerID);
    }
    return stalePeerIDs;
}

HandshakeState NoiseHandshakeCoordinator::getHandshakeState(const std::string& peerID) {
    auto it = handshakeStates.find(peerID);
    return it != handshakeStates.end() ? it->second.state : HandshakeState::IDLE;
}

int NoiseHandshakeCoordinator::getRetryCount(const std::string& peerID) {
    auto it = handshakeStates.find(peerID);
    return it != handshakeStates.end() ? it->second.attempt - 1 : 0;
}

void NoiseHandshakeCoordinator::incrementRetryCount(const std::string& peerID) {
    int currentAttempt = getCurrentAttempt(peerID);
    handshakeStates[peerID] = {HandshakeState::INITIATING, millis(), currentAttempt + 1, false, ""};
}

void NoiseHandshakeCoordinator::clearAllHandshakeStates() {
    handshakeStates.clear();
    processedHandshakeMessages.clear();
}

int NoiseHandshakeCoordinator::getCurrentAttempt(const std::string& peerID) {
    auto it = handshakeStates.find(peerID);
    if (it != handshakeStates.end()) {
        return it->second.attempt;
    }
    return 0;
}
