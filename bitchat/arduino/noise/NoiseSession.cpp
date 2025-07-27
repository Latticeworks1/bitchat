#include "NoiseSession.h"

NoiseSession::NoiseSession(const std::string& peerID, NoiseRole role, const std::vector<uint8_t>& localStaticKey, const std::vector<uint8_t>& remoteStaticKey) :
    peerID(peerID),
    role(role),
    state(NoiseSessionState::UNINITIALIZED),
    handshakeState(nullptr),
    sendCipher(nullptr),
    receiveCipher(nullptr),
    localStaticKey(localStaticKey),
    remoteStaticPublicKey(remoteStaticKey) {}

std::vector<uint8_t> NoiseSession::startHandshake() {
    if (state != NoiseSessionState::UNINITIALIZED) {
        return {};
    }

    handshakeState = new NoiseHandshakeState(role, NoisePattern::XX, localStaticKey);
    state = NoiseSessionState::HANDSHAKING;

    if (role == NoiseRole::INITIATOR) {
        auto message = handshakeState->writeMessage({});
        sentHandshakeMessages.push_back(message);
        return message;
    } else {
        return {};
    }
}

std::vector<uint8_t> NoiseSession::processHandshakeMessage(const std::vector<uint8_t>& message) {
    if (state == NoiseSessionState::UNINITIALIZED && role == NoiseRole::RESPONDER) {
        handshakeState = new NoiseHandshakeState(role, NoisePattern::XX, localStaticKey);
        state = NoiseSessionState::HANDSHAKING;
    }

    if (state != NoiseSessionState::HANDSHAKING || !handshakeState) {
        return {};
    }

    handshakeState->readMessage(message);

    if (handshakeState->isHandshakeComplete()) {
        auto ciphers = handshakeState->getTransportCiphers();
        sendCipher = new NoiseCipherState(ciphers.first);
        receiveCipher = new NoiseCipherState(ciphers.second);
        remoteStaticPublicKey = handshakeState->getRemoteStaticPublicKey();
        handshakeHash = handshakeState->getHandshakeHash();
        state = NoiseSessionState::ESTABLISHED;
        delete handshakeState;
        handshakeState = nullptr;
        return {};
    } else {
        auto response = handshakeState->writeMessage({});
        sentHandshakeMessages.push_back(response);
        if (handshakeState->isHandshakeComplete()) {
            auto ciphers = handshakeState->getTransportCiphers();
            sendCipher = new NoiseCipherState(ciphers.first);
            receiveCipher = new NoiseCipherState(ciphers.second);
            remoteStaticPublicKey = handshakeState->getRemoteStaticPublicKey();
            handshakeHash = handshakeState->getHandshakeHash();
            state = NoiseSessionState::ESTABLISHED;
            delete handshakeState;
            handshakeState = nullptr;
        }
        return response;
    }
}

std::vector<uint8_t> NoiseSession::encrypt(const std::vector<uint8_t>& plaintext) {
    if (state != NoiseSessionState::ESTABLISHED || !sendCipher) {
        return {};
    }
    return sendCipher->encrypt(plaintext, {});
}

std::vector<uint8_t> NoiseSession::decrypt(const std::vector<uint8_t>& ciphertext) {
    if (state != NoiseSessionState::ESTABLISHED || !receiveCipher) {
        return {};
    }
    return receiveCipher->decrypt(ciphertext, {});
}

NoiseSessionState NoiseSession::getState() const {
    return state;
}

bool NoiseSession::isEstablished() const {
    return state == NoiseSessionState::ESTABLISHED;
}

std::vector<uint8_t> NoiseSession::getRemoteStaticPublicKey() const {
    return remoteStaticPublicKey;
}

std::vector<uint8_t> NoiseSession::getHandshakeHash() const {
    return handshakeHash;
}

void NoiseSession::reset() {
    state = NoiseSessionState::UNINITIALIZED;
    delete handshakeState;
    handshakeState = nullptr;
    delete sendCipher;
    sendCipher = nullptr;
    delete receiveCipher;
    receiveCipher = nullptr;
    sentHandshakeMessages.clear();
    handshakeHash.clear();
}

NoiseSessionManager::NoiseSessionManager(const std::vector<uint8_t>& localStaticKey) : localStaticKey(localStaticKey) {}

NoiseSession* NoiseSessionManager::createSession(const std::string& peerID, NoiseRole role) {
    auto session = new NoiseSession(peerID, role, localStaticKey);
    sessions[peerID] = session;
    return session;
}

NoiseSession* NoiseSessionManager::getSession(const std::string& peerID) {
    auto it = sessions.find(peerID);
    return it != sessions.end() ? it->second : nullptr;
}

void NoiseSessionManager::removeSession(const std::string& peerID) {
    auto it = sessions.find(peerID);
    if (it != sessions.end()) {
        delete it->second;
        sessions.erase(it);
    }
}

void NoiseSessionManager::migrateSession(const std::string& oldPeerID, const std::string& newPeerID) {
    auto it = sessions.find(oldPeerID);
    if (it != sessions.end()) {
        sessions[newPeerID] = it->second;
        sessions.erase(it);
    }
}

std::map<std::string, NoiseSession*> NoiseSessionManager::getEstablishedSessions() {
    std::map<std::string, NoiseSession*> established;
    for (auto const& [peerID, session] : sessions) {
        if (session->isEstablished()) {
            established[peerID] = session;
        }
    }
    return established;
}

std::vector<uint8_t> NoiseSessionManager::initiateHandshake(const std::string& peerID) {
    if (getSession(peerID) && getSession(peerID)->isEstablished()) {
        return {};
    }
    if (getSession(peerID)) {
        removeSession(peerID);
    }
    auto session = createSession(peerID, NoiseRole::INITIATOR);
    return session->startHandshake();
}

std::vector<uint8_t> NoiseSessionManager::handleIncomingHandshake(const std::string& peerID, const std::vector<uint8_t>& message) {
    auto session = getSession(peerID);
    if (!session) {
        session = createSession(peerID, NoiseRole::RESPONDER);
    }
    return session->processHandshakeMessage(message);
}

std::vector<uint8_t> NoiseSessionManager::encrypt(const std::vector<uint8_t>& plaintext, const std::string& peerID) {
    auto session = getSession(peerID);
    if (!session) return {};
    return session->encrypt(plaintext);
}

std::vector<uint8_t> NoiseSessionManager::decrypt(const std::vector<uint8_t>& ciphertext, const std::string& peerID) {
    auto session = getSession(peerID);
    if (!session) return {};
    return session->decrypt(ciphertext);
}

std::vector<uint8_t> NoiseSessionManager::getRemoteStaticKey(const std::string& peerID) {
    auto session = getSession(peerID);
    if (!session) return {};
    return session->getRemoteStaticPublicKey();
}

std::vector<uint8_t> NoiseSessionManager::getHandshakeHash(const std::string& peerID) {
    auto session = getSession(peerID);
    if (!session) return {};
    return session->getHandshakeHash();
}
