//
// NoiseProtocol.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation
import CryptoKit

// Core Noise Protocol implementation
// Based on the Noise Protocol Framework specification

// MARK: - Constants and Types

enum NoisePattern {
    case XX  // Most versatile, mutual authentication
}

enum NoiseRole {
    case initiator
    case responder
}

enum NoiseMessagePattern {
    case e     // Ephemeral key
    case s     // Static key
    case ee    // DH(ephemeral, ephemeral)
    case es    // DH(ephemeral, static)
    case se    // DH(static, ephemeral)
    case ss    // DH(static, static)
}
