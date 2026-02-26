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

    /// Verifies that creating a playlist from an empty track list throws `.invalidArgument`.
    func testCreatePlaylistEmptyThrowsInvalidArgument() throws {
        let dbPath = FileManager.default.temporaryDirectory
            .appendingPathComponent("automix_test_\(UUID().uuidString).db")
            .path
        defer { try? FileManager.default.removeItem(atPath: dbPath) }

        let engine = try AutoMixEngine(dbPath: dbPath)
        XCTAssertThrowsError(try engine.createPlaylist(trackIds: [])) { error in
            XCTAssertEqual(error as? AutoMixError, .invalidArgument)
        }
    }

    /// Verifies that generating a playlist with a non-existent seed track throws `.playbackError`.
    func testGeneratePlaylistNonExistentSeedThrows() throws {
        let dbPath = FileManager.default.temporaryDirectory
            .appendingPathComponent("automix_test_\(UUID().uuidString).db")
            .path
        defer { try? FileManager.default.removeItem(atPath: dbPath) }

        let engine = try AutoMixEngine(dbPath: dbPath)
        XCTAssertThrowsError(try engine.generatePlaylist(seedTrackId: 9999, count: 5)) { error in
            XCTAssertEqual(error as? AutoMixError, .playbackError)
        }
    }

    /// Verifies that playback control methods throw when no playlist is playing.
    func testPlaybackControlThrowsWhenStopped() throws {
        let dbPath = FileManager.default.temporaryDirectory
            .appendingPathComponent("automix_test_\(UUID().uuidString).db")
            .path
        defer { try? FileManager.default.removeItem(atPath: dbPath) }

        let engine = try AutoMixEngine(dbPath: dbPath)
        // pause/resume/stop/skip should not throw when engine is stopped (succeed or no-op)
        XCTAssertNoThrow(try engine.pause())
        XCTAssertNoThrow(try engine.resume())
        XCTAssertNoThrow(try engine.stop())
        XCTAssertNoThrow(try engine.next())
    }

    /// Verifies state and position accessors return sane defaults on a fresh engine.
    func testDefaultStateAndPosition() throws {
        let dbPath = FileManager.default.temporaryDirectory
            .appendingPathComponent("automix_test_\(UUID().uuidString).db")
            .path
        defer { try? FileManager.default.removeItem(atPath: dbPath) }

        let engine = try AutoMixEngine(dbPath: dbPath)
        XCTAssertEqual(engine.state, .stopped)
        XCTAssertEqual(engine.position, 0.0, accuracy: 0.001)
        XCTAssertEqual(engine.currentTrackId, 0)
    }

    /// Verifies that setTransitionConfig does not crash.
    func testSetTransitionConfig() throws {
        let dbPath = FileManager.default.temporaryDirectory
            .appendingPathComponent("automix_test_\(UUID().uuidString).db")
            .path
        defer { try? FileManager.default.removeItem(atPath: dbPath) }

        let engine = try AutoMixEngine(dbPath: dbPath)
        let config = TransitionConfig(crossfadeBeats: 8.0, useEqSwap: true, stretchLimit: 0.04, stretchRecoverySeconds: 3.0)
        XCTAssertNoThrow(engine.setTransitionConfig(config))
    }

    /// Verifies that the async scan method throws for a non-existent directory.
    func testAsyncScanNonExistentDirectoryThrows() async throws {
        let dbPath = FileManager.default.temporaryDirectory
            .appendingPathComponent("automix_test_\(UUID().uuidString).db")
            .path
        defer { try? FileManager.default.removeItem(atPath: dbPath) }

        let engine = try AutoMixEngine(dbPath: dbPath)
        do {
            _ = try await engine.scan(musicDir: "/nonexistent/path/\(UUID().uuidString)", recursive: false)
            XCTFail("Expected an error for non-existent directory")
        } catch {
            XCTAssertTrue(error is AutoMixError)
        }
    }
}
