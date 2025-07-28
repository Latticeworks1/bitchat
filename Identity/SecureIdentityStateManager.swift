//
// SecureIdentityStateManager.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation
import CryptoKit

class SecureIdentityStateManager {
    static let shared = SecureIdentityStateManager()

    private let keychain = KeychainManager.shared
    private let cacheKey = "bitchat.identityCache.v2"

    // In-memory state
    private var ephemeralSessions: [String: EphemeralIdentity] = [:]
    private var cryptographicIdentities: [String: CryptographicIdentity] = [:]
    private var cache: IdentityCache = IdentityCache()


    private init() {
        // Load identity cache on init
        loadIdentityCache()
    }

    func loadIdentityCache() {
        guard let encryptedData = keychain.getIdentityKey(forKey: cacheKey) else {
            // No existing cache, start fresh
            return
        }

        do {
            // In a real implementation, we would decrypt the data here.
            cache = try JSONDecoder().decode(IdentityCache.self, from: encryptedData)
        } catch {
            // Log error but continue with empty cache
        }
    }

    func saveIdentityCache() {
        do {
            let data = try JSONEncoder().encode(cache)
            // In a real implementation, we would encrypt the data here.
            let saved = keychain.saveIdentityKey(data, forKey: cacheKey)
        } catch {
            // Log error
        }
    }
}
