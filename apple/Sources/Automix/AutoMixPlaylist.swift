//
//  AutoMixPlaylist.swift
//  Automix
//
//  Created by Swift API Design Plan.
//

import Foundation
import CAutomix

/// Wraps a C API `PlaylistHandle` and manages its lifecycle.
public class AutoMixPlaylist {
    
    // MARK: - Internal Properties
    
    /// The playlist handle pointer allocated by the C engine.
    internal let handle: PlaylistHandle?
    
    // MARK: - Initialization and Lifecycle
    
    /// Internal initializer called by `AutoMixEngine` when generating a playlist.
    /// - Parameter handle: The `PlaylistHandle` returned by the C API.
    internal init(handle: PlaylistHandle) {
        self.handle = handle
    }
    
    /// Calls the underlying API to release memory resources when this object is deallocated.
    deinit {
        if let h = handle {
            automix_playlist_free(h)
        }
        #if DEBUG
        print("AutoMixPlaylist deinitialized: handle freed")
        #endif
    }
    
    // MARK: - Playlist Information
    
    /// Returns all track IDs in the current playlist.
    /// Wraps `automix_playlist_get_tracks`, handling memory allocation and conversion.
    /// - Returns: An array of track IDs.
    /// - Throws: An `AutoMixError` if the underlying fetch fails.
    public func getTrackIDs() throws -> [Int64] {
        guard let h = handle else { return [] }
        
        var outIds: UnsafeMutablePointer<Int64>?
        var outCount: Int32 = 0
        
        let result = automix_playlist_get_tracks(h, &outIds, &outCount)
        
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
}
