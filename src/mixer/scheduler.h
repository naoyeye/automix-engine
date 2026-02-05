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
 */
class Scheduler {
public:
    Scheduler();
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
     * Called from audio thread.
     */
    int render(float* output, int frames, int sample_rate);
    
    /**
     * Set transition configuration.
     */
    void set_transition_config(const TransitionConfig& config);
    
private:
    // Update scheduler state (called from render)
    void update(int frames, int sample_rate);
    
    // Load track into a deck
    bool load_track_to_deck(Deck& deck, int64_t track_id);
    
    // Start transition to next track
    void start_transition();
    
    // Notify status change
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
    
    // State
    std::atomic<PlaybackState> state_{PlaybackState::Stopped};
    TransitionConfig transition_config_;
    
    // Callbacks
    TrackLoadCallback track_loader_;
    StatusCallback status_callback_;
    
    // Transition state
    bool transitioning_{false};
    float transition_start_position_{0};
};

} // namespace automix

#endif // AUTOMIX_SCHEDULER_H
