#pragma once

#include "Arduino.h"
#include "NoiseProtocol.h"
#include <vector>
#include <string>
#include <map>

enum class NoiseSessionState {
    UNINITIALIZED,
    HANDSHAKING,
    ESTABLISHED,
    FAILED
};

class NoiseSession {
public:
    NoiseSession(const std::string& peerID, NoiseRole role, const std::vector<uint8_t>& localStaticKey, const std::vector<uint8_t>& remoteStaticKey = {});
    std::vector<uint8_t> startHandshake();
    std::vector<uint8_t> processHandshakeMessage(const std::vector<uint8_t>& message);
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext);
    NoiseSessionState getState() const;
    bool isEstablished() const;
    std::vector<uint8_t> getRemoteStaticPublicKey() const;
    std::vector<uint8_t> getHandshakeHash() const;
    void reset();

private:
    std::string peerID;
    NoiseRole role;
    NoiseSessionState state;
    NoiseHandshakeState* handshakeState;
    NoiseCipherState* sendCipher;
    NoiseCipherState* receiveCipher;
    std::vector<uint8_t> localStaticKey;
    std::vector<uint8_t> remoteStaticPublicKey;
    std::vector<std::vector<uint8_t>> sentHandshakeMessages;
    std::vector<uint8_t> handshakeHash;
};

class NoiseSessionManager {
public:
    NoiseSessionManager(const std::vector<uint8_t>& localStaticKey);
    NoiseSession* createSession(const std::string& peerID, NoiseRole role);
    NoiseSession* getSession(const std::string& peerID);
    void removeSession(const std::string& peerID);
    void migrateSession(const std::string& oldPeerID, const std::string& newPeerID);
    std::map<std::string, NoiseSession*> getEstablishedSessions();
    std::vector<uint8_t> initiateHandshake(const std::string& peerID);
    std::vector<uint8_t> handleIncomingHandshake(const std::string& peerID, const std::vector<uint8_t>& message);
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, const std::string& peerID);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, const std::string& peerID);
    std::vector<uint8_t> getRemoteStaticKey(const std::string& peerID);
    std::vector<uint8_t> getHandshakeHash(const std::string& peerID);

private:
    std::map<std::string, NoiseSession*> sessions;
    std::vector<uint8_t> localStaticKey;
};
