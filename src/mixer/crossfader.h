/**
 * AutoMix Engine - Crossfader
 */

#ifndef AUTOMIX_CROSSFADER_H
#define AUTOMIX_CROSSFADER_H

#include "automix/types.h"
#include <atomic>

namespace automix {

/**
 * Mix parameters output for both decks (volume + EQ).
 */
struct MixParams {
    float volume_a = 1.0f, volume_b = 0.0f;
    // EQ gains in dB (0 = unity)
    float eq_low_a  = 0.0f, eq_mid_a  = 0.0f, eq_high_a = 0.0f;
    float eq_low_b  = 0.0f, eq_mid_b  = 0.0f, eq_high_b = 0.0f;
};

/**
 * Crossfader for mixing between two decks.
 */
class Crossfader {
public:
    enum class CurveType {
        Linear,     // Linear crossfade
        EqualPower, // Equal power (constant loudness)
        EQSwap,     // EQ-based transition (swap bass)
        HardCut     // Instant cut (for drops / breaks)
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
     * Get volume multipliers for both decks (legacy, no EQ).
     * Call this from the audio thread.
     */
    void get_volumes(float& deck_a_vol, float& deck_b_vol, int frames = 0);
    
    /**
     * Get full mix parameters for both decks (volume + EQ).
     * Call this from the audio thread.
     *
     * @param params  Output struct filled with volume and EQ values.
     * @param frames  Number of frames being processed (for automation advance).
     */
    void get_mix_params(MixParams& params, int frames = 0);
    
private:
    std::atomic<float> position_{-1.0f};  // Start with deck A
    std::atomic<CurveType> curve_{CurveType::EqualPower};
    
    // Automation state
    std::atomic<bool> automating_{false};
    float auto_start_pos_{0};
    float auto_end_pos_{0};
    int auto_total_frames_{0};
    int auto_current_frame_{0};
    
    // Advance automation, returns current position
    float advance_automation(int frames);
    
    // Compute volumes from position
    void compute_volumes(float pos, float& vol_a, float& vol_b);
    
    // Compute full mix params from position
    void compute_mix_params(float pos, MixParams& params);
};

} // namespace automix

#endif // AUTOMIX_CROSSFADER_H
