#pragma once

#include <cstdint>

namespace Bitchat {

namespace Constants {
    constexpr uint8_t PROTOCOL_VERSION = 1;
    constexpr size_t SENDER_ID_SIZE = 8;
    constexpr size_t RECIPIENT_ID_SIZE = 8;
    constexpr size_t SIGNATURE_SIZE = 64;
}

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

namespace Flags {
    constexpr uint8_t HAS_RECIPIENT = 0x01;
    constexpr uint8_t HAS_SIGNATURE = 0x02;
    constexpr uint8_t IS_COMPRESSED = 0x04;
}

struct BitchatPacket {
    uint8_t version;
    uint8_t type;
    uint8_t ttl;
    uint64_t timestamp;
    uint8_t flags;
    uint16_t payloadLength;
    uint8_t senderID[Constants::SENDER_ID_SIZE];
    uint8_t recipientID[Constants::RECIPIENT_ID_SIZE];
    uint8_t payload[2048]; // Max payload size
    uint8_t signature[Constants::SIGNATURE_SIZE];
} __attribute__((packed, aligned(4)));

} // namespace Bitchat
