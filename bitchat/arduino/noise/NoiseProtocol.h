#pragma once

#include "Arduino.h"
#include <vector>
#include <string>

// Forward declarations
class NoiseCipherState;
class NoiseSymmetricState;
class NoiseHandshakeState;

enum class NoisePattern {
    XX,
    IK,
    NK
};

enum class NoiseRole {
    INITIATOR,
    RESPONDER
};

enum class NoiseMessagePattern {
    E,
    S,
    EE,
    ES,
    SE,
    SS
};

class NoiseProtocolName {
public:
    NoiseProtocolName(const std::string& pattern);
    std::string getFullName() const;

private:
    std::string pattern;
    const std::string dh = "25519";
    const std::string cipher = "ChaChaPoly";
    const std::string hash = "SHA256";
};

class NoiseCipherState {
public:
    NoiseCipherState();
    NoiseCipherState(const std::vector<uint8_t>& key);
    void initializeKey(const std::vector<uint8_t>& key);
    bool hasKey() const;
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, const std::vector<uint8_t>& associatedData);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& associatedData);

private:
    std::vector<uint8_t> key;
    uint64_t nonce;
};

class NoiseSymmetricState {
public:
    NoiseSymmetricState(const std::string& protocolName);
    void mixKey(const std::vector<uint8_t>& inputKeyMaterial);
    void mixHash(const std::vector<uint8_t>& data);
    void mixKeyAndHash(const std::vector<uint8_t>& inputKeyMaterial);
    std::vector<uint8_t> getHandshakeHash() const;
    bool hasCipherKey() const;
    std::vector<uint8_t> encryptAndHash(const std::vector<uint8_t>& plaintext);
    std::vector<uint8_t> decryptAndHash(const std::vector<uint8_t>& ciphertext);
    std::pair<NoiseCipherState, NoiseCipherState> split();

private:
    NoiseCipherState cipherState;
    std::vector<uint8_t> chainingKey;
    std::vector<uint8_t> hash;
    std::vector<std::vector<uint8_t>> hkdf(const std::vector<uint8_t>& chainingKey, const std::vector<uint8_t>& inputKeyMaterial, int numOutputs);
};

class NoiseHandshakeState {
public:
    NoiseHandshakeState(NoiseRole role, NoisePattern pattern, const std::vector<uint8_t>& localStaticPrivate, const std::vector<uint8_t>& remoteStaticPublic);
    std::vector<uint8_t> writeMessage(const std::vector<uint8_t>& payload);
    std::vector<uint8_t> readMessage(const std::vector<uint8_t>& message);
    bool isHandshakeComplete() const;
    std::pair<NoiseCipherState, NoiseCipherState> getTransportCiphers();
    std::vector<uint8_t> getRemoteStaticPublicKey() const;
    std::vector<uint8_t> getHandshakeHash() const;
    static std::vector<uint8_t> getPublicKey(const std::vector<uint8_t>& privateKey);
    static bool validatePublicKey(const std::vector<uint8_t>& publicKey);

private:
    NoiseRole role;
    NoisePattern pattern;
    NoiseSymmetricState symmetricState;
    std::vector<uint8_t> localStaticPrivate;
    std::vector<uint8_t> localStaticPublic;
    std::vector<uint8_t> localEphemeralPrivate;
    std::vector<uint8_t> localEphemeralPublic;
    std::vector<uint8_t> remoteStaticPublic;
    std::vector<uint8_t> remoteEphemeralPublic;
    std::vector<std::vector<NoiseMessagePattern>> messagePatterns;
    int currentPattern;
    void mixPreMessageKeys();
    void performDHOperation(NoiseMessagePattern pattern);
};
