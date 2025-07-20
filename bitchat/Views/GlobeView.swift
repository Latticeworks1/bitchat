//
// GlobeView.swift
// bitchat
//
// This is free and unencumbered software released into the public domain.
// For more information, see <https://unlicense.org>
//

import SwiftUI
import SceneKit

struct GlobeView: UIViewRepresentable {

    func makeUIView(context: Context) -> SCNView {
        let scnView = SCNView()

        // Create a scene
        let scene = SCNScene()
        scnView.scene = scene

        // Create a camera
        let cameraNode = SCNNode()
        cameraNode.camera = SCNCamera()
        cameraNode.position = SCNVector3(x: 0, y: 0, z: 2.5)
        scene.rootNode.addChildNode(cameraNode)

        // Create the globe
        let sphere = SCNSphere(radius: 1.0)
        let material = SCNMaterial()
        // Here we reference the image we conceptually added to the project
        material.diffuse.contents = UIImage(named: "earth_texture.jpg")
        material.isDoubleSided = false // Optimization for a solid sphere
        sphere.materials = [material]

        let sphereNode = SCNNode(geometry: sphere)
        scene.rootNode.addChildNode(sphereNode)

        // Add rotation animation
        let rotation = SCNAction.repeatForever(SCNAction.rotateBy(x: 0, y: 2 * .pi, z: 0, duration: 60)) // One rotation every 60 seconds
        sphereNode.runAction(rotation)

        // Add ambient light to illuminate the globe
        let ambientLight = SCNNode()
        ambientLight.light = SCNLight()
        ambientLight.light!.type = .ambient
        ambientLight.light!.color = UIColor(white: 0.75, alpha: 1.0)
        scene.rootNode.addChildNode(ambientLight)

        // Configure the view
        scnView.backgroundColor = UIColor.clear
        scnView.allowsCameraControl = false // We don't want the user to move the camera

        return scnView
    }

    func updateUIView(_ uiView: SCNView, context: Context) {
        // No updates needed for this simple case
    }
}

// macOS compatibility using NSViewRepresentable
struct GlobeViewMacOS: NSViewRepresentable {

    func makeNSView(context: Context) -> SCNView {
        let scnView = SCNView()

        let scene = SCNScene()
        scnView.scene = scene

        let cameraNode = SCNNode()
        cameraNode.camera = SCNCamera()
        cameraNode.position = SCNVector3(x: 0, y: 0, z: 2.5)
        scene.rootNode.addChildNode(cameraNode)

        let sphere = SCNSphere(radius: 1.0)
        let material = SCNMaterial()
        material.diffuse.contents = NSImage(named: "earth_texture.jpg")
        material.isDoubleSided = false
        sphere.materials = [material]

        let sphereNode = SCNNode(geometry: sphere)
        scene.rootNode.addChildNode(sphereNode)

        let rotation = SCNAction.repeatForever(SCNAction.rotateBy(x: 0, y: 2 * .pi, z: 0, duration: 60))
        sphereNode.runAction(rotation)

        let ambientLight = SCNNode()
        ambientLight.light = SCNLight()
        ambientLight.light!.type = .ambient
        ambientLight.light!.color = NSColor(white: 0.75, alpha: 1.0)
        scene.rootNode.addChildNode(ambientLight)

        scnView.backgroundColor = NSColor.clear
        scnView.allowsCameraControl = false

        return scnView
    }

    func updateNSView(_ nsView: SCNView, context: Context) {
        // No updates needed
    }
}

// Universal GlobeView that picks the right representable for the platform
struct UniversalGlobeView: View {
    var body: some View {
        #if os(macOS)
        GlobeViewMacOS()
        #else
        GlobeView()
        #endif
    }
}
