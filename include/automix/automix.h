/**
 * AutoMix Engine - Public C API
 * 
 * A library for automatic DJ-style music mixing with intelligent
 * track analysis, playlist generation, and seamless transitions.
 */

#ifndef AUTOMIX_H
#define AUTOMIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Types
 * ============================================================================ */

typedef struct AutoMixEngine AutoMixEngine;
typedef struct PlaylistHandleImpl* PlaylistHandle;

typedef enum {
    AUTOMIX_OK = 0,
    AUTOMIX_ERROR_INVALID_ARGUMENT = -1,
    AUTOMIX_ERROR_FILE_NOT_FOUND = -2,
    AUTOMIX_ERROR_DECODE_FAILED = -3,
    AUTOMIX_ERROR_ANALYSIS_FAILED = -4,
    AUTOMIX_ERROR_DATABASE_ERROR = -5,
    AUTOMIX_ERROR_PLAYBACK_ERROR = -6,
    AUTOMIX_ERROR_OUT_OF_MEMORY = -7,
    AUTOMIX_ERROR_NOT_INITIALIZED = -8,
    AUTOMIX_ERROR_TRANSITIONING = -9,
} AutoMixError;

typedef enum {
    AUTOMIX_STATE_STOPPED = 0,
    AUTOMIX_STATE_PLAYING = 1,
    AUTOMIX_STATE_PAUSED = 2,
    AUTOMIX_STATE_TRANSITIONING = 3,
} AutoMixPlaybackState;

/* Track information */
typedef struct {
    int64_t id;
    const char* path;
    float bpm;
    const char* key;        /* Camelot notation, e.g. "8A" */
    float duration;         /* seconds */
    int64_t analyzed_at;    /* Unix timestamp */
} AutoMixTrackInfo;

/* Track metadata */
typedef struct {
    int64_t track_id;
    const char* title;
    const char* artist;
    const char* album;
    const char* artwork_url;
    const uint8_t* artwork_data;
    int artwork_data_size;
    const char* source;
    int64_t fetched_at;
} AutoMixTrackMetadata;

/* Playlist generation rules */
typedef struct {
    float bpm_tolerance;        /* Max BPM difference (0.0 = any) */
    int allow_key_change;       /* Allow different keys */
    int max_key_distance;       /* Max Camelot wheel distance (0 = any) */
    float min_energy_match;     /* Minimum energy similarity (0.0-1.0) */
    const char** style_filter;  /* NULL-terminated array of styles, or NULL for any */
    int allow_cross_style;      /* Allow mixing different styles */
    uint32_t random_seed;       /* Random seed for reproducible playlists (0 = non-deterministic) */
} AutoMixPlaylistRules;

/* Transition configuration */
typedef struct {
    float crossfade_beats;      /* Number of beats for crossfade (default: 16) */
    int use_eq_swap;            /* Use EQ-based transition */
    float stretch_limit;        /* Max time-stretch ratio (e.g., 0.06 for ±6%) */
    float stretch_recovery_seconds; /* Seconds to smoothly return stretch to 1.0 after transition */
    int enable_transitions;     /* 1 = crossfade, 0 = hard cut (default: 1) */
} AutoMixTransitionConfig;

/**
 * Return an AutoMixTransitionConfig populated with library defaults.
 * Prefer this over zero-initialisation to ensure enable_transitions=1.
 */
AutoMixTransitionConfig automix_transition_config_default(void);

/* Playback status callback */
typedef void (*AutoMixStatusCallback)(
    AutoMixPlaybackState state,
    int64_t current_track_id,
    float position_seconds,
    int64_t next_track_id,
    void* user_data
);

/* ============================================================================
 * Engine Lifecycle
 * ============================================================================ */

/**
 * Create a new AutoMix engine instance.
 * 
 * @param db_path Path to SQLite database file (will be created if not exists)
 * @return Engine instance, or NULL on failure
 */
AutoMixEngine* automix_create(const char* db_path);

/**
 * Destroy an engine instance and free all resources.
 */
void automix_destroy(AutoMixEngine* engine);

/**
 * Get the last error message.
 */
const char* automix_get_error(AutoMixEngine* engine);

/* ============================================================================
 * Library Scanning
 * ============================================================================ */

/**
 * Scan a directory for music files and analyze them.
 * This is a blocking operation that may take a long time for large libraries.
 * 
 * @param engine Engine instance
 * @param music_dir Path to directory containing music files
 * @param recursive Scan subdirectories
 * @return Number of tracks processed, or negative error code
 */
int automix_scan(AutoMixEngine* engine, const char* music_dir, int recursive);

/**
 * Scan a directory for music files, with optional metadata-only mode.
 * In metadata-only mode only path/duration/mtime are collected; BPM/key
 * analysis is skipped.  A later call with metadata_only=0 will still perform
 * full analysis on any track whose analysis is incomplete.
 * 
 * @param engine Engine instance
 * @param music_dir Path to directory containing music files
 * @param recursive Scan subdirectories
 * @param metadata_only If non-zero, only collect path/duration/mtime, skip BPM/key analysis
 * @return Number of tracks processed, or negative error code
 */
int automix_scan_ex(AutoMixEngine* engine, const char* music_dir, int recursive, int metadata_only);

/**
 * Scan progress callback type.
 */
typedef void (*AutoMixScanCallback)(
    const char* current_file,
    int files_processed,
    int files_total,
    void* user_data
);

/**
 * Scan with progress callback.
 */
int automix_scan_with_callback(
    AutoMixEngine* engine,
    const char* music_dir,
    int recursive,
    AutoMixScanCallback callback,
    void* user_data
);

/**
 * Scan with progress callback, with optional metadata-only mode.
 * See automix_scan_ex() for the metadata_only semantics.
 */
int automix_scan_with_callback_ex(
    AutoMixEngine* engine,
    const char* music_dir,
    int recursive,
    AutoMixScanCallback callback,
    void* user_data,
    int metadata_only
);

/**
 * Get the number of tracks in the library.
 */
int automix_get_track_count(AutoMixEngine* engine);

/**
 * Get track information by ID.
 * 
 * @param engine Engine instance
 * @param track_id Track ID
 * @param info Output parameter for track info (caller must free path and key)
 * @return AUTOMIX_OK on success
 */
AutoMixError automix_get_track_info(
    AutoMixEngine* engine,
    int64_t track_id,
    AutoMixTrackInfo* info
);

/**
 * Search tracks by path pattern.
 *
 * On success:
 *   - If *out_count > 0, *out_ids will point to a dynamically allocated
 *     array of track IDs, and the caller must free it with
 *     automix_free_track_ids().
 *   - If *out_count == 0, *out_ids will be set to NULL and there is
 *     nothing to free.
 *
 * On error (return value != AUTOMIX_OK), *out_ids is set to NULL and
 * *out_count is set to 0; in this case the caller must not call
 * automix_free_track_ids() on *out_ids.
 * 
 * @param engine Engine instance
 * @param pattern SQL LIKE pattern (e.g., "%artist%")
 * @param out_ids Output array of track IDs
 * @param out_count Output number of results
 * @return AUTOMIX_OK on success
 */
AutoMixError automix_search_tracks(
    AutoMixEngine* engine,
    const char* pattern,
    int64_t** out_ids,
    int* out_count
);

/**
 * Get metadata for a track.
 * 
 * @param engine Engine instance
 * @param track_id Track ID
 * @param out_metadata Output parameter for metadata. Caller MUST free string fields and artwork_data with automix_free_track_metadata().
 * @return AUTOMIX_OK on success, or AUTOMIX_ERROR_FILE_NOT_FOUND if not found.
 */
AutoMixError automix_get_track_metadata(
    AutoMixEngine* engine,
    int64_t track_id,
    AutoMixTrackMetadata* out_metadata
);

/**
 * Set metadata for a track.
 * 
 * @param engine Engine instance
 * @param metadata The metadata to save.
 * @return AUTOMIX_OK on success.
 */
AutoMixError automix_set_track_metadata(
    AutoMixEngine* engine,
    const AutoMixTrackMetadata* metadata
);

/**
 * Free track metadata allocated by automix_get_track_metadata.
 */
void automix_free_track_metadata(AutoMixTrackMetadata* metadata);

/* ============================================================================
 * Playlist Generation
 * ============================================================================ */

/**
 * Generate a playlist starting from a seed track.
 * 
 * @param engine Engine instance
 * @param seed_track_id Starting track ID
 * @param count Number of tracks to include
 * @param rules Playlist generation rules (NULL for defaults)
 * @return Playlist handle, or NULL on failure
 */
PlaylistHandle automix_generate_playlist(
    AutoMixEngine* engine,
    int64_t seed_track_id,
    int count,
    const AutoMixPlaylistRules* rules
);

/**
 * Create a playlist from an explicit list of track IDs.
 * The engine will calculate optimal transitions between each pair.
 * 
 * @param engine Engine instance
 * @param track_ids Array of track IDs
 * @param count Number of tracks
 * @return Playlist handle, or NULL on failure
 */
PlaylistHandle automix_create_playlist(
    AutoMixEngine* engine,
    const int64_t* track_ids,
    int count
);

/**
 * Get the track IDs in a playlist.
 * Caller must free the returned array with automix_free_track_ids().
 */
AutoMixError automix_playlist_get_tracks(
    PlaylistHandle playlist,
    int64_t** out_ids,
    int* out_count
);

/**
 * Free track ID array returned by automix_search_tracks or automix_playlist_get_tracks.
 */
void automix_free_track_ids(int64_t* ids);

/**
 * Free a playlist handle.
 */
void automix_playlist_free(PlaylistHandle playlist);

/* ============================================================================
 * Playback Control
 * ============================================================================ */

/**
 * Start playing a playlist.
 */
AutoMixError automix_play(AutoMixEngine* engine, PlaylistHandle playlist);

/**
 * Pause playback.
 */
AutoMixError automix_pause(AutoMixEngine* engine);

/**
 * Resume playback.
 */
AutoMixError automix_resume(AutoMixEngine* engine);

/**
 * Stop playback.
 */
AutoMixError automix_stop(AutoMixEngine* engine);

/**
 * Skip to next track (triggers immediate transition).
 */
AutoMixError automix_skip(AutoMixEngine* engine);

/**
 * Go to previous track (or restart current track from beginning if at first track).
 */
AutoMixError automix_previous(AutoMixEngine* engine);

/**
 * Seek within current track.
 */
AutoMixError automix_seek(AutoMixEngine* engine, float position_seconds);

/**
 * Get current playback state.
 */
AutoMixPlaybackState automix_get_state(AutoMixEngine* engine);

/**
 * Get current playback position in seconds.
 */
float automix_get_position(AutoMixEngine* engine);

/**
 * Get currently playing track ID.
 */
int64_t automix_get_current_track(AutoMixEngine* engine);

/**
 * Set status callback for playback events.
 */
void automix_set_status_callback(
    AutoMixEngine* engine,
    AutoMixStatusCallback callback,
    void* user_data
);

/**
 * Set transition configuration.
 */
void automix_set_transition_config(
    AutoMixEngine* engine,
    const AutoMixTransitionConfig* config
);

/* ============================================================================
 * Audio Rendering (for custom audio output)
 * ============================================================================ */

/**
 * Render audio frames to a buffer.
 * Use this when integrating with your own audio output system.
 * 
 * @param engine Engine instance
 * @param buffer Output buffer (interleaved stereo float32)
 * @param frames Number of frames to render
 * @return Number of frames actually rendered
 */
int automix_render(AutoMixEngine* engine, float* buffer, int frames);

/**
 * Poll for non-real-time work (track loading, status callbacks, etc.).
 * Call this from your main / control thread periodically (e.g. every 20ms).
 * Required when using automix_render() in a separate audio callback.
 */
void automix_poll(AutoMixEngine* engine);

/**
 * Start platform audio output (CoreAudio on macOS).
 * The engine will drive the render loop automatically.
 * @return AUTOMIX_OK on success.
 */
AutoMixError automix_start_audio(AutoMixEngine* engine);

/**
 * Stop platform audio output.
 */
void automix_stop_audio(AutoMixEngine* engine);

/**
 * Get the sample rate used by the engine.
 */
int automix_get_sample_rate(AutoMixEngine* engine);

/**
 * Get the number of channels (always 2 for stereo).
 */
int automix_get_channels(AutoMixEngine* engine);

#ifdef __cplusplus
}
#endif

#endif /* AUTOMIX_H */
