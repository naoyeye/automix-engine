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
}
