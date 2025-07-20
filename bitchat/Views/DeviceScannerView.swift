//
// DeviceScannerView.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import SwiftUI

struct DeviceScannerView: View {
    @EnvironmentObject var chatViewModel: ChatViewModel
    @State private var isScanning = false

    var body: some View {
        VStack {
            HStack {
                Text("Nearby BLE Devices")
                    .font(.headline)
                Spacer()
                if isScanning {
                    ProgressView()
                }
            }
            .padding()

            List(chatViewModel.nearbyBluetoothDevices) { device in
                HStack {
                    VStack(alignment: .leading) {
                        Text(device.name)
                            .fontWeight(.bold)
                        Text("ID: \(device.id.uuidString)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                    VStack(alignment: .trailing) {
                        Text("\(device.rssi) dBm")
                        RSSIIndicator(rssi: device.rssi)
                    }
                }
            }
            .listStyle(InsetGroupedListStyle())

            HStack {
                Button(action: {
                    chatViewModel.startDeviceScan()
                    isScanning = true
                }) {
                    Text("Start Scan")
                }
                .disabled(isScanning)
                .padding()

                Button(action: {
                    chatViewModel.stopDeviceScan()
                    isScanning = false
                }) {
                    Text("Stop Scan")
                }
                .disabled(!isScanning)
                .padding()
            }
        }
        .onDisappear {
            // Ensure scanning is stopped when the view disappears
            chatViewModel.stopDeviceScan()
            isScanning = false
        }
    }
}

struct RSSIIndicator: View {
    var rssi: Int

    private var color: Color {
        switch rssi {
        case -50...0:
            return .green
        case -70 ..< -50:
            return .yellow
        case -90 ..< -70:
            return .orange
        default:
            return .red
        }
    }

    private var level: Int {
        switch rssi {
        case -50...0:
            return 4
        case -70 ..< -50:
            return 3
        case -90 ..< -70:
            return 2
        default:
            return 1
        }
    }

    var body: some View {
        HStack(spacing: 2) {
            ForEach(1...4, id: \.self) { barLevel in
                Rectangle()
                    .fill(barLevel <= level ? color : Color.gray.opacity(0.3))
                    .frame(width: 5, height: CGFloat(barLevel) * 4)
            }
        }
    }
}

struct DeviceScannerView_Previews: PreviewProvider {
    static var previews: some View {
        let mockViewModel = ChatViewModel()
        mockViewModel.nearbyBluetoothDevices = [
            DiscoveredDevice(id: UUID(), name: "iPhone 13 Pro", rssi: -45),
            DiscoveredDevice(id: UUID(), name: "AirPods Pro", rssi: -65),
            DiscoveredDevice(id: UUID(), name: "Unknown Device", rssi: -85),
            DiscoveredDevice(id: UUID(), name: "Fitness Tracker", rssi: -95)
        ]

        return DeviceScannerView()
            .environmentObject(mockViewModel)
    }
}
