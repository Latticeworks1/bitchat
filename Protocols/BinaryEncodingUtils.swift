//
// BinaryEncodingUtils.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation

extension Data {
    mutating func appendUUID(_ uuidString: String) {
        if let uuid = UUID(uuidString: uuidString) {
            var uuidBytes = uuid.uuid
            withUnsafeBytes(of: &uuidBytes) {
                append(contentsOf: $0)
            }
        }
    }

    func readUUID(at offset: inout Int) -> String? {
        guard offset + 16 <= count else { return nil }
        let uuid = UUID(uuidBytes: withUnsafeBytes {
            var uuid: uuid_t = (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
            memcpy(&uuid, $0.baseAddress! + offset, 16)
            return uuid
        })
        offset += 16
        return uuid.uuidString
    }
}
