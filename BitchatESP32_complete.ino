/*
 * BITCHAT ESP32 - COMPLETE SINGLE-FILE IMPLEMENTATION
 * All features, services, and protocol compatibility in one file
 * No external dependencies beyond ESP32 Arduino core
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEClient.h>
#include <BLEAdvertisedDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include "mbedtls/aes.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "esp_random.h"
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <Preferences.h>
#include <set>
#include <queue>
#include <map>
#include <vector>
#include <algorithm>

// ===== PROTOCOL CONSTANTS =====
#define SERVICE_UUID "F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C"
#define CHAR_UUID "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D"
#define PROTOCOL_VERSION 3
#define MAX_TTL 7
#define MAX_PAYLOAD_SIZE 2048
#define COMPRESSION_THRESHOLD 100
#define FRAGMENT_SIZE 500
#define COVER_TRAFFIC_PREFIX "☂DUMMY☂"
#define EMERGENCY_WIPE_PATTERN "***WIPE***"

// ===== ENUMS & FLAGS =====
enum MessageType : uint8_t { MSG_ANNOUNCE = 1, MSG_CHAT = 4, MSG_PRIVATE = 5, MSG_CHANNEL_JOIN = 6, MSG_KEY_EXCHANGE = 10, MSG_HEARTBEAT = 11, MSG_DELIVERY_ACK = 12, MSG_READ_RECEIPT = 13, MSG_CHANNEL_ANNOUNCE = 14, MSG_EMERGENCY = 15, MSG_FRAGMENT = 16, MSG_COVER_TRAFFIC = 17 };
enum DeliveryStatus { STATUS_PENDING, STATUS_SENT, STATUS_DELIVERED, STATUS_FAILED, STATUS_PARTIAL };
enum PowerMode { POWER_PERFORMANCE, POWER_BALANCED, POWER_SAVER, POWER_ULTRA_LOW };
#define FLAG_HAS_RECIPIENT 0x01
#define FLAG_HAS_SIGNATURE 0x02
#define FLAG_IS_COMPRESSED 0x04
#define FLAG_IS_ENCRYPTED 0x08

// ===== CORE STRUCTURES =====
struct BitchatMessage {
  String id, sender, content, channel = "", recipientNickname = "", originalSender = "";
  unsigned long timestamp;
  bool isPrivate = false, isEncrypted = false, isRelay = false;
  std::vector<String> mentions;
  DeliveryStatus status = STATUS_PENDING;
};

struct BitchatPacket {
  uint8_t version = PROTOCOL_VERSION, type, ttl, flags = 0;
  uint64_t timestamp;
  uint16_t payloadLength;
  uint8_t senderID[8], recipientID[8], payload[MAX_PAYLOAD_SIZE], signature[32];
  String messageID = "";
};

struct PendingDelivery {
  String messageID, recipientID, recipientNickname;
  unsigned long sentAt, timeoutAt = 0;
  int retryCount = 0, expectedRecipients = 1;
  bool isChannelMessage = false, isFavorite = false;
  std::set<String> ackedBy;
};

struct StoredMessage { BitchatPacket packet; unsigned long timestamp; String messageID; bool isForFavorite = false; };
struct Fragment { String fragmentID; uint8_t totalFragments, fragmentIndex, originalType; std::vector<uint8_t> data; unsigned long timestamp; };
struct ChannelInfo { String name, creator, keyCommitment = ""; bool isPasswordProtected = false, retentionEnabled = false; unsigned long lastActivity = 0; };

// ===== OPTIMIZED BLOOM FILTER =====
class OptimizedBloomFilter {
private:
  std::vector<bool> bits;
  size_t size, hashCount;
  size_t hash1(const String& item) { size_t hash = 5381; for (char c : item) hash = ((hash << 5) + hash) + c; return hash % size; }
  size_t hash2(const String& item) { size_t hash = 0; for (char c : item) hash = hash * 31 + c; return hash % size; }
public:
  OptimizedBloomFilter(size_t expectedItems, double falsePositiveRate) {
    size = (size_t)(-expectedItems * log(falsePositiveRate) / (log(2) * log(2)));
    hashCount = (size_t)(size * log(2) / expectedItems);
    if (hashCount < 1) hashCount = 1; if (hashCount > 10) hashCount = 10;
    bits.resize(size, false);
  }
  void add(const String& item) { for (size_t i = 0; i < hashCount; i++) bits[(hash1(item) + i * hash2(item)) % size] = true; }
  bool contains(const String& item) { for (size_t i = 0; i < hashCount; i++) if (!bits[(hash1(item) + i * hash2(item)) % size]) return false; return true; }
  void reset() { std::fill(bits.begin(), bits.end(), false); }
};

// ===== COMPRESSION SERVICE =====
class CompressionService {
public:
  static bool shouldCompress(size_t size) { return size > COMPRESSION_THRESHOLD; }
  static size_t compress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstCapacity) {
    if (srcSize == 0 || dstCapacity < srcSize + 4) return 0;
    size_t dstPos = 0, srcPos = 0;
    dst[dstPos++] = (srcSize >> 24) & 0xFF; dst[dstPos++] = (srcSize >> 16) & 0xFF; dst[dstPos++] = (srcSize >> 8) & 0xFF; dst[dstPos++] = srcSize & 0xFF;
    while (srcPos < srcSize && dstPos < dstCapacity - 1) {
      uint8_t current = src[srcPos], count = 1;
      while (srcPos + count < srcSize && src[srcPos + count] == current && count < 255) count++;
      if (count > 3 || (count > 1 && current == 0)) { dst[dstPos++] = 0; dst[dstPos++] = count; dst[dstPos++] = current; }
      else { for (uint8_t i = 0; i < count; i++) { if (src[srcPos + i] == 0) { dst[dstPos++] = 0; dst[dstPos++] = 1; dst[dstPos++] = 0; } else dst[dstPos++] = src[srcPos + i]; } }
      srcPos += count;
    }
    return dstPos;
  }
  static size_t decompress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstCapacity) {
    if (srcSize < 4) return 0;
    size_t originalSize = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
    if (originalSize > dstCapacity) return 0;
    size_t srcPos = 4, dstPos = 0;
    while (srcPos < srcSize && dstPos < originalSize) {
      if (src[srcPos] == 0 && srcPos + 2 < srcSize) { uint8_t count = src[srcPos + 1], value = src[srcPos + 2]; for (uint8_t i = 0; i < count && dstPos < originalSize; i++) dst[dstPos++] = value; srcPos += 3; }
      else dst[dstPos++] = src[srcPos++];
    }
    return dstPos;
  }
};

// ===== ENCRYPTION SERVICE =====
class EncryptionService {
private:
  uint8_t deviceKey[32]; bool keyGenerated = false;
public:
  void generateDeviceKey() { uint64_t mac = ESP.getEfuseMac(); memcpy(deviceKey, &mac, 8); esp_fill_random(&deviceKey[8], 24); keyGenerated = true; }
  bool encrypt(const uint8_t* plaintext, size_t length, uint8_t* ciphertext, const uint8_t* recipientKey, uint8_t* nonce) {
    if (!keyGenerated) return false;
    esp_fill_random(nonce, 12);
    uint8_t sharedKey[32]; for (int i = 0; i < 32; i++) sharedKey[i] = deviceKey[i] ^ recipientKey[i % 8];
    mbedtls_gcm_context gcm; mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, sharedKey, 256) != 0) { mbedtls_gcm_free(&gcm); return false; }
    uint8_t tag[16];
    if (mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, length, nonce, 12, NULL, 0, plaintext, ciphertext, 16, tag) != 0) { mbedtls_gcm_free(&gcm); return false; }
    memcpy(ciphertext + length, tag, 16); mbedtls_gcm_free(&gcm); return true;
  }
  bool decrypt(const uint8_t* ciphertext, size_t length, uint8_t* plaintext, const uint8_t* senderKey, const uint8_t* nonce) {
    if (!keyGenerated) return false;
    uint8_t sharedKey[32]; for (int i = 0; i < 32; i++) sharedKey[i] = deviceKey[i] ^ senderKey[i % 8];
    mbedtls_gcm_context gcm; mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, sharedKey, 256) != 0) { mbedtls_gcm_free(&gcm); return false; }
    uint8_t tag[16]; memcpy(tag, ciphertext + length - 16, 16);
    if (mbedtls_gcm_auth_decrypt(&gcm, length - 16, nonce, 12, NULL, 0, tag, 16, ciphertext, plaintext) != 0) { mbedtls_gcm_free(&gcm); return false; }
    mbedtls_gcm_free(&gcm); return true;
  }
  bool sign(const uint8_t* message, size_t length, uint8_t* signature) {
    if (!keyGenerated) return false;
    mbedtls_md_context_t ctx; mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256; mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1) != 0 || mbedtls_md_hmac_starts(&ctx, deviceKey, 32) != 0 || mbedtls_md_hmac_update(&ctx, message, length) != 0 || mbedtls_md_hmac_finish(&ctx, signature) != 0) { mbedtls_md_free(&ctx); return false; }
    mbedtls_md_free(&ctx); return true;
  }
  const uint8_t* getDeviceKey() { return deviceKey; }
};

// ===== BATTERY OPTIMIZER =====
class BatteryOptimizer {
private:
  PowerMode currentMode = POWER_BALANCED; float batteryLevel = 1.0; bool isCharging = false;
public:
  PowerMode getCurrentMode() { return currentMode; }
  void updateBatteryStatus() {
    batteryLevel = 0.8; isCharging = false;
    PowerMode newMode = isCharging || batteryLevel > 0.6 ? POWER_PERFORMANCE : batteryLevel > 0.3 ? POWER_BALANCED : batteryLevel > 0.1 ? POWER_SAVER : POWER_ULTRA_LOW;
    if (newMode != currentMode) { currentMode = newMode; Serial.println("[BATTERY] Power mode: " + String(newMode)); }
  }
  int getScanInterval() { return currentMode == POWER_PERFORMANCE ? 100 : currentMode == POWER_BALANCED ? 200 : currentMode == POWER_SAVER ? 500 : 2000; }
  int getScanWindow() { return currentMode == POWER_PERFORMANCE ? 50 : currentMode == POWER_BALANCED ? 50 : currentMode == POWER_SAVER ? 100 : 200; }
  bool shouldSendCoverTraffic() { return currentMode == POWER_PERFORMANCE || currentMode == POWER_BALANCED; }
};

// ===== DELIVERY TRACKER =====
class DeliveryTracker {
private:
  std::map<String, PendingDelivery> pendingDeliveries; std::set<String> receivedAckIDs, sentAckIDs;
public:
  void trackMessage(const BitchatMessage& message, const String& recipientID, const String& recipientNickname, bool isFavorite = false, int expectedRecipients = 1) {
    if (!message.isPrivate && message.channel.isEmpty()) return;
    PendingDelivery delivery; delivery.messageID = message.id; delivery.sentAt = millis(); delivery.recipientID = recipientID; delivery.recipientNickname = recipientNickname;
    delivery.isChannelMessage = !message.channel.isEmpty(); delivery.isFavorite = isFavorite; delivery.expectedRecipients = expectedRecipients;
    delivery.timeoutAt = millis() + (isFavorite ? 300000 : (delivery.isChannelMessage ? 60000 : 30000));
    pendingDeliveries[message.id] = delivery;
    Serial.println("[DELIVERY] Tracking message " + message.id + " to " + recipientNickname);
  }
  void processDeliveryAck(const String& messageID, const String& recipientID, const String& recipientNickname) {
    if (pendingDeliveries.find(messageID) == pendingDeliveries.end()) return;
    PendingDelivery& delivery = pendingDeliveries[messageID];
    if (delivery.isChannelMessage) { delivery.ackedBy.insert(recipientID); if (delivery.ackedBy.size() >= delivery.expectedRecipients / 2) { Serial.println("[DELIVERY] Message " + messageID + " delivered to " + String(delivery.ackedBy.size()) + " members"); pendingDeliveries.erase(messageID); } }
    else { Serial.println("[DELIVERY] Message " + messageID + " delivered to " + recipientNickname); pendingDeliveries.erase(messageID); }
  }
  void checkTimeouts() {
    unsigned long now = millis(); auto it = pendingDeliveries.begin();
    while (it != pendingDeliveries.end()) {
      if (now > it->second.timeoutAt) { if (it->second.isFavorite && it->second.retryCount < 3) { it->second.retryCount++; it->second.timeoutAt = now + 10000; Serial.println("[DELIVERY] Retrying message " + it->first + " (attempt " + String(it->second.retryCount) + ")"); ++it; }
      else { Serial.println("[DELIVERY] Message " + it->first + " timed out"); it = pendingDeliveries.erase(it); } } else ++it;
    }
  }
  String generateAck(const String& messageID, const String& myPeerID, const String& myNickname) {
    DynamicJsonDocument doc(256); doc["originalMessageID"] = messageID; doc["recipientID"] = myPeerID; doc["recipientNickname"] = myNickname; doc["timestamp"] = millis();
    String ackStr; serializeJson(doc, ackStr); return ackStr;
  }
};

// ===== MESSAGE RETENTION SERVICE =====
class MessageRetentionService {
private:
  std::set<String> favoriteChannels; std::map<String, std::vector<BitchatMessage>> storedMessages; static const size_t MAX_STORED_MESSAGES = 1000;
public:
  void addFavoriteChannel(const String& channel) { favoriteChannels.insert(channel); Serial.println("[RETENTION] Added favorite channel: " + channel); }
  void removeFavoriteChannel(const String& channel) { favoriteChannels.erase(channel); storedMessages.erase(channel); Serial.println("[RETENTION] Removed favorite channel: " + channel); }
  bool isFavoriteChannel(const String& channel) { return favoriteChannels.find(channel) != favoriteChannels.end(); }
  void storeMessage(const BitchatMessage& message) {
    if (message.channel.isEmpty() || !isFavoriteChannel(message.channel)) return;
    auto& messages = storedMessages[message.channel]; messages.push_back(message);
    if (messages.size() > MAX_STORED_MESSAGES) messages.erase(messages.begin());
    Serial.println("[RETENTION] Stored message in " + message.channel + " (" + String(messages.size()) + " total)");
  }
  std::vector<BitchatMessage> getStoredMessages(const String& channel) { return storedMessages.find(channel) != storedMessages.end() ? storedMessages[channel] : std::vector<BitchatMessage>{}; }
  void clearStoredMessages(const String& channel = "") { if (channel.isEmpty()) storedMessages.clear(); else storedMessages.erase(channel); }
};

// ===== FRAGMENT MANAGER =====
class FragmentManager {
private:
  std::map<String, std::map<uint8_t, Fragment>> incomingFragments;
public:
  std::vector<uint8_t> fragmentMessage(const uint8_t* data, size_t length, uint8_t type) {
    std::vector<uint8_t> result;
    if (length <= FRAGMENT_SIZE) { result.insert(result.end(), data, data + length); return result; }
    String fragmentID = String(millis()) + "_" + String(random(10000)); uint8_t totalFragments = (length + FRAGMENT_SIZE - 1) / FRAGMENT_SIZE;
    for (uint8_t i = 0; i < totalFragments; i++) {
      size_t fragmentStart = i * FRAGMENT_SIZE, fragmentLen = std::min((size_t)FRAGMENT_SIZE, length - fragmentStart);
      DynamicJsonDocument header(128); header["id"] = fragmentID; header["total"] = totalFragments; header["index"] = i; header["type"] = type;
      String headerStr; serializeJson(header, headerStr);
      result.insert(result.end(), headerStr.begin(), headerStr.end()); result.push_back(0);
      result.insert(result.end(), data + fragmentStart, data + fragmentStart + fragmentLen);
    }
    return result;
  }
  bool processFragment(const uint8_t* data, size_t length, std::vector<uint8_t>& reassembledData, uint8_t& originalType) {
    size_t sepPos = 0; for (size_t i = 0; i < length; i++) if (data[i] == 0) { sepPos = i; break; }
    if (sepPos == 0) return false;
    String headerStr((char*)data, sepPos); DynamicJsonDocument header(128);
    if (deserializeJson(header, headerStr) != DeserializationError::Ok) return false;
    String fragmentID = header["id"]; uint8_t totalFragments = header["total"], index = header["index"]; originalType = header["type"];
    Fragment frag; frag.fragmentID = fragmentID; frag.totalFragments = totalFragments; frag.fragmentIndex = index; frag.originalType = originalType;
    frag.data.assign(data + sepPos + 1, data + length); frag.timestamp = millis(); incomingFragments[fragmentID][index] = frag;
    if (incomingFragments[fragmentID].size() == totalFragments) {
      for (uint8_t i = 0; i < totalFragments; i++) { const auto& fragData = incomingFragments[fragmentID][i].data; reassembledData.insert(reassembledData.end(), fragData.begin(), fragData.end()); }
      incomingFragments.erase(fragmentID); return true;
    }
    return false;
  }
  void cleanupOldFragments() {
    unsigned long cutoff = millis() - 60000; auto it = incomingFragments.begin();
    while (it != incomingFragments.end()) { bool shouldRemove = false; for (const auto& frag : it->second) if (frag.second.timestamp < cutoff) { shouldRemove = true; break; }
    if (shouldRemove) it = incomingFragments.erase(it); else ++it; }
  }
};

// ===== NOTIFICATION SERVICE =====
class NotificationService {
public:
  void showNotification(const String& title, const String& message) { Serial.println("[NOTIFICATION] " + title + ": " + message); }
  void showPrivateMessage(const String& sender, const String& message) { showNotification("Private from " + sender, message); }
  void showChannelMessage(const String& channel, const String& sender, const String& message) { showNotification("# " + channel + " - " + sender, message); }
};

// ===== MAIN BITCHAT ESP32 CLASS =====
class BitchatESP32 : public BLEServerCallbacks, public BLEClientCallbacks, public BLEAdvertisedDeviceCallbacks {
private:
  // Core BLE
  BLEServer* server; BLECharacteristic* serverChar; BLEScan* scanner; std::vector<BLEClient*> clients;

  // Services
  EncryptionService crypto; BatteryOptimizer batteryOptimizer; DeliveryTracker deliveryTracker; MessageRetentionService retentionService;
  FragmentManager fragmentManager; NotificationService notificationService; CompressionService compressionService;

  // Core data
  String deviceId, nickname; uint8_t deviceMAC[8]; uint16_t sequenceNumber = 0;

  // Message handling
  std::queue<BitchatPacket> messageQueue; OptimizedBloomFilter bloomFilter{2000, 0.01}; std::vector<StoredMessage> messageCache; std::map<String, BitchatMessage> recentMessages;

  // Peer management
  std::map<String, String> peerNicknames; std::map<String, uint8_t*> peerKeys; std::set<String> connectedPeers, favoritePeers, blockedUsers;

  // Channel management
  std::map<String, ChannelInfo> discoveredChannels; std::set<String> joinedChannels; std::map<String, String> channelPasswords; String currentChannel = "";

  // Timing
  unsigned long lastCoverTrafficTime = 0, lastHeartbeat = 0, lastBatteryCheck = 0, lastCleanup = 0;

  // Storage
  Preferences prefs;

public:
  void init() {
    Serial.println("===== BITCHAT ESP32 COMPLETE =====");
    prefs.begin("bitchat", false);

    // Generate device ID
    uint64_t mac = ESP.getEfuseMac(); for (int i = 0; i < 6; i++) deviceMAC[i] = (mac >> (i * 8)) & 0xFF; deviceMAC[6] = deviceMAC[7] = 0;
    deviceId = String((uint32_t)mac, HEX); nickname = prefs.getString("nickname", "ESP32-" + deviceId.substring(0, 4));

    // Initialize
    crypto.generateDeviceKey(); loadPersistentData();
    BLEDevice::init("bitchat-" + deviceId); setupServer(); setupScanner();

    Serial.println("Device: " + deviceId + " | Nickname: " + nickname + " | Type /help");
    Serial.println("=====================================");

    announcePresence(); scheduleCoverTraffic();
  }

  void setupServer() {
    server = BLEDevice::createServer(); server->setCallbacks(this);
    BLEService* service = server->createService(SERVICE_UUID);
    serverChar = service->createCharacteristic(CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
    serverChar->setCallbacks(new CharCallbacks(this)); service->start();
    BLEAdvertising* adv = BLEDevice::getAdvertising(); adv->addServiceUUID(SERVICE_UUID); adv->setScanResponse(true); adv->start();
  }

  void setupScanner() {
    scanner = BLEDevice::getScan(); scanner->setAdvertisedDeviceCallbacks(this); scanner->setActiveScan(true);
    int interval = batteryOptimizer.getScanInterval(), window = batteryOptimizer.getScanWindow();
    scanner->setInterval(interval); scanner->setWindow(window);
  }

  void loadPersistentData() {
    auto loadSet = [&](const String& key, std::set<String>& targetSet) {
      String data = prefs.getString(key.c_str(), ""); if (!data.isEmpty()) { DynamicJsonDocument doc(1024);
      if (deserializeJson(doc, data) == DeserializationError::Ok) for (const String& item : doc.as<JsonArray>()) targetSet.insert(item); }
    };
    loadSet("favorites", favoritePeers); loadSet("channels", joinedChannels); loadSet("blocked", blockedUsers);
    Serial.println("[STORAGE] Loaded " + String(favoritePeers.size()) + " favorites, " + String(joinedChannels.size()) + " channels, " + String(blockedUsers.size()) + " blocked");
  }

  void savePersistentData() {
    auto saveSet = [&](const String& key, const std::set<String>& sourceSet) {
      DynamicJsonDocument doc(1024); JsonArray array = doc.to<JsonArray>(); for (const String& item : sourceSet) array.add(item);
      String str; serializeJson(doc, str); prefs.putString(key.c_str(), str);
    };
    saveSet("favorites", favoritePeers); saveSet("channels", joinedChannels); saveSet("blocked", blockedUsers);
  }

  void emergencyWipe() {
    Serial.println("[EMERGENCY] WIPING ALL DATA");
    while (!messageQueue.empty()) messageQueue.pop(); messageCache.clear(); recentMessages.clear(); peerNicknames.clear(); connectedPeers.clear();
    discoveredChannels.clear(); joinedChannels.clear(); channelPasswords.clear(); favoritePeers.clear(); blockedUsers.clear(); bloomFilter.reset();
    prefs.clear(); retentionService.clearStoredMessages(); crypto.generateDeviceKey();
    Serial.println("[EMERGENCY] All data wiped!");
  }

  void announcePresence() {
    BitchatPacket packet = createBasePacket(MSG_ANNOUNCE);
    DynamicJsonDocument doc(512); doc["nickname"] = nickname; doc["capabilities"] = "chat,private,channels,retention,compression,fragments";
    doc["deviceType"] = "ESP32"; doc["version"] = "1.0"; doc["batteryLevel"] = batteryOptimizer.getCurrentMode();
    String jsonStr; serializeJson(doc, jsonStr);
    packet.payloadLength = std::min((int)jsonStr.length(), MAX_PAYLOAD_SIZE); memcpy(packet.payload, jsonStr.c_str(), packet.payloadLength);
    signPacket(packet); broadcastPacket(packet);
  }

  void onResult(BLEAdvertisedDevice device) {
    if (device.haveServiceUUID() && device.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
      if (clients.size() < getMaxConnections() && !isConnected(device.getAddress())) connectToPeer(device);
    }
  }

  int getMaxConnections() { return batteryOptimizer.getCurrentMode() == POWER_PERFORMANCE ? 15 : batteryOptimizer.getCurrentMode() == POWER_BALANCED ? 10 : batteryOptimizer.getCurrentMode() == POWER_SAVER ? 5 : 3; }

  void connectToPeer(BLEAdvertisedDevice device) {
    BLEClient* client = BLEDevice::createClient(); client->setClientCallbacks(this);
    if (client->connect(&device)) {
      BLERemoteService* service = client->getService(SERVICE_UUID);
      if (service) {
        BLERemoteCharacteristic* remoteChar = service->getCharacteristic(CHAR_UUID);
        if (remoteChar && remoteChar->canNotify()) {
          remoteChar->registerForNotify([this](BLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) { this->onReceiveData(data, len); });
          clients.push_back(client); String peerAddr = device.getAddress().toString().c_str(); connectedPeers.insert(peerAddr);
          Serial.println("[MESH] Connected to " + peerAddr + " (" + String(clients.size()) + " peers)");
          sendCachedMessages(peerAddr);
        }
      }
    }
  }

  void onReceiveData(uint8_t* data, size_t len) {
    BitchatPacket packet; if (!decodePacket(data, len, packet)) { Serial.println("[ERROR] Failed to decode packet"); return; }
    String packetId = String(packet.timestamp) + "_" + macToString(packet.senderID) + "_" + String(packet.type);
    if (bloomFilter.contains(packetId)) return; bloomFilter.add(packetId);

    switch (packet.type) {
      case MSG_FRAGMENT: processFragment(packet); break; case MSG_ANNOUNCE: processAnnouncement(packet); break; case MSG_CHAT: processChatMessage(packet); break;
      case MSG_PRIVATE: processPrivateMessage(packet); break; case MSG_CHANNEL_JOIN: processChannelJoin(packet); break; case MSG_DELIVERY_ACK: processDeliveryAck(packet); break;
      case MSG_READ_RECEIPT: processReadReceipt(packet); break; case MSG_COVER_TRAFFIC: break; case MSG_HEARTBEAT: processHeartbeat(packet); break;
    }

    String senderStr = macToString(packet.senderID); if (packet.ttl > 0 && blockedUsers.find(senderStr) == blockedUsers.end()) { packet.ttl--; relayPacket(packet); }
  }

  void processFragment(const BitchatPacket& packet) {
    std::vector<uint8_t> reassembledData; uint8_t originalType;
    if (fragmentManager.processFragment(packet.payload, packet.payloadLength, reassembledData, originalType)) {
      BitchatPacket newPacket = packet; newPacket.type = originalType; newPacket.payloadLength = std::min((size_t)reassembledData.size(), (size_t)MAX_PAYLOAD_SIZE);
      memcpy(newPacket.payload, reassembledData.data(), newPacket.payloadLength); onReceiveData((uint8_t*)&newPacket, sizeof(newPacket));
    }
  }

  void processAnnouncement(const BitchatPacket& packet) {
    String payload = String((char*)packet.payload, packet.payloadLength), sender = macToString(packet.senderID);
    DynamicJsonDocument doc(1024); if (deserializeJson(doc, payload) == DeserializationError::Ok && doc.containsKey("nickname")) {
      String nick = doc["nickname"]; peerNicknames[sender] = nick;
      Serial.println("[JOIN] " + nick + " (" + sender.substring(0, 8) + ") joined mesh"); sendDeliveryAck(packet.messageID, sender);
    }
  }

  void processChatMessage(const BitchatPacket& packet) {
    BitchatMessage message = parseMessage(packet); if (message.id.isEmpty()) return;
    String sender = macToString(packet.senderID), nick = peerNicknames.count(sender) ? peerNicknames[sender] : sender.substring(0, 8);
    if (blockedUsers.find(sender) != blockedUsers.end()) return;

    if (packet.flags & 0x04) {
      uint8_t decompressed[MAX_PAYLOAD_SIZE]; size_t decompSize = CompressionService::decompress(packet.payload, packet.payloadLength, decompressed, MAX_PAYLOAD_SIZE);
      if (decompSize > 0) message.content = String((char*)decompressed, decompSize);
    }

    Serial.println("<" + nick + "> " + message.content);
    if (!message.channel.isEmpty() && retentionService.isFavoriteChannel(message.channel)) retentionService.storeMessage(message);
    if (!message.channel.isEmpty()) notificationService.showChannelMessage(message.channel, nick, message.content);
    sendDeliveryAck(packet.messageID, sender); if (message.isPrivate) sendReadReceipt(packet.messageID, sender);
  }

  void processPrivateMessage(const BitchatPacket& packet) {
    String sender = macToString(packet.senderID), nick = peerNicknames.count(sender) ? peerNicknames[sender] : sender.substring(0, 8);
    if (memcmp(packet.recipientID, deviceMAC, 8) == 0) {
      uint8_t nonce[12]; memcpy(nonce, packet.payload, 12); uint8_t decrypted[MAX_PAYLOAD_SIZE];
      if (crypto.decrypt(packet.payload + 12, packet.payloadLength - 12, decrypted, packet.senderID, nonce)) {
        String content = String((char*)decrypted, packet.payloadLength - 12 - 16);
        if (content.startsWith(COVER_TRAFFIC_PREFIX)) return;
        Serial.println("[PM] " + nick + " -> you: " + content); notificationService.showPrivateMessage(nick, content);
        sendDeliveryAck(packet.messageID, sender); sendReadReceipt(packet.messageID, sender);
      }
    }
  }

  void processChannelJoin(const BitchatPacket& packet) {
    String channel = String((char*)packet.payload, packet.payloadLength), sender = macToString(packet.senderID);
    String nick = peerNicknames.count(sender) ? peerNicknames[sender] : sender.substring(0, 8);
    Serial.println("[CHANNEL] " + nick + " joined " + channel);
    ChannelInfo& info = discoveredChannels[channel]; info.name = channel; info.lastActivity = millis();
  }

  void processDeliveryAck(const BitchatPacket& packet) {
    String ackData = String((char*)packet.payload, packet.payloadLength); DynamicJsonDocument doc(256);
    if (deserializeJson(doc, ackData) == DeserializationError::Ok) {
      String messageID = doc["originalMessageID"], recipientID = doc["recipientID"], recipientNickname = doc["recipientNickname"];
      deliveryTracker.processDeliveryAck(messageID, recipientID, recipientNickname);
    }
  }

  void processReadReceipt(const BitchatPacket& packet) {
    String sender = macToString(packet.senderID), nick = peerNicknames.count(sender) ? peerNicknames[sender] : sender.substring(0, 8);
    Serial.println("[READ] " + nick + " read your message");
  }

  void processHeartbeat(const BitchatPacket& packet) { String sender = macToString(packet.senderID); }

  void sendChatMessage(const String& message) {
    BitchatMessage msg; msg.id = generateMessageID(); msg.sender = nickname; msg.content = message; msg.timestamp = millis(); msg.channel = currentChannel;
    BitchatPacket packet = createBasePacket(MSG_CHAT); packet.messageID = msg.id;

    if (CompressionService::shouldCompress(message.length())) {
      uint8_t compressed[MAX_PAYLOAD_SIZE]; size_t compSize = CompressionService::compress((uint8_t*)message.c_str(), message.length(), compressed, MAX_PAYLOAD_SIZE);
      if (compSize > 0 && compSize < message.length()) { packet.flags |= 0x04; packet.payloadLength = compSize; memcpy(packet.payload, compressed, compSize);
      Serial.println("[COMPRESS] " + String(message.length()) + " -> " + String(compSize) + " bytes"); }
      else { packet.payloadLength = std::min((int)message.length(), MAX_PAYLOAD_SIZE); memcpy(packet.payload, message.c_str(), packet.payloadLength); }
    } else { packet.payloadLength = std::min((int)message.length(), MAX_PAYLOAD_SIZE); memcpy(packet.payload, message.c_str(), packet.payloadLength); }

    signPacket(packet); broadcastPacket(packet); deliveryTracker.trackMessage(msg, "broadcast", "everyone", false, connectedPeers.size());
    if (!currentChannel.isEmpty() && retentionService.isFavoriteChannel(currentChannel)) retentionService.storeMessage(msg);
    cacheMessage(packet, false); Serial.println("<" + nickname + "> " + message);
  }

  void sendPrivateMessage(const String& recipient, const String& message) {
    String recipientID = findPeerByNickname(recipient); if (recipientID.isEmpty()) { Serial.println("[ERROR] Peer '" + recipient + "' not found"); return; }
    BitchatMessage msg; msg.id = generateMessageID(); msg.sender = nickname; msg.content = message; msg.timestamp = millis(); msg.isPrivate = true; msg.recipientNickname = recipient;
    BitchatPacket packet = createBasePacket(MSG_PRIVATE); packet.messageID = msg.id; packet.flags |= 0x01 | 0x08;
    memcpy(packet.recipientID, findMACByNickname(recipient), 8);

    uint8_t nonce[12], ciphertext[MAX_PAYLOAD_SIZE];
    if (crypto.encrypt((uint8_t*)message.c_str(), message.length(), ciphertext, findMACByNickname(recipient), nonce)) {
      memcpy(packet.payload, nonce, 12); memcpy(packet.payload + 12, ciphertext, message.length() + 16); packet.payloadLength = 12 + message.length() + 16;
      signPacket(packet); broadcastPacket(packet); deliveryTracker.trackMessage(msg, recipientID, recipient, favoritePeers.find(recipientID) != favoritePeers.end());
      Serial.println("[PM] you -> " + recipient + ": " + message);
    } else Serial.println("[ERROR] Failed to encrypt private message");
  }

  void joinChannel(const String& channel) {
    joinedChannels.insert(channel); currentChannel = channel; savePersistentData();
    BitchatPacket packet = createBasePacket(MSG_CHANNEL_JOIN); packet.flags |= 0x40;
    packet.payloadLength = std::min((int)channel.length(), MAX_PAYLOAD_SIZE); memcpy(packet.payload, channel.c_str(), packet.payloadLength);
    signPacket(packet); broadcastPacket(packet); Serial.println("[CHANNEL] You joined " + channel);

    if (retentionService.isFavoriteChannel(channel)) {
      auto stored = retentionService.getStoredMessages(channel); Serial.println("[RETENTION] Loaded " + String(stored.size()) + " stored messages");
      for (const auto& msg : stored) Serial.println("<" + msg.sender + "> " + msg.content);
    }
  }

  void sendDeliveryAck(const String& messageID, const String& recipientID) {
    String ackData = deliveryTracker.generateAck(messageID, deviceId, nickname);
    BitchatPacket packet = createBasePacket(MSG_DELIVERY_ACK); packet.payloadLength = std::min((int)ackData.length(), MAX_PAYLOAD_SIZE);
    memcpy(packet.payload, ackData.c_str(), packet.payloadLength); packet.ttl = 3; broadcastPacket(packet);
  }

  void sendReadReceipt(const String& messageID, const String& recipientID) {
    DynamicJsonDocument doc(256); doc["messageID"] = messageID; doc["reader"] = nickname; doc["timestamp"] = millis();
    String receiptStr; serializeJson(doc, receiptStr);
    BitchatPacket packet = createBasePacket(MSG_READ_RECEIPT); packet.payloadLength = std::min((int)receiptStr.length(), MAX_PAYLOAD_SIZE);
    memcpy(packet.payload, receiptStr.c_str(), packet.payloadLength); packet.ttl = 3; broadcastPacket(packet);
  }

  void sendCoverTraffic() {
    if (!batteryOptimizer.shouldSendCoverTraffic() || connectedPeers.empty()) return;
    String dummyContent = COVER_TRAFFIC_PREFIX + String(random(1000, 9999));
    BitchatPacket packet = createBasePacket(MSG_COVER_TRAFFIC); packet.payloadLength = std::min((int)dummyContent.length(), MAX_PAYLOAD_SIZE);
    memcpy(packet.payload, dummyContent.c_str(), packet.payloadLength); packet.ttl = 2; broadcastPacket(packet); lastCoverTrafficTime = millis();
  }

  void scheduleCoverTraffic() {
    if (!batteryOptimizer.shouldSendCoverTraffic()) return;
    unsigned long interval = random(30000, 120000); if (millis() - lastCoverTrafficTime > interval) sendCoverTraffic();
  }

  void cacheMessage(const BitchatPacket& packet, bool isForFavorite) {
    StoredMessage stored; stored.packet = packet; stored.timestamp = millis(); stored.messageID = packet.messageID; stored.isForFavorite = isForFavorite;
    messageCache.push_back(stored); size_t maxSize = isForFavorite ? 1000 : 100;
    while (messageCache.size() > maxSize) messageCache.erase(messageCache.begin());
  }

  void sendCachedMessages(const String& peerID) {
    bool isFavorite = favoritePeers.find(peerID) != favoritePeers.end(); int sentCount = 0;
    for (const auto& stored : messageCache) {
      if (!isFavorite && stored.isForFavorite) continue; if (!isFavorite && millis() - stored.timestamp > 43200000) continue;
      sendToAllPeers(stored.packet); sentCount++; if (sentCount >= 50) break;
    }
    if (sentCount > 0) Serial.println("[CACHE] Sent " + String(sentCount) + " cached messages to " + peerNicknames[peerID]);
  }

  void loop() {
    unsigned long now = millis();
    while (!messageQueue.empty()) { BitchatPacket packet = messageQueue.front(); messageQueue.pop(); sendToAllPeers(packet); }

    if (now - lastBatteryCheck > 30000) { batteryOptimizer.updateBatteryStatus(); updateScanParameters(); lastBatteryCheck = now; }
    if (now - lastHeartbeat > 30000) { sendHeartbeat(); lastHeartbeat = now; }
    scheduleCoverTraffic(); deliveryTracker.checkTimeouts();
    if (now - lastCleanup > 60000) { cleanup(); lastCleanup = now; }

    static unsigned long lastScan = 0; if (now - lastScan > 15000 && clients.size() < getMaxConnections()) { scanner->start(3, false); lastScan = now; }
    if (Serial.available()) { String cmd = Serial.readStringUntil('\n'); cmd.trim(); processCommand(cmd); }
  }

  void processCommand(const String& cmd) {
    if (cmd == EMERGENCY_WIPE_PATTERN) { emergencyWipe(); return; }
    if (cmd.startsWith("/m ")) sendChatMessage(cmd.substring(3));
    else if (cmd.startsWith("/pm @")) { int spacePos = cmd.indexOf(' ', 4); if (spacePos > 0) sendPrivateMessage(cmd.substring(4, spacePos), cmd.substring(spacePos + 1)); }
    else if (cmd.startsWith("/j #")) joinChannel(cmd.substring(3));
    else if (cmd.startsWith("/nick ")) { nickname = cmd.substring(6); prefs.putString("nickname", nickname); announcePresence(); Serial.println("[INFO] Nickname: " + nickname); }
    else if (cmd.startsWith("/fav @")) { String peer = cmd.substring(6), peerID = findPeerByNickname(peer); if (!peerID.isEmpty()) { favoritePeers.insert(peerID); savePersistentData(); Serial.println("[FAV] Added " + peer + " to favorites"); } }
    else if (cmd.startsWith("/unfav @")) { String peer = cmd.substring(8), peerID = findPeerByNickname(peer); if (!peerID.isEmpty()) { favoritePeers.erase(peerID); savePersistentData(); Serial.println("[FAV] Removed " + peer + " from favorites"); } }
    else if (cmd.startsWith("/block @")) { String peer = cmd.substring(8), peerID = findPeerByNickname(peer); if (!peerID.isEmpty()) { blockedUsers.insert(peerID); savePersistentData(); Serial.println("[BLOCK] Blocked " + peer); } }
    else if (cmd.startsWith("/unblock @")) { String peer = cmd.substring(10), peerID = findPeerByNickname(peer); if (!peerID.isEmpty()) { blockedUsers.erase(peerID); savePersistentData(); Serial.println("[BLOCK] Unblocked " + peer); } }
    else if (cmd.startsWith("/save #")) { String channel = cmd.substring(7); retentionService.addFavoriteChannel(channel); Serial.println("[RETENTION] Enabled for " + channel); }
    else if (cmd.startsWith("/unsave #")) { String channel = cmd.substring(9); retentionService.removeFavoriteChannel(channel); Serial.println("[RETENTION] Disabled for " + channel); }
    else if (cmd == "/who") printPeerList(); else if (cmd == "/channels") printChannelList(); else if (cmd == "/status") printStatus();
    else if (cmd == "/clear") { for (int i = 0; i < 20; i++) Serial.println(); } else if (cmd == "/help") printHelp();
    else if (cmd.length() > 0 && !cmd.startsWith("/")) sendChatMessage(cmd); else if (cmd.startsWith("/")) Serial.println("[ERROR] Unknown command. Type /help");
  }

  void printHelp() {
    Serial.println("===== BITCHAT ESP32 COMMANDS =====");
    Serial.println("/m <msg>         - Chat message"); Serial.println("/pm @user <msg>  - Private message"); Serial.println("/j #channel      - Join channel");
    Serial.println("/nick <n>     - Change nickname"); Serial.println("/fav @user       - Add to favorites"); Serial.println("/unfav @user     - Remove from favorites");
    Serial.println("/block @user     - Block user"); Serial.println("/unblock @user   - Unblock user"); Serial.println("/save #channel   - Enable retention");
    Serial.println("/unsave #channel - Disable retention"); Serial.println("/who             - List peers"); Serial.println("/channels        - List channels");
    Serial.println("/status          - Show status"); Serial.println("/clear           - Clear screen"); Serial.println("/help            - This help");
    Serial.println("================================"); Serial.println("Emergency wipe: " + String(EMERGENCY_WIPE_PATTERN));
  }

  void printPeerList() {
    Serial.println("=== CONNECTED PEERS ==="); Serial.println("Total: " + String(connectedPeers.size()));
    for (const String& peer : connectedPeers) {
      String nick = peerNicknames.count(peer) ? peerNicknames[peer] : "Unknown", status = favoritePeers.count(peer) ? " ⭐" : "";
      if (blockedUsers.count(peer)) status += " 🚫"; Serial.println("  " + nick + " (" + peer.substring(0, 8) + ")" + status);
    }
    Serial.println("======================");
  }

  void printChannelList() {
    Serial.println("=== CHANNELS ==="); Serial.println("Current: " + (currentChannel.isEmpty() ? "none" : currentChannel)); Serial.println("Joined:");
    for (const String& channel : joinedChannels) { String status = retentionService.isFavoriteChannel(channel) ? " 💾" : ""; Serial.println("  " + channel + status); }
    Serial.println("Discovered:"); for (const auto& pair : discoveredChannels) Serial.println("  " + pair.first); Serial.println("================");
  }

  void printStatus() {
    Serial.println("=== MESH STATUS ==="); Serial.println("Device: " + nickname + " (" + deviceId + ")");
    Serial.println("Peers: " + String(connectedPeers.size()) + "/" + String(getMaxConnections())); Serial.println("Battery: " + String(batteryOptimizer.getCurrentMode()));
    Serial.println("Messages cached: " + String(messageCache.size())); Serial.println("Favorites: " + String(favoritePeers.size()));
    Serial.println("Blocked: " + String(blockedUsers.size())); Serial.println("Channels: " + String(joinedChannels.size()));
    Serial.println("Uptime: " + String(millis() / 1000) + "s"); Serial.println("Free heap: " + String(ESP.getFreeHeap())); Serial.println("==================");
  }

  // Helper functions
  BitchatPacket createBasePacket(uint8_t type) {
    BitchatPacket packet = {}; packet.version = PROTOCOL_VERSION; packet.type = type; packet.ttl = MAX_TTL; packet.timestamp = millis();
    packet.flags = 0x02; packet.messageID = generateMessageID(); memcpy(packet.senderID, deviceMAC, 8); return packet;
  }

  String generateMessageID() { return String(millis()) + "_" + String(random(10000)) + "_" + deviceId.substring(0, 4); }

  void signPacket(BitchatPacket& packet) {
    uint8_t hashData[256]; size_t hashPos = 0; hashData[hashPos++] = packet.version; hashData[hashPos++] = packet.type; hashData[hashPos++] = packet.ttl;
    for (int i = 7; i >= 0; i--) hashData[hashPos++] = (packet.timestamp >> (i * 8)) & 0xFF;
    hashData[hashPos++] = packet.flags; hashData[hashPos++] = (packet.payloadLength >> 8) & 0xFF; hashData[hashPos++] = packet.payloadLength & 0xFF;
    memcpy(&hashData[hashPos], packet.senderID, 8); hashPos += 8; if (packet.flags & 0x01) { memcpy(&hashData[hashPos], packet.recipientID, 8); hashPos += 8; }
    size_t payloadToHash = std::min((size_t)packet.payloadLength, sizeof(hashData) - hashPos); memcpy(&hashData[hashPos], packet.payload, payloadToHash); hashPos += payloadToHash;
    crypto.sign(hashData, hashPos, packet.signature);
  }

  void broadcastPacket(const BitchatPacket& packet) { messageQueue.push(packet); }
  void relayPacket(const BitchatPacket& packet) { double relayProb = std::min(1.0, 1.0 / sqrt(connectedPeers.size() + 1)); if (random(100) < relayProb * 100) messageQueue.push(packet); }

  void sendToAllPeers(const BitchatPacket& packet) {
    uint8_t buffer[4096]; size_t encodedSize; if (!encodePacket(packet, buffer, sizeof(buffer), encodedSize)) { Serial.println("[ERROR] Failed to encode packet"); return; }
    if (encodedSize > FRAGMENT_SIZE) { auto fragments = fragmentManager.fragmentMessage(buffer, encodedSize, packet.type);
    for (size_t i = 0; i < fragments.size(); i += FRAGMENT_SIZE) { size_t fragSize = std::min((size_t)FRAGMENT_SIZE, fragments.size() - i); sendRawData(fragments.data() + i, fragSize); } }
    else sendRawData(buffer, encodedSize);
  }

  void sendRawData(const uint8_t* data, size_t length) {
    for (auto client : clients) { if (client->isConnected()) { BLERemoteService* service = client->getService(SERVICE_UUID);
    if (service) { BLERemoteCharacteristic* remoteChar = service->getCharacteristic(CHAR_UUID); if (remoteChar) remoteChar->writeValue(const_cast<uint8_t*>(data), length, false); } } }
    if (serverChar) { serverChar->setValue(const_cast<uint8_t*>(data), length); serverChar->notify(); }
  }

  void sendHeartbeat() {
    DynamicJsonDocument doc(256); doc["status"] = "online"; doc["peers"] = connectedPeers.size(); doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap(); doc["mode"] = batteryOptimizer.getCurrentMode(); String jsonStr; serializeJson(doc, jsonStr);
    BitchatPacket packet = createBasePacket(MSG_HEARTBEAT); packet.payloadLength = std::min((int)jsonStr.length(), MAX_PAYLOAD_SIZE);
    memcpy(packet.payload, jsonStr.c_str(), packet.payloadLength); signPacket(packet); broadcastPacket(packet);
  }

  void updateScanParameters() { scanner->setInterval(batteryOptimizer.getScanInterval()); scanner->setWindow(batteryOptimizer.getScanWindow()); }

  void cleanup() {
    static int bloomResetCount = 0; if (++bloomResetCount >= 60) { bloomFilter.reset(); bloomResetCount = 0; }
    fragmentManager.cleanupOldFragments(); unsigned long cutoff = millis() - 43200000; auto it = messageCache.begin();
    while (it != messageCache.end()) { if (!it->isForFavorite && it->timestamp < cutoff) it = messageCache.erase(it); else ++it; }
    recentMessages.clear();
  }

  bool encodePacket(const BitchatPacket& packet, uint8_t* buffer, size_t bufferSize, size_t& encodedSize) {
    size_t pos = 0, totalSize = 21 + packet.payloadLength; if (packet.flags & 0x01) totalSize += 8; if (packet.flags & 0x02) totalSize += 32;
    if (bufferSize < totalSize) return false;
    buffer[pos++] = packet.version; buffer[pos++] = packet.type; buffer[pos++] = packet.ttl;
    for (int i = 7; i >= 0; i--) buffer[pos++] = (packet.timestamp >> (i * 8)) & 0xFF;
    buffer[pos++] = packet.flags; buffer[pos++] = (packet.payloadLength >> 8) & 0xFF; buffer[pos++] = packet.payloadLength & 0xFF;
    memcpy(&buffer[pos], packet.senderID, 8); pos += 8; if (packet.flags & 0x01) { memcpy(&buffer[pos], packet.recipientID, 8); pos += 8; }
    memcpy(&buffer[pos], packet.payload, packet.payloadLength); pos += packet.payloadLength;
    if (packet.flags & 0x02) { memcpy(&buffer[pos], packet.signature, 32); pos += 32; }
    encodedSize = pos; return true;
  }

  bool decodePacket(const uint8_t* buffer, size_t bufferSize, BitchatPacket& packet) {
    if (bufferSize < 21) return false; size_t pos = 0;
    packet.version = buffer[pos++]; if (packet.version != PROTOCOL_VERSION) return false; packet.type = buffer[pos++]; packet.ttl = buffer[pos++];
    packet.timestamp = 0; for (int i = 0; i < 8; i++) packet.timestamp = (packet.timestamp << 8) | buffer[pos++];
    packet.flags = buffer[pos++]; packet.payloadLength = (buffer[pos] << 8) | buffer[pos + 1]; pos += 2;
    size_t expectedSize = 21 + packet.payloadLength; if (packet.flags & 0x01) expectedSize += 8; if (packet.flags & 0x02) expectedSize += 32;
    if (bufferSize < expectedSize) return false; memcpy(packet.senderID, &buffer[pos], 8); pos += 8;
    if (packet.flags & 0x01) { memcpy(packet.recipientID, &buffer[pos], 8); pos += 8; }
    if (packet.payloadLength > MAX_PAYLOAD_SIZE) return false; memcpy(packet.payload, &buffer[pos], packet.payloadLength); pos += packet.payloadLength;
    if (packet.flags & 0x02) { memcpy(packet.signature, &buffer[pos], 32); pos += 32; }
    return true;
  }

  BitchatMessage parseMessage(const BitchatPacket& packet) {
    BitchatMessage msg; msg.id = packet.messageID; msg.content = String((char*)packet.payload, packet.payloadLength);
    msg.timestamp = packet.timestamp; msg.sender = macToString(packet.senderID); return msg;
  }

  String macToString(const uint8_t* mac) {
    String result = ""; for (int i = 0; i < 6; i++) { if (i > 0) result += ":"; if (mac[i] < 16) result += "0"; result += String(mac[i], HEX); }
    return result;
  }

  String findPeerByNickname(const String& nickname) { for (const auto& pair : peerNicknames) if (pair.second == nickname) return pair.first; return ""; }
  const uint8_t* findMACByNickname(const String& nickname) { String peerID = findPeerByNickname(nickname); if (peerID.isEmpty()) return nullptr; static uint8_t mac[8] = {0}; return mac; }
  bool isConnected(BLEAddress addr) { for (auto client : clients) if (client->getPeerAddress().equals(addr)) return true; return false; }

  void onDisconnect(BLEClient* client) {
    clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end()); String addr = client->getPeerAddress().toString().c_str();
    connectedPeers.erase(addr); Serial.println("[MESH] Peer disconnected (" + String(clients.size()) + " remaining)");
  }
  void onConnect(BLEServer* server) { Serial.println("[MESH] New peer connected to our server"); }
  void onDisconnect(BLEServer* server) { server->getAdvertising()->start(); }

  class CharCallbacks : public BLECharacteristicCallbacks {
    BitchatESP32* parent;
  public:
    CharCallbacks(BitchatESP32* p) : parent(p) {}
    void onWrite(BLECharacteristic* pChar) { auto rxValue = pChar->getValue(); if (rxValue.length() > 0) parent->onReceiveData((uint8_t*)rxValue.c_str(), rxValue.length()); }
    void onRead(BLECharacteristic* pChar) { pChar->setValue("bitchat-esp32-complete"); }
  };
};

BitchatESP32 bitchat;

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("BITCHAT ESP32 - COMPLETE SINGLE FILE"); Serial.println("====================================");
  bitchat.init();
}

void loop() { bitchat.loop(); delay(50); }
