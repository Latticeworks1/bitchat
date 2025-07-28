//
// NoiseSecurityConsiderations.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation

// MARK: - Security Constants

enum NoiseSecurityConstants {
    // Maximum message size to prevent memory exhaustion
    static let maxMessageSize = 65535 // 64KB as per Noise spec
}
