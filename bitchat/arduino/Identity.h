#pragma once

#include "Arduino.h"
#include <vector>
#include <string>
#include <map>

enum class HandshakeState {
    NONE,
    INITIATED,
    IN_PROGRESS,
    COMPLETED,
    FAILED
};

struct EphemeralIdentity {
    std::string peerID;
    unsigned long sessionStart;
    HandshakeState handshakeState;
    std::string fingerprint; // if completed
};

struct CryptographicIdentity {
    std::string fingerprint;
    std::vector<uint8_t> publicKey;
    unsigned long firstSeen;
    unsigned long lastHandshake;
};

enum class TrustLevel {
    UNKNOWN,
    CASUAL,
    TRUSTED,
    VERIFIED
};

struct SocialIdentity {
    std::string fingerprint;
    std::string localPetname;
    std::string claimedNickname;
    TrustLevel trustLevel;
    bool isFavorite;
    bool isBlocked;
    std::string notes;
};

struct IdentityCache {
    std::map<std::string, SocialIdentity> socialIdentities;
    std::map<std::string, std::vector<std::string>> nicknameIndex;
    std::vector<std::string> verifiedFingerprints;
    std::map<std::string, unsigned long> lastInteractions;
    int version = 1;
};

class SecureIdentityStateManager {
public:
    static SecureIdentityStateManager& getInstance();
    void loadIdentityCache();
    void saveIdentityCache();
    SocialIdentity getSocialIdentity(const std::string& fingerprint);
    void updateSocialIdentity(const SocialIdentity& identity);
    std::vector<std::string> getFavorites();
    void setFavorite(const std::string& fingerprint, bool isFavorite);
    bool isFavorite(const std::string& fingerprint);
    bool isBlocked(const std::string& fingerprint);
    void setBlocked(const std::string& fingerprint, bool isBlocked);
    void registerEphemeralSession(const std::string& peerID);
    void updateHandshakeState(const std::string& peerID, HandshakeState state, const std::string& fingerprint = "");
    HandshakeState getHandshakeState(const std::string& peerID);
    void clearAllIdentityData();
    void removeEphemeralSession(const std::string& peerID);
    void setVerified(const std::string& fingerprint, bool verified);
    bool isVerified(const std::string& fingerprint);

private:
    SecureIdentityStateManager();
    IdentityCache cache;
    std::map<std::string, EphemeralIdentity> ephemeralSessions;
    // In a real implementation, we would use a secure storage mechanism
    // like the keychain on iOS or encrypted flash on ESP32.
    // For simplicity, we'll just store the cache in a file.
    const char* cache_filename = "/identity_cache.json";
};
