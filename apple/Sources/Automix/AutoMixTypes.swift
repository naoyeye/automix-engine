//
//  AutoMixTypes.swift
//  Automix
//
//  Created by Swift API Design Plan.
//

import Foundation

/// Error types that map to the C API's `AutoMixError` codes.
public enum AutoMixError: Error {
    case invalidArgument
    case fileNotFound
    case decodeFailed
    case analysisFailed
    case databaseError
    case playbackError
    case outOfMemory
    case notInitialized
    case unknown(Int)
    
    // TODO: Add a static factory method that maps a C Int32 code to this enum:
    // static func from(code: Int32) -> AutoMixError
}

/// Playback states that map to the C API's `AutoMixPlaybackState`.
public enum AutoMixPlaybackState: Int {
    case stopped = 0
    case playing = 1
    case paused = 2
    case transitioning = 3
}

/// Track metadata that maps to the C API's `AutoMixTrackInfo`.
public struct TrackInfo {
    public let id: Int64
    public let path: String
    public let bpm: Float
    /// Camelot notation key, e.g. "8A".
    public let key: String
    /// Duration in seconds.
    public let duration: Float
    /// Unix timestamp of when the track was analyzed.
    public let analyzedAt: Int64
    
    public init(id: Int64, path: String, bpm: Float, key: String, duration: Float, analyzedAt: Int64) {
        self.id = id
        self.path = path
        self.bpm = bpm
        self.key = key
        self.duration = duration
        self.analyzedAt = analyzedAt
    }
}

/// Playlist generation rules that map to the C API's `AutoMixPlaylistRules`.
public struct PlaylistRules {
    /// Maximum BPM difference allowed (0.0 = no limit).
    public var bpmTolerance: Float = 0.0
    /// Whether key changes are permitted.
    public var allowKeyChange: Bool = true
    /// Maximum Camelot wheel distance between tracks (0 = no limit).
    public var maxKeyDistance: Int = 0
    /// Minimum required energy similarity in the range 0.0–1.0.
    public var minEnergyMatch: Float = 0.0
    /// Style filter; `nil` means any style is accepted.
    public var styleFilter: [String]? = nil
    /// Whether tracks from different styles may be mixed.
    public var allowCrossStyle: Bool = true
    /// Random seed for reproducible playlists (0 = non-deterministic).
    public var randomSeed: UInt32 = 0
    
    public init() {}
}

/// Transition configuration that maps to the C API's `AutoMixTransitionConfig`.
public struct TransitionConfig {
    /// Number of beats for the crossfade (default: 16).
    public var crossfadeBeats: Float = 16.0
    /// Whether to use an EQ-based transition.
    public var useEqSwap: Bool = false
    /// Maximum time-stretch ratio (e.g. 0.06 means ±6%).
    public var stretchLimit: Float = 0.06
    /// Time in seconds to smoothly recover stretch back to 1.0 after a transition.
    public var stretchRecoverySeconds: Float = 5.0
    
    public init() {}
}
