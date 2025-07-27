#include "Identity.h"
#include "FS.h" // For file system access on ESP32

SecureIdentityStateManager& SecureIdentityStateManager::getInstance() {
    static SecureIdentityStateManager instance;
    return instance;
}

SecureIdentityStateManager::SecureIdentityStateManager() {
    loadIdentityCache();
}

void SecureIdentityStateManager::loadIdentityCache() {
    if (SPIFFS.exists(cache_filename)) {
        File file = SPIFFS.open(cache_filename, "r");
        if (file) {
            // In a real implementation, we would parse the JSON file here.
            // For simplicity, we'll just assume the cache is empty.
            file.close();
        }
    }
}

void SecureIdentityStateManager::saveIdentityCache() {
    File file = SPIFFS.open(cache_filename, "w");
    if (file) {
        // In a real implementation, we would serialize the cache to JSON here.
        file.print("{\"version\": 1}");
        file.close();
    }
}

SocialIdentity SecureIdentityStateManager::getSocialIdentity(const std::string& fingerprint) {
    auto it = cache.socialIdentities.find(fingerprint);
    if (it != cache.socialIdentities.end()) {
        return it->second;
    }
    return SocialIdentity{fingerprint, "", "Unknown", TrustLevel::UNKNOWN, false, false, ""};
}

void SecureIdentityStateManager::updateSocialIdentity(const SocialIdentity& identity) {
    cache.socialIdentities[identity.fingerprint] = identity;
    saveIdentityCache();
}

std::vector<std::string> SecureIdentityStateManager::getFavorites() {
    std::vector<std::string> favorites;
    for (const auto& pair : cache.socialIdentities) {
        if (pair.second.isFavorite) {
            favorites.push_back(pair.first);
        }
    }
    return favorites;
}

void SecureIdentityStateManager::setFavorite(const std::string& fingerprint, bool isFavorite) {
    cache.socialIdentities[fingerprint].isFavorite = isFavorite;
    saveIdentityCache();
}

bool SecureIdentityStateManager::isFavorite(const std::string& fingerprint) {
    auto it = cache.socialIdentities.find(fingerprint);
    return it != cache.socialIdentities.end() && it->second.isFavorite;
}

bool SecureIdentityStateManager::isBlocked(const std::string& fingerprint) {
    auto it = cache.socialIdentities.find(fingerprint);
    return it != cache.socialIdentities.end() && it->second.isBlocked;
}

void SecureIdentityStateManager::setBlocked(const std::string& fingerprint, bool isBlocked) {
    cache.socialIdentities[fingerprint].isBlocked = isBlocked;
    if (isBlocked) {
        cache.socialIdentities[fingerprint].isFavorite = false;
    }
    saveIdentityCache();
}

void SecureIdentityStateManager::registerEphemeralSession(const std::string& peerID) {
    ephemeralSessions[peerID] = {peerID, millis(), HandshakeState::NONE, ""};
}

void SecureIdentityStateManager::updateHandshakeState(const std::string& peerID, HandshakeState state, const std::string& fingerprint) {
    auto it = ephemeralSessions.find(peerID);
    if (it != ephemeralSessions.end()) {
        it->second.handshakeState = state;
        if (state == HandshakeState::COMPLETED) {
            it->second.fingerprint = fingerprint;
            cache.lastInteractions[fingerprint] = millis();
            saveIdentityCache();
        }
    }
}

HandshakeState SecureIdentityStateManager::getHandshakeState(const std::string& peerID) {
    auto it = ephemeralSessions.find(peerID);
    return it != ephemeralSessions.end() ? it->second.handshakeState : HandshakeState::NONE;
}

void SecureIdentityStateManager::clearAllIdentityData() {
    cache = IdentityCache();
    ephemeralSessions.clear();
    SPIFFS.remove(cache_filename);
}

void SecureIdentityStateManager::removeEphemeralSession(const std::string& peerID) {
    ephemeralSessions.erase(peerID);
}

void SecureIdentityStateManager::setVerified(const std::string& fingerprint, bool verified) {
    if (verified) {
        bool found = false;
        for(const auto& fp : cache.verifiedFingerprints) {
            if (fp == fingerprint) {
                found = true;
                break;
            }
        }
        if (!found) {
            cache.verifiedFingerprints.push_back(fingerprint);
        }
    } else {
        cache.verifiedFingerprints.erase(std::remove(cache.verifiedFingerprints.begin(), cache.verifiedFingerprints.end(), fingerprint), cache.verifiedFingerprints.end());
    }

    auto it = cache.socialIdentities.find(fingerprint);
    if (it != cache.socialIdentities.end()) {
        it->second.trustLevel = verified ? TrustLevel::VERIFIED : TrustLevel::CASUAL;
    }
    saveIdentityCache();
}

bool SecureIdentityStateManager::isVerified(const std::string& fingerprint) {
    for(const auto& fp : cache.verifiedFingerprints) {
        if (fp == fingerprint) {
            return true;
        }
    }
    return false;
}
