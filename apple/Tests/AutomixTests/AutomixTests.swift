import XCTest
@testable import Automix

final class AutomixTests: XCTestCase {
    func testAutomixModuleImports() throws {
        // Verify that the Automix module is importable and core types are accessible.
        _ = AutoMixEngine.self
        _ = AutoMixError.self
        _ = AutoMixPlaybackState.self
        _ = TrackInfo.self
        _ = PlaylistRules.self
        _ = TransitionConfig.self
        _ = AutoMixPlaylist.self
    }

    func testAutoMixErrorCases() throws {
        let errors: [AutoMixError] = [
            .invalidArgument, .fileNotFound, .decodeFailed, .analysisFailed,
            .databaseError, .playbackError, .outOfMemory, .notInitialized, .unknown(99)
        ]
        XCTAssertEqual(errors.count, 9)
    }

    func testAutoMixPlaybackStateCases() throws {
        XCTAssertEqual(AutoMixPlaybackState.stopped.rawValue, 0)
        XCTAssertEqual(AutoMixPlaybackState.playing.rawValue, 1)
        XCTAssertEqual(AutoMixPlaybackState.paused.rawValue, 2)
        XCTAssertEqual(AutoMixPlaybackState.transitioning.rawValue, 3)
    }

    func testPlaylistRulesDefaults() throws {
        let rules = PlaylistRules()
        XCTAssertEqual(rules.bpmTolerance, 0.0)
        XCTAssertTrue(rules.allowKeyChange)
        XCTAssertEqual(rules.maxKeyDistance, 0)
        XCTAssertEqual(rules.minEnergyMatch, 0.0)
        XCTAssertNil(rules.styleFilter)
        XCTAssertTrue(rules.allowCrossStyle)
        XCTAssertEqual(rules.randomSeed, 0)
    }

    func testTransitionConfigDefaults() throws {
        let config = TransitionConfig()
        XCTAssertEqual(config.crossfadeBeats, 16.0)
        XCTAssertFalse(config.useEqSwap)
        XCTAssertEqual(config.stretchLimit, 0.06, accuracy: 0.001)
        XCTAssertEqual(config.stretchRecoverySeconds, 5.0)
    }

    func testTrackInfoInit() throws {
        let info = TrackInfo(id: 42, path: "/music/track.mp3", bpm: 128.0, key: "8A", duration: 300.0, analyzedAt: 1_700_000_000)
        XCTAssertEqual(info.id, 42)
        XCTAssertEqual(info.path, "/music/track.mp3")
        XCTAssertEqual(info.bpm, 128.0)
        XCTAssertEqual(info.key, "8A")
        XCTAssertEqual(info.duration, 300.0)
        XCTAssertEqual(info.analyzedAt, 1_700_000_000)
    }

    func testAutoMixErrorFromCode() throws {
        XCTAssertEqual(AutoMixError.from(code: -1), .invalidArgument)
        XCTAssertEqual(AutoMixError.from(code: -2), .fileNotFound)
        XCTAssertEqual(AutoMixError.from(code: -3), .decodeFailed)
        XCTAssertEqual(AutoMixError.from(code: -4), .analysisFailed)
        XCTAssertEqual(AutoMixError.from(code: -5), .databaseError)
        XCTAssertEqual(AutoMixError.from(code: -6), .playbackError)
        XCTAssertEqual(AutoMixError.from(code: -7), .outOfMemory)
        XCTAssertEqual(AutoMixError.from(code: -8), .notInitialized)
        if case .unknown(-99) = AutoMixError.from(code: -99) { } else {
            XCTFail("Expected unknown(-99)")
        }
    }

    /// End-to-end: Engine creation, trackInfo(id:) returns nil for non-existent track, searchTracks returns empty.
    /// Verifies the C→Swift type bridge works without crashing.
    func testTrackInfoAndSearchTracksBridge() throws {
        let dbPath = FileManager.default.temporaryDirectory
            .appendingPathComponent("automix_test_\(UUID().uuidString).db")
            .path
        defer { try? FileManager.default.removeItem(atPath: dbPath) }

        let engine = try AutoMixEngine(dbPath: dbPath)

        let info = try engine.trackInfo(id: 1)
        XCTAssertNil(info, "Empty DB should return nil for trackInfo")

        let ids = try engine.searchTracks(pattern: "%")
        XCTAssertTrue(ids.isEmpty, "Empty DB should return empty search results")

        XCTAssertEqual(engine.trackCount(), 0)
    }
}
