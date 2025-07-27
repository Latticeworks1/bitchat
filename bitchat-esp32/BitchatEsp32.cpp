#include "BitchatEsp32.h"
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>
#include <mbedtls/aes.h>
#include "lz4.h"

namespace Bitchat {

static const char* TAG = "BitchatProfiler";
static ProfilingData profilingData = {0, 0, 0};
QueueHandle_t packetQueue;

const char* errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "SUCCESS";
        case ErrorCode::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
        case ErrorCode::PAYLOAD_TOO_LARGE: return "PAYLOAD_TOO_LARGE";
        case ErrorCode::INVALID_PARAMETER: return "INVALID_PARAMETER";
        case ErrorCode::ENCRYPTION_ERROR: return "ENCRYPTION_ERROR";
        case ErrorCode::COMPRESSION_ERROR: return "COMPRESSION_ERROR";
        case ErrorCode::UNSUPPORTED_MESSAGE_TYPE: return "UNSUPPORTED_MESSAGE_TYPE";
        default: return "UNKNOWN_ERROR";
    }
}

ErrorCode validatePacket(const BitchatPacket& packet) {
    int64_t startTime = esp_timer_get_time();

    if (packet.header.version != Constants::PROTOCOL_VERSION) {
        ESP_LOGE(TAG, "Invalid version: %u", packet.header.version);
        return ErrorCode::UNSUPPORTED_VERSION;
    }
    if (packet.payloadLength > 2048) {
        ESP_LOGE(TAG, "Payload too large: %u", packet.payloadLength);
        return ErrorCode::PAYLOAD_TOO_LARGE;
    }
    if (packet.header.flags & Flags::HAS_RECIPIENT) {
        if (memcmp(packet.recipientID, Constants::NULL_RECIPIENT, Constants::RECIPIENT_ID_SIZE) == 0) {
            ESP_LOGE(TAG, "Invalid recipient ID");
            return ErrorCode::INVALID_PARAMETER;
        }
    }

    int64_t endTime = esp_timer_get_time();
    profilingData.validateTimeUs += (endTime - startTime);
    profilingData.packetCount++;
    return ErrorCode::SUCCESS;
}

ErrorCode decryptPayload(uint8_t* payload, size_t length, uint8_t* key, uint8_t* output) {
    int64_t startTime = esp_timer_get_time();
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_dec(&aes, key, 256);
    if (ret != 0) {
        ESP_LOGE(TAG, "AES key setup failed: %d", ret);
        mbedtls_aes_free(&aes);
        return ErrorCode::ENCRYPTION_ERROR;
    }
    // For CBC mode, a 16-byte IV is expected to precede the ciphertext.
    // This is a simplified example; a real implementation would handle the IV properly.
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, length, key + 32, payload, output);
    mbedtls_aes_free(&aes);
    int64_t endTime = esp_timer_get_time();
    ESP_LOGI(TAG, "AES decryption took %lld us", endTime - startTime);
    return ret == 0 ? ErrorCode::SUCCESS : ErrorCode::ENCRYPTION_ERROR;
}

ErrorCode decompressPayload(BitchatPacket& packet) {
    int64_t startTime = esp_timer_get_time();
    if (!(packet.header.flags & Flags::IS_COMPRESSED)) {
        return ErrorCode::SUCCESS;
    }
    uint8_t tempBuffer[2048];
    int decompressedSize = LZ4_decompress_safe(
        (const char*)packet.payload, (char*)tempBuffer, packet.payloadLength, 2048);
    if (decompressedSize < 0) {
        ESP_LOGE(TAG, "Decompression failed: %d", decompressedSize);
        return ErrorCode::COMPRESSION_ERROR;
    }
    memcpy(packet.payload, tempBuffer, decompressedSize);
    packet.payloadLength = decompressedSize;
    packet.originalPayloadSize = decompressedSize;
    int64_t endTime = esp_timer_get_time();
    ESP_LOGI(TAG, "Decompression took %lld us", endTime - startTime);
    return ErrorCode::SUCCESS;
}

ErrorCode processPacket(BitchatPacket& packet) {
    int64_t startTime = esp_timer_get_time();

    if (packet.header.flags & Flags::IS_ENCRYPTED) {
        uint8_t key[256/8 + 16] = {0}; // key + IV
        uint8_t decrypted_payload[2048];
        ErrorCode res = decryptPayload(packet.payload, packet.payloadLength, key, decrypted_payload);
        if (res != ErrorCode::SUCCESS) return res;
        memcpy(packet.payload, decrypted_payload, packet.payloadLength);
    }

    if (packet.header.flags & Flags::IS_COMPRESSED) {
        ErrorCode res = decompressPayload(packet);
        if (res != ErrorCode::SUCCESS) return res;
    }

    switch (static_cast<MessageType>(packet.header.type)) {
        case MessageType::MESSAGE: {
            // Simulate message processing
            ets_delay_us(50);
            break;
        }
        case MessageType::HANDSHAKE_REQUEST: {
            // Simulate Noise handshake
            ets_delay_us(300);
            break;
        }
        case MessageType::EMERGENCY_BROADCAST: {
            // Prioritize emergency messages
            ets_delay_us(10);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unsupported message type: %u", packet.header.type);
            return ErrorCode::UNSUPPORTED_MESSAGE_TYPE;
    }

    int64_t endTime = esp_timer_get_time();
    profilingData.processTimeUs += (endTime - startTime);
    return ErrorCode::SUCCESS;
}

void logProfilingData() {
    if (profilingData.packetCount > 0) {
        ESP_LOGI(TAG, "Processed %u packets", profilingData.packetCount);
        ESP_LOGI(TAG, "Avg validatePacket time: %lld us",
                 profilingData.validateTimeUs / profilingData.packetCount);
        ESP_LOGI(TAG, "Avg processPacket time: %lld us",
                 profilingData.processTimeUs / profilingData.packetCount);
        ESP_LOGI(TAG, "Free heap: %u bytes", esp_get_free_heap_size());
    } else {
        ESP_LOGI(TAG, "No packets processed");
    }
}

#include <esp_sleep.h>

void enterLightSleepIfIdle() {
    if (uxQueueMessagesWaiting(packetQueue) == 0) {
        esp_sleep_enable_timer_wakeup(1000000); // 1 second
        esp_light_sleep_start();
    }
}

void packetProcessingTask(void* pvParameters) {
    BitchatPacket packets[4];
    while (true) {
        int received = uxQueueMessagesWaiting(packetQueue);
        received = received > 4 ? 4 : received;
        for (int i = 0; i < received; i++) {
            if (xQueueReceive(packetQueue, &packets[i], 0) == pdTRUE) {
                ErrorCode result = validatePacket(packets[i]);
                if (result == ErrorCode::SUCCESS) {
                    result = processPacket(packets[i]);
                }
                if (result != ErrorCode::SUCCESS) {
                    ESP_LOGE(TAG, "Packet %d failed: %s", i,
                             errorCodeToString(result));
                }
            }
        }
        if (profilingData.packetCount > 0 && profilingData.packetCount % 10 == 0) {
            logProfilingData();
        }
        enterLightSleepIfIdle();
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield
    }
}

void initPacketProcessing() {
    packetQueue = xQueueCreate(10, sizeof(BitchatPacket));
    xTaskCreatePinnedToCore(packetProcessingTask, "PacketTask", 4096, nullptr, 5, nullptr, 1);
}

} // namespace Bitchat
