/**
 * AutoMix Engine - Playback Scheduler
 */

#ifndef AUTOMIX_SCHEDULER_H
#define AUTOMIX_SCHEDULER_H

#include "automix/types.h"
#include "deck.h"
#include "crossfader.h"
#include <functional>
#include <memory>
#include <atomic>
#include <vector>

namespace automix {

class Store;
class Decoder;

/**
 * Callback for loading tracks.
 */
using TrackLoadCallback = std::function<Result<AudioBuffer>(int64_t track_id)>;

/**
 * Status callback for playback events.
 */
using StatusCallback = std::function<void(
    PlaybackState state,
    int64_t current_track_id,
    float position,
    int64_t next_track_id
)>;

/**
 * Scheduler manages playlist playback and automatic transitions.
 *
 * Thread model:
 *   - render() is called from the real-time audio thread.
 *     It must NOT allocate memory, do I/O, or call user callbacks.
 *   - poll() is called from the control thread (e.g. a timer or main loop).
 *     It handles track loading, status callbacks, and other non-RT work.
 *   - Atomic flags bridge the two threads.
 */
class Scheduler {
public:
    /**
     * @param max_buffer_frames  Maximum frames per render() call.
     *        Pre-allocates internal mix buffers to this size.
     */
    explicit Scheduler(int max_buffer_frames = 4096);
    ~Scheduler();
    
    /**
     * Set the track loader callback.
     */
    void set_track_loader(TrackLoadCallback loader);
    
    /**
     * Set status callback.
     */
    void set_status_callback(StatusCallback callback);
    
    /**
     * Load a playlist for playback.
     */
    bool load_playlist(const Playlist& playlist);
    
    /**
     * Start playback.
     */
    void play();
    
    /**
     * Pause playback.
     */
    void pause();
    
    /**
     * Resume playback.
     */
    void resume();
    
    /**
     * Stop playback.
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
    PlaybackState state() const { return state_; }
    
    /**
     * Get current playback position.
     */
    float position() const;
    
    /**
     * Get currently playing track ID.
     */
    int64_t current_track_id() const;
    
    /**
     * Get next track ID.
     */
    int64_t next_track_id() const;
    
    /**
     * Render audio frames.
     * Called from audio thread — real-time safe (no alloc / no I/O).
     */
    int render(float* output, int frames, int sample_rate);
    
    /**
     * Poll for non-real-time work.
     * Call from the control / main thread periodically (e.g. every 10-50 ms).
     * Handles: pre-loading next track, firing status callbacks, completing
     * deck swaps after a transition finishes.
     */
    void poll();
    
    /**
     * Set transition configuration.
     */
    void set_transition_config(const TransitionConfig& config);
    
    /**
     * Set the sample rate used for transition frame calculations.
     */
    void set_sample_rate(int sample_rate);
    
private:
    // --- Audio-thread helpers (called only from render) ---
    void rt_update(int frames);
    
    // --- Control-thread helpers (called only from poll / control methods) ---
    bool load_track_to_deck(Deck& deck, int64_t track_id);
    void start_transition();
    void notify_status();
    
    // Decks
    std::unique_ptr<Deck> deck_a_;
    std::unique_ptr<Deck> deck_b_;
    Deck* active_deck_{nullptr};
    Deck* next_deck_{nullptr};
    
    // Crossfader
    Crossfader crossfader_;
    
    // Playlist
    Playlist playlist_;
    size_t current_index_{0};
    
    // State (atomic — shared between threads)
    std::atomic<PlaybackState> state_{PlaybackState::Stopped};
    std::atomic<bool> transitioning_{false};
    
    // Atomic flags for audio-thread -> control-thread signalling
    std::atomic<bool> need_preload_next_{false};   // audio thread detected we need to preload
    std::atomic<bool> transition_finished_{false};  // crossfader automation done
    std::atomic<bool> playback_finished_{false};    // current track ended, no transition active
    std::atomic<bool> need_status_notify_{false};   // request status callback
    
    // Atomic flag for control-thread -> audio-thread
    std::atomic<bool> skip_requested_{false};
    
    // Configuration
    TransitionConfig transition_config_;
    int sample_rate_{44100};
    
    // Pre-allocated mix buffers (sized to max_buffer_frames * 2 channels)
    std::vector<float> buffer_a_;
    std::vector<float> buffer_b_;
    int max_buffer_frames_;
    
    // Callbacks
    TrackLoadCallback track_loader_;
    StatusCallback status_callback_;
    
    // Transition trigger position (set by audio thread for poll to use)
    std::atomic<bool> transition_trigger_pending_{false};
    float transition_start_position_{0};
};

} // namespace automix

#endif // AUTOMIX_SCHEDULER_H
