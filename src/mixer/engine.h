/**
 * AutoMix Engine - Main Engine Class
 */

#ifndef AUTOMIX_ENGINE_H
#define AUTOMIX_ENGINE_H

#include "automix/types.h"
#include "scheduler.h"
#include "audio_output.h"
#include "../core/store.h"
#include "../decoder/decoder.h"
#include "../analyzer/analyzer.h"
#include "../matcher/playlist.h"
#include <memory>
#include <string>
#include <functional>

namespace automix {

/**
 * Scan progress callback.
 */
using ScanCallback = std::function<void(const std::string& file, int processed, int total)>;

/**
 * Main AutoMix Engine class.
 * Coordinates all components: scanning, analysis, playlist generation, and playback.
 */
class Engine {
public:
    explicit Engine(const std::string& db_path);
    ~Engine();
    
    // Non-copyable
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    
    /**
     * Check if engine initialized successfully.
     */
    bool is_valid() const;
    
    /**
     * Get last error message.
     */
    const std::string& error() const { return last_error_; }
    
    /* ========================================================================
     * Library Management
     * ======================================================================== */
    
    /**
     * Scan a directory for music files.
     * @param music_dir Directory to scan
     * @param recursive Scan subdirectories
     * @param callback Optional progress callback
     * @return Number of tracks analyzed
     */
    int scan(const std::string& music_dir, bool recursive = true, ScanCallback callback = nullptr);
    
    /**
     * Get total track count in library.
     */
    int track_count() const;
    
    /**
     * Get track info by ID.
     */
    std::optional<TrackInfo> get_track(int64_t id);
    
    /**
     * Search tracks by path pattern.
     */
    std::vector<TrackInfo> search_tracks(const std::string& pattern);
    
    /**
     * Get all tracks.
     */
    std::vector<TrackInfo> get_all_tracks();
    
    /* ========================================================================
     * Playlist Generation
     * ======================================================================== */
    
    /**
     * Generate a playlist starting from a seed track.
     */
    Playlist generate_playlist(
        int64_t seed_track_id,
        int count,
        const PlaylistRules& rules = PlaylistRules()
    );
    
    /**
     * Create a playlist from a list of track IDs.
     */
    Playlist create_playlist(const std::vector<int64_t>& track_ids);
    
    /* ========================================================================
     * Playback Control
     * ======================================================================== */
    
    /**
     * Load and start playing a playlist.
     * If audio output is available, starts it automatically.
     */
    bool play(const Playlist& playlist);
    
    /**
     * Pause playback.
     */
    void pause();
    
    /**
     * Resume playback.
     */
    void resume();
    
    /**
     * Stop playback and audio output.
     */
    void stop();
    
    /**
     * Skip to next track.
     */
    void skip();
    
    /**
     * Seek within current track.
     */
    void seek(float position);
    
    /**
     * Get current playback state.
     */
    PlaybackState playback_state() const;
    
    /**
     * Get current playback position.
     */
    float playback_position() const;
    
    /**
     * Get currently playing track ID.
     */
    int64_t current_track_id() const;
    
    /**
     * Set status callback.
     */
    void set_status_callback(StatusCallback callback);
    
    /**
     * Set transition configuration.
     */
    void set_transition_config(const TransitionConfig& config);
    
    /* ========================================================================
     * Audio Rendering
     * ======================================================================== */
    
    /**
     * Render audio frames.
     * Call this from your audio callback (pull mode).
     * If using start_audio() / stop_audio(), this is called automatically.
     * 
     * @param buffer Output buffer (interleaved stereo float32)
     * @param frames Number of frames to render
     * @return Number of frames rendered
     */
    int render(float* buffer, int frames);
    
    /**
     * Start platform audio output (CoreAudio on macOS).
     * The engine will drive the render loop automatically.
     * @return true if audio output started successfully.
     */
    bool start_audio();
    
    /**
     * Stop platform audio output.
     */
    void stop_audio();
    
    /**
     * Check if platform audio output is running.
     */
    bool is_audio_running() const;
    
    /**
     * Poll for non-real-time work.
     * Call this from your main / control thread periodically (e.g. every 20ms).
     * Handles track pre-loading, transition completion, and status callbacks.
     */
    void poll();
    
    /**
     * Get sample rate.
     */
    int sample_rate() const { return sample_rate_; }
    
    /**
     * Get number of channels.
     */
    int channels() const { return 2; }
    
private:
    // Track loader callback for scheduler
    Result<AudioBuffer> load_track_audio(int64_t track_id);
    
    std::unique_ptr<Store> store_;
    std::unique_ptr<Decoder> decoder_;
    std::unique_ptr<Analyzer> analyzer_;
    std::unique_ptr<PlaylistGenerator> playlist_generator_;
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<AudioOutput> audio_output_;
    
    TransitionConfig transition_config_;
    std::string last_error_;
    int sample_rate_{44100};
};

} // namespace automix

#endif // AUTOMIX_ENGINE_H
