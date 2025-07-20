//
// EncryptionService.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation
import CryptoKit

class EncryptionService {
    // Key agreement keys for encryption
    private var privateKey: Curve25519.KeyAgreement.PrivateKey
    public let publicKey: Curve25519.KeyAgreement.PublicKey
    
    // Signing keys for authentication
    private var signingPrivateKey: Curve25519.Signing.PrivateKey
    public let signingPublicKey: Curve25519.Signing.PublicKey
    
    // Storage for peer keys
    private var peerPublicKeys: [String: Curve25519.KeyAgreement.PublicKey] = [:]
    private var peerSigningKeys: [String: Curve25519.Signing.PublicKey] = [:]
    private var peerIdentityKeys: [String: Curve25519.Signing.PublicKey] = [:]
    private var sharedSecrets: [String: SymmetricKey] = [:]
    
    // Persistent identity for favorites (separate from ephemeral keys)
    private let identityKey: Curve25519.Signing.PrivateKey
    public let identityPublicKey: Curve25519.Signing.PublicKey
    
    // Thread safety
    private let cryptoQueue = DispatchQueue(label: "chat.bitchat.crypto", attributes: .concurrent)
    
    init() {
        // Generate ephemeral key pairs for this session
        self.privateKey = Curve25519.KeyAgreement.PrivateKey()
        self.publicKey = privateKey.publicKey
        
        self.signingPrivateKey = Curve25519.Signing.PrivateKey()
        self.signingPublicKey = signingPrivateKey.publicKey
        
        // Load or create persistent identity key from Keychain
        let keychainManager = KeychainManager.shared
        let identityKeyAccount = KeychainManager.identityKeyAccount
        let identityKeyService = KeychainManager.identityKeyService
        let userDefaultsKey = "bitchat.identityKey" // Old UserDefaults key

        var loadedIdentityKey: Curve25519.Signing.PrivateKey?

        // 1. Try to load from Keychain
        if let keyDataFromKeychain = keychainManager.retrieveData(forKey: identityKeyAccount, forService: identityKeyService) {
            if let key = try? Curve25519.Signing.PrivateKey(rawRepresentation: keyDataFromKeychain) {
                loadedIdentityKey = key
                // print("[CRYPTO] Loaded identity key from Keychain.")
            } else {
                // print("[CRYPTO] Failed to initialize key from Keychain data. Data might be corrupted.")
                // Attempt to delete corrupted key from Keychain
                _ = keychainManager.delete(forKey: identityKeyAccount, forService: identityKeyService)
            }
        }

        // 2. If not in Keychain, try to migrate from UserDefaults (one-time)
        if loadedIdentityKey == nil, let keyDataFromUserDefaults = UserDefaults.standard.data(forKey: userDefaultsKey) {
            if let key = try? Curve25519.Signing.PrivateKey(rawRepresentation: keyDataFromUserDefaults) {
                // print("[CRYPTO] Migrating identity key from UserDefaults to Keychain.")
                if keychainManager.saveData(key.rawRepresentation, forKey: identityKeyAccount, forService: identityKeyService) {
                    loadedIdentityKey = key
                    UserDefaults.standard.removeObject(forKey: userDefaultsKey) // Remove from UserDefaults after successful migration
                    // print("[CRYPTO] Successfully migrated key to Keychain and removed from UserDefaults.")
                } else {
                    // print("[CRYPTO] Failed to save migrated key to Keychain. Key will remain in UserDefaults for this session.")
                    loadedIdentityKey = key // Use it for this session, but it's not secured in Keychain yet
                }
            } else {
                // print("[CRYPTO] Failed to initialize key from UserDefaults data. Removing corrupted data.")
                UserDefaults.standard.removeObject(forKey: userDefaultsKey)
            }
        }

        // 3. If still no key, generate a new one and save to Keychain
        if let finalKey = loadedIdentityKey {
            self.identityKey = finalKey
        } else {
            // print("[CRYPTO] No existing key found or migration failed. Generating new identity key.")
            let newKey = Curve25519.Signing.PrivateKey()
            if keychainManager.saveData(newKey.rawRepresentation, forKey: identityKeyAccount, forService: identityKeyService) {
                self.identityKey = newKey
                // print("[CRYPTO] Successfully generated and saved new identity key to Keychain.")
            } else {
                // print("[CRYPTO] CRITICAL: Failed to save new identity key to Keychain. Using ephemeral key for this session.")
                // This is a fallback, ideally should not happen. The app will lose its persistent identity if Keychain fails.
                self.identityKey = Curve25519.Signing.PrivateKey() // Use an in-memory key for this session
            }
        }
        self.identityPublicKey = self.identityKey.publicKey
    }
    
    // Create combined public key data for exchange
    func getCombinedPublicKeyData() -> Data {
        var data = Data()
        data.append(publicKey.rawRepresentation)  // 32 bytes - ephemeral encryption key
        data.append(signingPublicKey.rawRepresentation)  // 32 bytes - ephemeral signing key
        data.append(identityPublicKey.rawRepresentation)  // 32 bytes - persistent identity key
        return data  // Total: 96 bytes
    }
    
    // Add peer's combined public keys
    func addPeerPublicKey(_ peerID: String, publicKeyData: Data) throws {
        try cryptoQueue.sync(flags: .barrier) {
            // Convert to array for safe access
            let keyBytes = [UInt8](publicKeyData)
            
            guard keyBytes.count == 96 else {
                // print("[CRYPTO] Invalid public key data size: \(keyBytes.count), expected 96")
                throw EncryptionError.invalidPublicKey
            }
            
            // Extract all three keys: 32 for key agreement + 32 for signing + 32 for identity
            let keyAgreementData = Data(keyBytes[0..<32])
            let signingKeyData = Data(keyBytes[32..<64])
            let identityKeyData = Data(keyBytes[64..<96])
            
            let publicKey = try Curve25519.KeyAgreement.PublicKey(rawRepresentation: keyAgreementData)
            peerPublicKeys[peerID] = publicKey
            
            let signingKey = try Curve25519.Signing.PublicKey(rawRepresentation: signingKeyData)
            peerSigningKeys[peerID] = signingKey
            
            let identityKey = try Curve25519.Signing.PublicKey(rawRepresentation: identityKeyData)
            peerIdentityKeys[peerID] = identityKey
            
            // Stored all three keys for peer
            
            // Generate shared secret for encryption
            if let publicKey = peerPublicKeys[peerID] {
                let sharedSecret = try privateKey.sharedSecretFromKeyAgreement(with: publicKey)
                let symmetricKey = sharedSecret.hkdfDerivedSymmetricKey(
                    using: SHA256.self,
                    salt: "bitchat-v1".data(using: .utf8)!,
                    sharedInfo: Data(),
                    outputByteCount: 32
                )
                sharedSecrets[peerID] = symmetricKey
            }
        }
    }
    
    // Get peer's persistent identity key for favorites
    func getPeerIdentityKey(_ peerID: String) -> Data? {
        return cryptoQueue.sync {
            return peerIdentityKeys[peerID]?.rawRepresentation
        }
    }
    
    // Clear persistent identity (for panic mode)
    func clearPersistentIdentity() {
        let keychainManager = KeychainManager.shared
        let identityKeyAccount = KeychainManager.identityKeyAccount
        let identityKeyService = KeychainManager.identityKeyService
        let userDefaultsKey = "bitchat.identityKey" // Old UserDefaults key

        // Delete from Keychain
        let keychainSuccess = keychainManager.delete(forKey: identityKeyAccount, forService: identityKeyService)
        // if keychainSuccess {
        //     print("[CRYPTO] Successfully cleared persistent identity key from Keychain.")
        // } else {
        //     print("[CRYPTO] Failed to clear persistent identity key from Keychain (it might not have existed).")
        // }

        // Also ensure it's removed from UserDefaults (in case migration hadn't completed or for older versions)
        UserDefaults.standard.removeObject(forKey: userDefaultsKey)
        // print("[CRYPTO] Ensured persistent identity key is also cleared from UserDefaults.")
    }
    
    func encrypt(_ data: Data, for peerID: String) throws -> Data {
        let symmetricKey = try cryptoQueue.sync {
            guard let key = sharedSecrets[peerID] else {
                throw EncryptionError.noSharedSecret
            }
            return key
        }
        
        let sealedBox = try AES.GCM.seal(data, using: symmetricKey)
        return sealedBox.combined ?? Data()
    }
    
    func decrypt(_ data: Data, from peerID: String) throws -> Data {
        let symmetricKey = try cryptoQueue.sync {
            guard let key = sharedSecrets[peerID] else {
                throw EncryptionError.noSharedSecret
            }
            return key
        }
        
        let sealedBox = try AES.GCM.SealedBox(combined: data)
        return try AES.GCM.open(sealedBox, using: symmetricKey)
    }
    
    func sign(_ data: Data) throws -> Data {
        // Create a local copy of the key to avoid concurrent access
        let key = signingPrivateKey
        return try key.signature(for: data)
    }
    
    func verify(_ signature: Data, for data: Data, from peerID: String) throws -> Bool {
        let verifyingKey = try cryptoQueue.sync {
            guard let key = peerSigningKeys[peerID] else {
                throw EncryptionError.noSharedSecret
            }
            return key
        }
        
        return verifyingKey.isValidSignature(signature, for: data)
    }
    
}

enum EncryptionError: Error {
    case noSharedSecret
    case invalidPublicKey
    case encryptionFailed
    case decryptionFailed
}