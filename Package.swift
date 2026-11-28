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
        // The phone remote's web server, shared with the Windows and Linux apps.
        .target(
            name: "DropoRemote",
            path: "shared/remote",
            exclude: ["page.html", "schema.json", "tests"],
            sources: ["remote_server.cpp", "qrcodegen.c"],
            publicHeadersPath: "include",
            cxxSettings: [.headerSearchPath(".")]
        ),
        .executableTarget(
            name: "Dropocalypse",
            dependencies: ["DropocalypseCore", "DropoRemote"],
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
    ],
    cxxLanguageStandard: .cxx17
)
