//
//  AutoMixEngine.swift
//  Automix
//
//  Created by Swift API Design Plan.
//

import Foundation
import CAutomix
import Combine

/// Snapshot of the current playback status of the AutoMix engine.
///
/// Instances of this type are produced by the underlying C `AutoMixEngine` via
/// `automix_set_status_callback` and are emitted to clients through
/// ``AutoMixEngine/statusPublisher`` and ``AutoMixEngine/statusStream`` whenever
/// the playback state changes, the playback position advances, or the current/next
/// track changes.
public struct AutoMixStatus {
    /// The current high-level playback state of the engine (for example, playing or paused).
    public let state: AutoMixPlaybackState
    /// Identifier of the track that is currently playing.
    ///
    /// A value of `0` indicates no track is active (matches C engine semantics).
    public let currentTrackId: Int64
    /// Current playback position within the active track, in seconds.
    public let position: Float
    /// Identifier of the track scheduled to play next.
    public let nextTrackId: Int64
}

public struct AutoMixTrackMetadata {
    public var title: String?
    public var artist: String?
    public var album: String?
    public var artworkData: Data?
    public var source: String?
    public var fetchedAt: Int64
    
    public init(title: String? = nil, artist: String? = nil, album: String? = nil, artworkData: Data? = nil, source: String? = nil, fetchedAt: Int64 = 0) {
        self.title = title
        self.artist = artist
        self.album = album
        self.artworkData = artworkData
        self.source = source
        self.fetchedAt = fetchedAt
    }
}

/// The core engine class of the Swift wrapper. Manages the lifecycle of the underlying
/// C `AutoMixEngine` instance, library scanning, playlist generation, and audio control.
public class AutoMixEngine {
    
    // MARK: - Internal Properties
    
    /// Pointer to the C engine instance; its lifecycle is controlled by this Swift class.
    internal var enginePtr: OpaquePointer?
    
    // MARK: - Public Properties (Reactive)
    
    /// Combine publisher for engine status updates.
    public let statusPublisher = PassthroughSubject<AutoMixStatus, Never>()
    
    /// AsyncStream for engine status updates.
    private var statusContinuation: AsyncStream<AutoMixStatus>.Continuation?
    public lazy var statusStream: AsyncStream<AutoMixStatus> = {
        AsyncStream { continuation in
            self.statusContinuation = continuation
        }
    }()
    
    // MARK: - Initialization and Deinitialization
    
    /// Initializes an AutoMix engine instance by calling the underlying `automix_create`.
    /// - Parameter dbPath: Path to the SQLite database file; will be created if it does not exist.
    /// - Throws: `AutoMixError.notInitialized` if engine creation fails.
    public init(dbPath: String) throws {
        self.enginePtr = automix_create(dbPath)
        guard self.enginePtr != nil else {
            throw AutoMixError.notInitialized
        }
        
        // Setup status callback
        let selfPtr = Unmanaged.passUnretained(self).toOpaque()
        let callback: AutoMixStatusCallback = { state, currentTrackId, position, nextTrackId, userData in
             guard let userData = userData else { return }
             let engine = Unmanaged<AutoMixEngine>.fromOpaque(userData).takeUnretainedValue()
             engine.handleStatusUpdate(state: state, currentTrackId: currentTrackId, position: position, nextTrackId: nextTrackId)
        }
        automix_set_status_callback(self.enginePtr, callback, selfPtr)
    }
    
    /// Calls the underlying `automix_destroy` to release resources when the engine is deallocated.
    deinit {
        statusContinuation?.finish()
        if let ptr = enginePtr {
            automix_destroy(ptr)
        }
        #if DEBUG
        print("AutoMixEngine deinitialized: engine destroyed")
        #endif
    }
    
    // MARK: - Internal Callback Handlers
    
    fileprivate func handleStatusUpdate(state: CAutomix.AutoMixPlaybackState, currentTrackId: Int64, position: Float, nextTrackId: Int64) {
        // Convert C enum to Swift enum
        let swiftState = AutoMixPlaybackState(rawValue: Int(state.rawValue)) ?? .stopped
        
        let status = AutoMixStatus(
            state: swiftState,
            currentTrackId: currentTrackId,
            position: position,
            nextTrackId: nextTrackId
        )
        
        // Dispatch to main thread for UI updates if needed, or keep on background.
        // For Combine/AsyncStream, it's often safer to let the consumer decide dispatching,
        // but for UI-heavy apps, publishing on MainActor might be preferred.
        // Here we publish directly.
        
        statusPublisher.send(status)
        statusContinuation?.yield(status)
    }
    
    // MARK: - Library Scanning
    
    /// Scans a directory for music files and analyzes them. This is a blocking operation.
    /// Wraps `automix_scan` and `automix_scan_with_callback`.
    ///
    /// - Parameters:
    ///   - musicDir: Path to the directory containing music files.
    ///   - recursive: Whether to scan subdirectories recursively.
    ///   - progress: Optional callback closure with parameters (current file path, files processed, total files).
    ///               If omitted, the no-callback variant is called.
    ///   - metadataOnly: When true, only collect path/duration/mtime without BPM/key analysis.
    /// - Returns: The number of tracks successfully processed.
    /// - Throws: An `AutoMixError` if scanning fails or a database error occurs.
    public func scan(musicDir: String, recursive: Bool, progress: ((String, Int, Int) -> Void)? = nil, metadataOnly: Bool = false) throws -> Int {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        
        if let progress = progress {
            let context = ScanContext(callback: progress)
            let contextPtr = Unmanaged.passRetained(context).toOpaque()
            
            defer {
                Unmanaged<ScanContext>.fromOpaque(contextPtr).release()
            }
            
            let callback: AutoMixScanCallback = { currentFile, filesProcessed, filesTotal, userData in
                guard let userData = userData else { return }
                let context = Unmanaged<ScanContext>.fromOpaque(userData).takeUnretainedValue()
                let filePath = currentFile.map { String(cString: $0) } ?? ""
                context.callback(filePath, Int(filesProcessed), Int(filesTotal))
            }
            
            let result = automix_scan_with_callback_ex(
                engine,
                musicDir,
                recursive ? 1 : 0,
                callback,
                contextPtr,
                metadataOnly ? 1 : 0
            )
            
            if result < 0 {
                throw AutoMixError.from(code: Int32(result))
            }
            return Int(result)
        } else {
            let result = automix_scan_ex(engine, musicDir, recursive ? 1 : 0, metadataOnly ? 1 : 0)
            if result < 0 {
                throw AutoMixError.from(code: Int32(result))
            }
            return Int(result)
        }
    }
    
    /// Async version of scan without progress reporting.
    public func scan(musicDir: String, recursive: Bool, metadataOnly: Bool = false) async throws -> Int {
        return try await Task.detached(priority: .userInitiated) {
            try self.scan(musicDir: musicDir, recursive: recursive, progress: nil, metadataOnly: metadataOnly)
        }.value
    }
    
    /// Async version of scan with a progress stream.
    ///
    /// Yields `(currentFilePath, filesProcessed, totalFiles)` tuples as scanning progresses.
    /// Supports cooperative cancellation: finishing or cancelling the iteration stops the background scan.
    public func scanProgressStream(musicDir: String, recursive: Bool, metadataOnly: Bool = false) -> AsyncThrowingStream<(String, Int, Int), Error> {
        return AsyncThrowingStream { continuation in
            var workItem: DispatchWorkItem!
            workItem = DispatchWorkItem {
                if workItem.isCancelled {
                    continuation.finish()
                    return
                }
                do {
                    _ = try self.scan(musicDir: musicDir, recursive: recursive, progress: { file, processed, total in
                        if workItem.isCancelled { return }
                        continuation.yield((file, processed, total))
                    }, metadataOnly: metadataOnly)
                    if !workItem.isCancelled {
                        continuation.finish()
                    }
                } catch {
                    if !workItem.isCancelled {
                        continuation.finish(throwing: error)
                    }
                }
            }
            continuation.onTermination = { @Sendable _ in
                workItem.cancel()
            }
            DispatchQueue.global(qos: .userInitiated).async(execute: workItem)
        }
    }
    
    // MARK: - Track Retrieval
    
    public func getTrackMetadata(trackId: Int64) throws -> AutoMixTrackMetadata {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        var cMetadata = CAutomix.AutoMixTrackMetadata()
        let result = automix_get_track_metadata(engine, trackId, &cMetadata)
        
        if result == AUTOMIX_ERROR_FILE_NOT_FOUND {
            throw AutoMixError.fileNotFound
        } else if result != AUTOMIX_OK {
            throw AutoMixError.from(code: result.rawValue)
        }
        
        defer {
            automix_free_track_metadata(&cMetadata)
        }
        
        let title = cMetadata.title.map { String(cString: $0) }
        let artist = cMetadata.artist.map { String(cString: $0) }
        let album = cMetadata.album.map { String(cString: $0) }
        let source = cMetadata.source.map { String(cString: $0) }
        
        var artworkData: Data? = nil
        if let dataPtr = cMetadata.artwork_data, cMetadata.artwork_data_size > 0 {
            artworkData = Data(bytes: dataPtr, count: Int(cMetadata.artwork_data_size))
        }
        
        return AutoMixTrackMetadata(
            title: title,
            artist: artist,
            album: album,
            artworkData: artworkData,
            source: source,
            fetchedAt: cMetadata.fetched_at
        )
    }
    
    public func setTrackMetadata(trackId: Int64, metadata: AutoMixTrackMetadata) throws {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        
        var cMetadata = CAutomix.AutoMixTrackMetadata()
        cMetadata.track_id = trackId
        
        let titleCStr = metadata.title?.withCString { strdup($0) }
        let artistCStr = metadata.artist?.withCString { strdup($0) }
        let albumCStr = metadata.album?.withCString { strdup($0) }
        let sourceCStr = metadata.source?.withCString { strdup($0) }
        
        defer {
            free(titleCStr)
            free(artistCStr)
            free(albumCStr)
            free(sourceCStr)
        }
        
        cMetadata.title = UnsafePointer(titleCStr)
        cMetadata.artist = UnsafePointer(artistCStr)
        cMetadata.album = UnsafePointer(albumCStr)
        cMetadata.source = UnsafePointer(sourceCStr)
        cMetadata.fetched_at = metadata.fetchedAt
        
        var result: CAutomix.AutoMixError
        if let data = metadata.artworkData {
            result = data.withUnsafeBytes { buffer in
                cMetadata.artwork_data = buffer.bindMemory(to: UInt8.self).baseAddress
                cMetadata.artwork_data_size = Int32(data.count)
                return automix_set_track_metadata(engine, &cMetadata)
            }
        } else {
            cMetadata.artwork_data = nil
            cMetadata.artwork_data_size = 0
            result = automix_set_track_metadata(engine, &cMetadata)
        }
        
        if result != AUTOMIX_OK {
            throw AutoMixError.from(code: Int32(result.rawValue))
        }
    }
    
    /// Returns the number of tracks currently in the library.
    /// - Returns: The total number of analyzed tracks in the library.
    public func trackCount() -> Int {
        guard let engine = enginePtr else { return 0 }
        return Int(automix_get_track_count(engine))
    }
    
    /// Retrieves detailed information for a track by its ID.
    /// Wraps `automix_get_track_info`, converting the C struct to a Swift struct and managing string memory.
    /// - Parameter id: The ID of the track to query.
    /// - Returns: A `TrackInfo` struct if found, or `nil` if the track does not exist.
    /// - Throws: An `AutoMixError` if the underlying query fails.
    public func trackInfo(id: Int64) throws -> TrackInfo? {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        
        var cInfo = AutoMixTrackInfo()
        let result = automix_get_track_info(engine, id, &cInfo)
        
        if result == AUTOMIX_ERROR_FILE_NOT_FOUND {
            return nil
        }
        if result != AUTOMIX_OK {
            throw AutoMixError.from(code: result.rawValue)
        }
        
        defer {
            if let ptr = cInfo.path {
                free(UnsafeMutablePointer(mutating: ptr))
            }
            if let ptr = cInfo.key {
                free(UnsafeMutablePointer(mutating: ptr))
            }
        }
        
        let path = cInfo.path.map { String(cString: $0) } ?? ""
        let key = cInfo.key.map { String(cString: $0) } ?? ""
        
        return TrackInfo(
            id: cInfo.id,
            path: path,
            bpm: cInfo.bpm,
            key: key,
            duration: cInfo.duration,
            analyzedAt: cInfo.analyzed_at
        )
    }
    
    /// Searches for tracks matching the given pattern.
    /// Wraps `automix_search_tracks`.
    /// - Parameter pattern: A SQL LIKE query pattern (e.g., "%artist%").
    /// - Returns: An array of matching track IDs.
    /// - Throws: An `AutoMixError` if the search fails.
    public func searchTracks(pattern: String) throws -> [Int64] {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        
        var outIds: UnsafeMutablePointer<Int64>?
        var outCount: Int32 = 0
        
        let result = pattern.withCString { cPattern in
            automix_search_tracks(engine, cPattern, &outIds, &outCount)
        }
        
        guard result == AUTOMIX_OK else {
            throw AutoMixError.from(code: result.rawValue)
        }
        
        defer {
            if let ptr = outIds {
                automix_free_track_ids(ptr)
            }
        }
        
        guard let ids = outIds, outCount > 0 else {
            return []
        }
        
        let buffer = UnsafeBufferPointer(start: ids, count: Int(outCount))
        return Array(buffer)
    }
    
    // MARK: - Playlist Generation
    
    /// Generates a playlist based on a seed track and optional rules.
    /// - Parameters:
    ///   - seedTrackId: The ID of the starting track.
    ///   - count: The desired playlist length (including the seed track).
    ///   - rules: Optional generation rules.
    /// - Returns: A populated `AutoMixPlaylist` instance.
    /// - Throws: An `AutoMixError` if generation fails (e.g., seed track not found).
    public func generatePlaylist(seedTrackId: Int64, count: Int, rules: PlaylistRules? = nil) throws -> AutoMixPlaylist {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        
        let handle: PlaylistHandle?
        if let rules = rules {
            var cRules = AutoMixPlaylistRules(
                bpm_tolerance: rules.bpmTolerance,
                allow_key_change: rules.allowKeyChange ? 1 : 0,
                max_key_distance: Int32(rules.maxKeyDistance),
                min_energy_match: rules.minEnergyMatch,
                style_filter: nil,
                allow_cross_style: rules.allowCrossStyle ? 1 : 0,
                random_seed: rules.randomSeed
            )
            var cStyleStrings: [UnsafeMutablePointer<CChar>] = []
            defer { cStyleStrings.forEach { free($0) } }
            
            if let styleFilter = rules.styleFilter, !styleFilter.isEmpty {
                for s in styleFilter {
                    if let dup = s.withCString({ strdup($0) }) {
                        cStyleStrings.append(dup)
                    }
                }
                let styleFilterArray = cStyleStrings.map { UnsafePointer<CChar>($0) } + [nil]
                handle = styleFilterArray.withUnsafeBufferPointer { buf in
                    var rulesWithFilter = cRules
                    rulesWithFilter.style_filter = UnsafeMutablePointer(mutating: buf.baseAddress)
                    return automix_generate_playlist(engine, seedTrackId, Int32(count), &rulesWithFilter)
                }
            } else {
                handle = automix_generate_playlist(engine, seedTrackId, Int32(count), &cRules)
            }
        } else {
            handle = automix_generate_playlist(engine, seedTrackId, Int32(count), nil)
        }
        
        guard let h = handle else {
            throw AutoMixError.playbackError
        }
        return AutoMixPlaylist(handle: h)
    }
    
    /// Creates a playlist from an explicit list of track IDs.
    /// The engine will compute the best transitions between the specified tracks.
    /// - Parameter trackIds: An array of track IDs.
    /// - Returns: A populated `AutoMixPlaylist` instance.
    /// - Throws: An `AutoMixError` if creation fails.
    public func createPlaylist(trackIds: [Int64]) throws -> AutoMixPlaylist {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        guard !trackIds.isEmpty else { throw AutoMixError.invalidArgument }
        
        let handle = trackIds.withUnsafeBufferPointer { buf in
            automix_create_playlist(engine, buf.baseAddress, Int32(buf.count))
        }
        
        guard let h = handle else {
            throw AutoMixError.invalidArgument
        }
        return AutoMixPlaylist(handle: h)
    }
    
    // MARK: - Playback Control
    
    /// Starts playback of the provided playlist.
    /// - Parameter playlist: The `AutoMixPlaylist` to play.
    /// - Throws: An `AutoMixError` if playback fails to start.
    public func play(playlist: AutoMixPlaylist) throws {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        guard let handle = playlist.handle else { throw AutoMixError.invalidArgument }
        let result = automix_play(engine, handle)
        if result != AUTOMIX_OK {
            throw AutoMixError.from(code: result.rawValue)
        }
    }
    
    /// Pauses playback.
    public func pause() throws {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        let result = automix_pause(engine)
        if result != AUTOMIX_OK {
             throw AutoMixError.from(code: result.rawValue)
        }
    }
    
    /// Resumes playback.
    public func resume() throws {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        let result = automix_resume(engine)
        if result != AUTOMIX_OK {
             throw AutoMixError.from(code: result.rawValue)
        }
    }
    
    /// Stops playback completely.
    public func stop() throws {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        let result = automix_stop(engine)
        if result != AUTOMIX_OK {
             throw AutoMixError.from(code: result.rawValue)
        }
    }
    
    /// Skips to the next track, triggering an immediate transition.
    public func next() throws {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        let result = automix_skip(engine)
        if result != AUTOMIX_OK {
             throw AutoMixError.from(code: result.rawValue)
        }
    }
    
    /// Returns to the previous track. If on the first track, restarts it from the beginning.
    public func previous() throws {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        let result = automix_previous(engine)
        if result != AUTOMIX_OK {
             throw AutoMixError.from(code: result.rawValue)
        }
    }
    
    /// Seeks to a specific position within the currently playing track.
    /// - Parameter seconds: The target position in seconds.
    public func seek(seconds: Float) throws {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        let result = automix_seek(engine, seconds)
        if result != AUTOMIX_OK {
             throw AutoMixError.from(code: result.rawValue)
        }
    }
    
    /// Sets the transition configuration for crossfades and EQ.
    /// - Parameter config: The transition configuration to apply.
    public func setTransitionConfig(_ config: TransitionConfig) {
        guard let engine = enginePtr else { return }
        var cConfig = CAutomix.AutoMixTransitionConfig()
        cConfig.enable_transitions = config.enableTransitions ? 1 : 0
        cConfig.crossfade_beats = config.crossfadeBeats
        cConfig.use_eq_swap = config.useEqSwap ? 1 : 0
        cConfig.stretch_limit = config.stretchLimit
        cConfig.stretch_recovery_seconds = config.stretchRecoverySeconds
        automix_set_transition_config(engine, &cConfig)
    }
    
    // MARK: - State Queries
    
    /// The current playback state.
    public var state: AutoMixPlaybackState {
        guard let engine = enginePtr else { return .stopped }
        let cState = automix_get_state(engine)
        return AutoMixPlaybackState(rawValue: Int(cState.rawValue)) ?? .stopped
    }
    
    /// The current playback position in seconds.
    public var position: Float {
        guard let engine = enginePtr else { return 0.0 }
        return automix_get_position(engine)
    }
    
    /// The ID of the currently playing track.
    public var currentTrackId: Int64 {
        guard let engine = enginePtr else { return -1 }
        return automix_get_current_track(engine)
    }
    
    // MARK: - Audio Management
    
    /// Starts the underlying audio system for automatic output (e.g., CoreAudio).
    public func startAudio() throws {
        guard let engine = enginePtr else { throw AutoMixError.notInitialized }
        let result = automix_start_audio(engine)
        if result != AUTOMIX_OK {
            throw AutoMixError.from(code: result.rawValue)
        }
    }
    
    /// Stops the underlying audio system's automatic output.
    public func stopAudio() {
        guard let engine = enginePtr else { return }
        automix_stop_audio(engine)
    }
    
    /// Integrates with a custom audio render callback.
    /// - Parameters:
    ///   - buffer: Pointer to the output buffer.
    ///   - frames: Number of frames to render.
    /// - Returns: The number of frames actually rendered.
    public func render(buffer: UnsafeMutablePointer<Float>, frames: Int) -> Int {
        guard let engine = enginePtr else { return 0 }
        return Int(automix_render(engine, buffer, Int32(frames)))
    }
    
    /// Call periodically to process non-realtime tasks such as loading. Required when rendering audio manually.
    public func poll() {
        guard let engine = enginePtr else { return }
        automix_poll(engine)
    }
    
    /// The sample rate of the current audio system.
    public var sampleRate: Int {
        guard let engine = enginePtr else { return 44100 }
        return Int(automix_get_sample_rate(engine))
    }
    
    /// The number of channels in the current audio system (typically 2).
    public var channels: Int {
        guard let engine = enginePtr else { return 2 }
        return Int(automix_get_channels(engine))
    }
}

// MARK: - Private Trampolines & Contexts

private class ScanContext {
    let callback: (String, Int, Int) -> Void
    
    init(callback: @escaping (String, Int, Int) -> Void) {
        self.callback = callback
    }
}

