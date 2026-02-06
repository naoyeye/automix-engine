/**
 * AutoMix Engine - Deck (Single Track Player)
 */

#ifndef AUTOMIX_DECK_H
#define AUTOMIX_DECK_H

#include "automix/types.h"
#include <memory>
#include <atomic>

namespace automix {

/**
 * A single deck that plays one track.
 * Supports playback, seeking, time-stretching, and 3-band EQ.
 */
class Deck {
public:
    Deck();
    ~Deck();
    
    // Non-copyable
    Deck(const Deck&) = delete;
    Deck& operator=(const Deck&) = delete;
    
    /**
     * Load audio data into the deck.
     */
    bool load(const AudioBuffer& audio, int64_t track_id = 0);
    
    /**
     * Unload current audio.
     */
    void unload();
    
    /**
     * Check if deck has audio loaded.
     */
    bool is_loaded() const { return loaded_; }
    
    /**
     * Get the loaded track ID.
     */
    int64_t track_id() const { return track_id_; }
    
    /**
     * Start playback.
     */
    void play();
    
    /**
     * Pause playback.
     */
    void pause();
    
    /**
     * Check if playing.
     */
    bool is_playing() const { return playing_; }
    
    /**
     * Seek to a position (in seconds).
     */
    void seek(float position);
    
    /**
     * Get current playback position (in seconds).
     */
    float position() const;
    
    /**
     * Get total duration (in seconds).
     */
    float duration() const;
    
    /**
     * Set playback speed/stretch ratio (1.0 = normal).
     * Uses time-stretch to maintain pitch.
     */
    void set_stretch_ratio(float ratio);
    
    /**
     * Get current stretch ratio.
     */
    float stretch_ratio() const { return stretch_ratio_; }
    
    /**
     * Set volume (0.0 to 1.0).
     * Volume changes are smoothed to prevent clicks.
     */
    void set_volume(float volume);
    
    /**
     * Get current volume.
     */
    float volume() const { return volume_; }
    
    /**
     * Set 3-band EQ gains.
     * @param low_db  Low band gain in dB  (0 = unity, -inf to +12)
     * @param mid_db  Mid band gain in dB
     * @param high_db High band gain in dB
     */
    void set_eq(float low_db, float mid_db, float high_db);
    
    /**
     * Get current EQ gains in dB.
     */
    void get_eq(float& low_db, float& mid_db, float& high_db) const;
    
    /**
     * Render audio frames to output buffer.
     * Called from audio thread.
     * 
     * @param output Output buffer (interleaved stereo)
     * @param frames Number of frames to render
     * @return Number of frames actually rendered
     */
    int render(float* output, int frames);
    
    /**
     * Check if playback has finished.
     */
    bool is_finished() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    std::atomic<bool> loaded_{false};
    std::atomic<bool> playing_{false};
    std::atomic<float> volume_{1.0f};
    std::atomic<float> stretch_ratio_{1.0f};
    std::atomic<float> eq_low_db_{0.0f};
    std::atomic<float> eq_mid_db_{0.0f};
    std::atomic<float> eq_high_db_{0.0f};
    int64_t track_id_{0};
};

} // namespace automix

#endif // AUTOMIX_DECK_H
