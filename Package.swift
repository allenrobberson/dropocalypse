// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "Dropocalypse",
    platforms: [.macOS(.v13)],
    targets: [
        .target(
            name: "DropocalypseCore",
            path: "Sources/DropocalypseCore"
        ),
        .executableTarget(
            name: "Dropocalypse",
            dependencies: ["DropocalypseCore"],
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
        ),
        .testTarget(
            name: "DropocalypseCoreTests",
            dependencies: ["DropocalypseCore"],
            path: "Tests/DropocalypseCoreTests"
        ),
    ]
)
