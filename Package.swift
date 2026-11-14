// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "Dropocalypse",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "Dropocalypse",
            path: "Sources/Dropocalypse",
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("Metal"),
                .linkedFramework("MetalKit"),
                .linkedFramework("AVFoundation"),
                .linkedFramework("CoreAudio"),
                .linkedFramework("AudioToolbox"),
                .linkedFramework("Accelerate"),
            ]
        )
    ]
)
