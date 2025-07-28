//
// NoiseSession.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation
import CryptoKit

// MARK: - Noise Session State

enum NoiseSessionState: Equatable {
    case uninitialized
    case handshaking
    case established
    case failed(Error)
}

class NoiseSession {

}
