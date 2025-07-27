#pragma once

#include "Arduino.h"
#include <vector>
#include <string>
#include <cstdint>

enum class MessageType : uint8_t {
    ANNOUNCE = 0x01,
    LEAVE = 0x03,
    MESSAGE = 0x04,
    FRAGMENT_START = 0x05,
    FRAGMENT_CONTINUE = 0x06,
    FRAGMENT_END = 0x07,
    DELIVERY_ACK = 0x0A,
    DELIVERY_STATUS_REQUEST = 0x0B,
    READ_RECEIPT = 0x0C,
    NOISE_HANDSHAKE_INIT = 0x10,
    NOISE_HANDSHAKE_RESP = 0x11,
    NOISE_ENCRYPTED = 0x12,
    NOISE_IDENTITY_ANNOUNCE = 0x13,
    VERSION_HELLO = 0x20,
    VERSION_ACK = 0x21,
    PROTOCOL_ACK = 0x22,
    PROTOCOL_NACK = 0x23,
    SYSTEM_VALIDATION = 0x24
};

struct BitchatPacket {
    uint8_t version;
    MessageType type;
    std::vector<uint8_t> senderID;
    std::vector<uint8_t> recipientID;
    uint64_t timestamp;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> signature;
    uint8_t ttl;

    std::vector<uint8_t> serialize() const;
    static BitchatPacket deserialize(const std::vector<uint8_t>& data);
};

struct BitchatMessage {
    std::string id;
    std::string sender;
    std::string content;
    uint64_t timestamp;
    bool isRelay;
    std::string originalSender;
    bool isPrivate;
    std::string recipientNickname;
    std::string senderPeerID;
    std::vector<std::string> mentions;

    std::vector<uint8_t> serialize() const;
    static BitchatMessage deserialize(const std::vector<uint8_t>& data);
};

struct DeliveryAck {
    std::string originalMessageID;
    std::string ackID;
    std::string recipientID;
    std::string recipientNickname;
    uint64_t timestamp;
    uint8_t hopCount;

    std::vector<uint8_t> serialize() const;
    static DeliveryAck deserialize(const std::vector<uint8_t>& data);
};

struct ReadReceipt {
    std::string originalMessageID;
    std::string receiptID;
    std::string readerID;
    std::string readerNickname;
    uint64_t timestamp;

    std::vector<uint8_t> serialize() const;
    static ReadReceipt deserialize(const std::vector<uint8_t>& data);
};

struct ProtocolAck {
    std::string originalPacketID;
    std::string ackID;
    std::string senderID;
    std::string receiverID;
    MessageType packetType;
    uint64_t timestamp;
    uint8_t hopCount;

    std::vector<uint8_t> serialize() const;
    static ProtocolAck deserialize(const std::vector<uint8_t>& data);
};

struct ProtocolNack {
    std::string originalPacketID;
    std::string nackID;
    std::string senderID;
    std::string receiverID;
    MessageType packetType;
    uint64_t timestamp;
    std::string reason;
    uint8_t errorCode;

    std::vector<uint8_t> serialize() const;
    static ProtocolNack deserialize(const std::vector<uint8_t>& data);
};

struct NoiseIdentityAnnouncement {
    std::string peerID;
    std::vector<uint8_t> publicKey;
    std::vector<uint8_t> signingPublicKey;
    std::string nickname;
    uint64_t timestamp;
    std::string previousPeerID;
    std::vector<uint8_t> signature;

    std::vector<uint8_t> serialize() const;
    static NoiseIdentityAnnouncement deserialize(const std::vector<uint8_t>& data);
};

struct VersionHello {
    std::vector<uint8_t> supportedVersions;
    uint8_t preferredVersion;
    std::string clientVersion;
    std::string platform;
    std::vector<std::string> capabilities;

    std::vector<uint8_t> serialize() const;
    static VersionHello deserialize(const std::vector<uint8_t>& data);
};

struct VersionAck {
    uint8_t agreedVersion;
    std::string serverVersion;
    std::string platform;
    std::vector<std::string> capabilities;
    bool rejected;
    std::string reason;

    std::vector<uint8_t> serialize() const;
    static VersionAck deserialize(const std::vector<uint8_t>& data);
};
