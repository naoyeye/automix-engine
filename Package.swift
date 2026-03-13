// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import Foundation
import PackageDescription

// Resolve the Homebrew prefix from the environment (set automatically by Homebrew, or override
// manually). This avoids hard-coding paths and ensures the correct architecture is used:
//   - Apple Silicon Macs: /opt/homebrew  (default)
//   - Intel Macs:         /usr/local
// To override: `export HOMEBREW_PREFIX=/usr/local` before running `swift build`.
let brewPrefix = ProcessInfo.processInfo.environment["HOMEBREW_PREFIX"] ?? "/opt/homebrew"

let package = Package(
    name: "Automix",
    platforms: [
        .macOS(.v13),
        .iOS(.v15)
    ],
    products: [
        .library(
            name: "Automix",
            targets: ["Automix"]),
        .executable(
            name: "AutomixDemo",
            targets: ["AutomixDemo"])
    ],
    targets: [
        .systemLibrary(
            name: "CAutomix",
            path: "include/automix"
        ),
        .target(
            name: "Automix",
            dependencies: ["CAutomix"],
            path: "apple/Sources/Automix",
            linkerSettings: [
                .unsafeFlags(["-L", "cmake-build"]),
                .unsafeFlags(["-L", "\(brewPrefix)/lib"], .when(platforms: [.macOS])),
                .unsafeFlags(["-L", "\(brewPrefix)/opt/ffmpeg/lib"], .when(platforms: [.macOS])),
                .unsafeFlags(["-Xlinker", "-rpath", "-Xlinker", "\(brewPrefix)/lib"], .when(platforms: [.macOS])),
                .linkedLibrary("automix"),
                .linkedLibrary("avformat"),
                .linkedLibrary("avcodec"),
                .linkedLibrary("avutil"),
                .linkedLibrary("swresample"),
                .linkedLibrary("sqlite3"),
                .linkedLibrary("essentia"),
                .linkedLibrary("rubberband"),
                .linkedLibrary("chromaprint"),
                .linkedLibrary("fftw3f"),
                .linkedLibrary("samplerate"),
                .linkedLibrary("yaml"),
                .linkedLibrary("tag"),
                .linkedLibrary("c++"),
                .linkedFramework("CoreAudio"),
                .linkedFramework("AudioToolbox"),
                .linkedFramework("CoreFoundation")
            ]
        ),
        .executableTarget(
            name: "AutomixDemo",
            dependencies: ["Automix"],
            path: "apple/Examples/AutomixDemo"
        ),
        .testTarget(
            name: "AutomixTests",
            dependencies: ["Automix"],
            path: "apple/Tests/AutomixTests"
        ),
    ]
)
