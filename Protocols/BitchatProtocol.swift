//
// BitchatProtocol.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation

enum MessageType: UInt8 {
    case announce = 0x01
    case leave = 0x03
    case message = 0x04
}

struct BitchatPacket: Codable {
    let version: UInt8
    let type: UInt8
    let senderID: Data
    let recipientID: Data?
    let timestamp: UInt64
    let payload: Data
    let signature: Data?
    var ttl: UInt8
}

class BitchatMessage: Codable {
    let id: String
    let sender: String
    let content: String
    let timestamp: Date
}
