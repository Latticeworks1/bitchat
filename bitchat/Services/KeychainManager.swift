//
// KeychainManager.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation
import Security

class KeychainManager {
    static let shared = KeychainManager()
    
    // Default service name for channel passwords
    private let defaultPasswordService = "com.bitchat.passwords"
    // Service name for identity keys
    static let identityKeyService = "com.bitchat.identitykey" // Made static for EncryptionService to use
    // Account name for the identity key (typically only one per app)
    static let identityKeyAccount = "bitchat.persistentIdentityPrivateKey" // Made static

    private let accessGroup: String? = nil // Set this if using app groups
    
    private init() {}
    
    // MARK: - Channel Passwords (using default service)
    
    func saveChannelPassword(_ password: String, for channel: String) -> Bool {
        let key = "channel_\(channel)" // This 'key' is kSecAttrAccount
        guard let data = password.data(using: .utf8) else { return false }
        return saveData(data, forKey: key, forService: defaultPasswordService)
    }
    
    func getChannelPassword(for channel: String) -> String? {
        let key = "channel_\(channel)"
        guard let data = retrieveData(forKey: key, forService: defaultPasswordService) else { return nil }
        return String(data: data, encoding: .utf8)
    }
    
    func deleteChannelPassword(for channel: String) -> Bool {
        let key = "channel_\(channel)"
        return delete(forKey: key, forService: defaultPasswordService)
    }
    
    func getAllChannelPasswords() -> [String: String] {
        var passwords: [String: String] = [:]
        
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: defaultPasswordService, // Use default service
            kSecMatchLimit as String: kSecMatchLimitAll,
            kSecReturnAttributes as String: true,
            kSecReturnData as String: true
        ]
        
        if let accessGroup = accessGroup {
            query[kSecAttrAccessGroup as String] = accessGroup
        }
        
        var result: AnyObject?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        
        if status == errSecSuccess, let items = result as? [[String: Any]] {
            for item in items {
                if let account = item[kSecAttrAccount as String] as? String,
                   account.hasPrefix("channel_"),
                   let data = item[kSecValueData as String] as? Data,
                   let password = String(data: data, encoding: .utf8) {
                    let channel = String(account.dropFirst(8)) // Remove "channel_" prefix
                    passwords[channel] = password
                }
            }
        }
        return passwords
    }

    // MARK: - Generic Operations (Now public to be used by EncryptionService)
    
    // Saves data for a given key (kSecAttrAccount) and service (kSecAttrService)
    // Uses kSecAttrAccessibleWhenUnlockedThisDeviceOnly by default and disables iCloud sync.
    public func saveData(_ data: Data, forKey key: String, forService service: String, accessible: CFString = kSecAttrAccessibleWhenUnlockedThisDeviceOnly) -> Bool {
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: key
        ]
        if let accessGroup = accessGroup {
            query[kSecAttrAccessGroup as String] = accessGroup
        }

        // Try to update first. If item doesn't exist, SecItemUpdate returns errSecItemNotFound.
        let attributesToUpdate: [String: Any] = [kSecValueData as String: data]
        var status = SecItemUpdate(query as CFDictionary, attributesToUpdate as CFDictionary)
        
        if status == errSecItemNotFound {
            // Item not found, so add it.
            query[kSecValueData as String] = data
            query[kSecAttrAccessible as String] = accessible // Set accessibility constraint
            query[kSecAttrSynchronizable as String] = kCFBooleanFalse // Do not sync with iCloud Keychain
            status = SecItemAdd(query as CFDictionary, nil)
        }
        
        return status == errSecSuccess
    }
    
    // Retrieves data for a given key (kSecAttrAccount) and service (kSecAttrService)
    public func retrieveData(forKey key: String, forService service: String) -> Data? {
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: key,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]
        if let accessGroup = accessGroup {
            query[kSecAttrAccessGroup as String] = accessGroup
        }
        
        var dataTypeRef: AnyObject?
        let status = SecItemCopyMatching(query as CFDictionary, &dataTypeRef)
        
        if status == errSecSuccess {
            return dataTypeRef as? Data
        }
        return nil
    }
    
    // Deletes data for a given key (kSecAttrAccount) and service (kSecAttrService)
    public func delete(forKey key: String, forService service: String) -> Bool {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: key
        ]
        // No need to add accessGroup to delete query if it wasn't part of the item's identity,
        // but if it was, it should be included for specificity.
        // However, standard practice is to identify by service & account primarily.

        let status = SecItemDelete(query as CFDictionary)
        return status == errSecSuccess || status == errSecItemNotFound // Consider not found also a success for deletion
    }
    
    // MARK: - Cleanup
    
    // Deletes all channel passwords (items under the defaultPasswordService)
    func deleteAllPasswords() -> Bool {
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: defaultPasswordService // Specifically target channel passwords
        ]
        if let accessGroup = accessGroup {
            query[kSecAttrAccessGroup as String] = accessGroup
        }
        
        let status = SecItemDelete(query as CFDictionary)
        return status == errSecSuccess || status == errSecItemNotFound
    }

    // It might be useful to have a deleteAllDataForService method if needed in future
    // func deleteAllData(forService service: String) -> Bool { ... }
}