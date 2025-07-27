#pragma once

#include <cstdint>

namespace Bitchat {

namespace Constants {
    constexpr uint8_t PROTOCOL_VERSION = 1;
    constexpr size_t SENDER_ID_SIZE = 8;
    constexpr size_t RECIPIENT_ID_SIZE = 8;
    constexpr size_t SIGNATURE_SIZE = 64;
    constexpr uint8_t NULL_RECIPIENT[RECIPIENT_ID_SIZE] = {0};
}

enum class ErrorCode {
    SUCCESS,
    UNSUPPORTED_VERSION,
    PAYLOAD_TOO_LARGE,
    INVALID_PARAMETER,
    ENCRYPTION_ERROR,
    COMPRESSION_ERROR,
    UNSUPPORTED_MESSAGE_TYPE
};

const char* errorCodeToString(ErrorCode code);

enum class MessageType : uint8_t {
    MESSAGE = 0x04,
    HANDSHAKE_REQUEST = 0x10,
    EMERGENCY_BROADCAST = 0x1F,
};

namespace Flags {
    constexpr uint8_t HAS_RECIPIENT = 0x01;
    constexpr uint8_t HAS_SIGNATURE = 0x02;
    constexpr uint8_t IS_COMPRESSED = 0x04;
    constexpr uint8_t IS_ENCRYPTED = 0x08;
}

struct BitchatHeader {
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint8_t ttl;
    uint64_t timestamp;
};

struct BitchatPacket {
    BitchatHeader header;
    uint8_t senderID[Constants::SENDER_ID_SIZE];
    uint8_t recipientID[Constants::RECIPIENT_ID_SIZE];
    uint16_t originalPayloadSize;
    uint16_t payloadLength;
    uint8_t payload[2048]; // Reduced to 2 KB to fit SRAM
    uint8_t signature[Constants::SIGNATURE_SIZE];
} __attribute__((packed, aligned(4)));

struct ProfilingData {
    int64_t validateTimeUs;
    int64_t processTimeUs;
    uint32_t packetCount;
};

ErrorCode validatePacket(const BitchatPacket& packet);
ErrorCode processPacket(BitchatPacket& packet);
void logProfilingData();
void enterLightSleepIfIdle();

void initPacketProcessing();

} // namespace Bitchat
