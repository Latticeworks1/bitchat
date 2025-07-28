//
// KeychainManager.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation

class KeychainManager {
    static let shared = KeychainManager()

    func saveIdentityKey(_ data: Data, forKey key: String) -> Bool {
        return false
    }

    func getIdentityKey(forKey key: String) -> Data? {
        return nil
    }

    func deleteIdentityKey(forKey key: String) -> Bool {
        return false
    }
}
