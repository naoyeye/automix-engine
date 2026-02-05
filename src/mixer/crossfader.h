/**
 * AutoMix Engine - Crossfader
 */

#ifndef AUTOMIX_CROSSFADER_H
#define AUTOMIX_CROSSFADER_H

#include "automix/types.h"
#include <atomic>

namespace automix {

/**
 * Crossfader for mixing between two decks.
 */
class Crossfader {
public:
    enum class CurveType {
        Linear,     // Linear crossfade
        EqualPower, // Equal power (constant loudness)
        EQSwap      // EQ-based transition
    };
    
    Crossfader();
    
    /**
     * Set crossfader position (-1.0 = deck A only, 0.0 = center, 1.0 = deck B only).
     */
    void set_position(float position);
    
    /**
     * Get current position.
     */
    float position() const { return position_; }
    
    /**
     * Set curve type.
     */
    void set_curve(CurveType curve) { curve_ = curve; }
    
    /**
     * Get current curve type.
     */
    CurveType curve() const { return curve_; }
    
    /**
     * Start an automated crossfade.
     * 
     * @param from_position Starting position (-1 to 1)
     * @param to_position Ending position (-1 to 1)
     * @param duration_frames Duration in audio frames
     */
    void start_automation(float from_position, float to_position, int duration_frames);
    
    /**
     * Stop automation.
     */
    void stop_automation();
    
    /**
     * Check if automation is active.
     */
    bool is_automating() const { return automating_; }
    
    /**
     * Get volume multipliers for both decks.
     * Call this from the audio thread.
     * 
     * @param deck_a_vol Output: volume for deck A
     * @param deck_b_vol Output: volume for deck B
     * @param frames Number of frames being processed (for automation advance)
     */
    void get_volumes(float& deck_a_vol, float& deck_b_vol, int frames = 0);
    
private:
    std::atomic<float> position_{-1.0f};  // Start with deck A
    std::atomic<CurveType> curve_{CurveType::EqualPower};
    
    // Automation state
    std::atomic<bool> automating_{false};
    float auto_start_pos_{0};
    float auto_end_pos_{0};
    int auto_total_frames_{0};
    int auto_current_frame_{0};
    
    // Compute volumes from position
    void compute_volumes(float pos, float& vol_a, float& vol_b);
};

} // namespace automix

#endif // AUTOMIX_CROSSFADER_H
