//
// BluetoothScannerService.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import Foundation
import CoreBluetooth
import Combine

class BluetoothScannerService: NSObject, CBCentralManagerDelegate, ObservableObject {

    @Published private(set) var discoveredDevices: [DiscoveredDevice] = []

    private var centralManager: CBCentralManager!
    private var isScanning = false

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func startScanning() {
        guard centralManager.state == .poweredOn else {
            print("[Scanner] Bluetooth is not powered on.")
            return
        }

        if !isScanning {
            print("[Scanner] Starting scan for all BLE peripherals.")
            isScanning = true
            // Setting allow duplicates to true gives us continuous RSSI updates
            centralManager.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        }
    }

    func stopScanning() {
        if isScanning {
            print("[Scanner] Stopping scan.")
            isScanning = false
            centralManager.stopScan()
        }
    }

    // MARK: - CBCentralManagerDelegate Methods

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            print("[Scanner] Bluetooth is On.")
            // If scanning was requested before BT was on, start now
            if isScanning {
                startScanning()
            }
        case .poweredOff:
            print("[Scanner] Bluetooth is Off.")
            // Stop scanning and clear devices if Bluetooth is turned off
            stopScanning()
            discoveredDevices.removeAll()
        case .resetting:
            print("[Scanner] Bluetooth is resetting.")
        case .unauthorized:
            print("[Scanner] App is not authorized to use Bluetooth.")
        case .unsupported:
            print("[Scanner] Bluetooth is not supported on this device.")
        case .unknown:
            print("[Scanner] Bluetooth state is unknown.")
        @unknown default:
            print("[Scanner] A new Bluetooth state was introduced.")
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        let deviceName = peripheral.name ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? "Unknown"
        let rssiInt = RSSI.intValue

        // Use the peripheral's identifier as the stable ID for the device
        let deviceId = peripheral.identifier

        DispatchQueue.main.async {
            // Check if we already have this device in our list
            if let existingDeviceIndex = self.discoveredDevices.firstIndex(where: { $0.id == deviceId }) {
                // Device exists, update its RSSI and potentially its name if it changed
                self.discoveredDevices[existingDeviceIndex].rssi = rssiInt
                if self.discoveredDevices[existingDeviceIndex].name == "Unknown" && deviceName != "Unknown" {
                    self.discoveredDevices[existingDeviceIndex].name = deviceName
                }
            } else {
                // New device, add it to the list
                let newDevice = DiscoveredDevice(id: deviceId, name: deviceName, rssi: rssiInt)
                self.discoveredDevices.append(newDevice)
            }

            // Optional: Sort the list by signal strength
            self.discoveredDevices.sort { $0.rssi > $1.rssi }
        }
    }
}
