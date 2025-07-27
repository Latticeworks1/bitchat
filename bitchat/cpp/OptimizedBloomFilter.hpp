#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "openssl/sha.h"

class OptimizedBloomFilter {
public:
    OptimizedBloomFilter(int expectedItems = 1000, double falsePositiveRate = 0.01) {
        double m = - (double)expectedItems * log(falsePositiveRate) / (log(2) * log(2));
        bitCount = std::max(64, (int)round(m));

        double k = (double)bitCount / (double)expectedItems * log(2);
        hashCount = std::max(1, std::min(10, (int)round(k)));

        int arraySize = (bitCount + 63) / 64;
        bitArray.resize(arraySize, 0);
        insertCount = 0;
    }

    void insert(const std::string& item) {
        std::vector<int> hashes = generateHashes(item);

        for (int i = 0; i < hashCount; i++) {
            int bitIndex = hashes[i] % bitCount;
            int arrayIndex = bitIndex / 64;
            int bitOffset = bitIndex % 64;

            bitArray[arrayIndex] |= (1ULL << bitOffset);
        }

        insertCount++;
    }

    bool contains(const std::string& item) {
        std::vector<int> hashes = generateHashes(item);

        for (int i = 0; i < hashCount; i++) {
            int bitIndex = hashes[i] % bitCount;
            int arrayIndex = bitIndex / 64;
            int bitOffset = bitIndex % 64;

            if ((bitArray[arrayIndex] & (1ULL << bitOffset)) == 0) {
                return false;
            }
        }

        return true;
    }

    void reset() {
        std::fill(bitArray.begin(), bitArray.end(), 0);
        insertCount = 0;
    }

    double estimatedFalsePositiveRate() const {
        if (insertCount == 0) {
            return 0.0;
        }

        int setBits = 0;
        for (uint64_t value : bitArray) {
            setBits += __builtin_popcountll(value);
        }

        double ratio = (double)(hashCount * insertCount) / (double)bitCount;
        return pow(1.0 - exp(-ratio), (double)hashCount);
    }

    int memorySizeBytes() const {
        return bitArray.size() * sizeof(uint64_t);
    }

private:
    std::vector<uint64_t> bitArray;
    int bitCount;
    int hashCount;
    int insertCount;

    std::vector<int> generateHashes(const std::string& item) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, item.c_str(), item.size());
        SHA256_Final(hash, &sha256);

        std::vector<int> hashes;
        for (int i = 0; i < hashCount; i++) {
            int offset = (i * 4) % (SHA256_DIGEST_LENGTH - 3);
            int value = (int)hash[offset] |
                        ((int)hash[offset + 1] << 8) |
                        ((int)hash[offset + 2] << 16) |
                        ((int)hash[offset + 3] << 24);
            hashes.push_back(abs(value));
        }

        return hashes;
    }
};
