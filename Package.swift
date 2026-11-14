// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "LogoVisualizer",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "LogoVisualizer",
            path: "Sources/LogoVisualizer",
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
