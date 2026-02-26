// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

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
