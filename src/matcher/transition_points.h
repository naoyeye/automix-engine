/**
 * AutoMix Engine - Transition Point Selection
 */

#ifndef AUTOMIX_TRANSITION_POINTS_H
#define AUTOMIX_TRANSITION_POINTS_H

#include "automix/types.h"

namespace automix {

/**
 * Finds optimal transition points for mixing between tracks.
 */
class TransitionPointFinder {
public:
    TransitionPointFinder() = default;
    
    /**
     * Find optimal out point (where to start fading out current track).
     * Prefers:
     * - Energy valleys
     * - Beat-aligned positions
     * - Near the end of the track (last 8-32 seconds)
     */
    TransitionPoint find_out_point(const TrackInfo& track, const TransitionConfig& config);
    
    /**
     * Find optimal in point (where next track should start mixing in).
     * Prefers:
     * - Energy valleys at the beginning
     * - Beat-aligned positions
     * - Within first 8-30 seconds
     */
    TransitionPoint find_in_point(const TrackInfo& track, const TransitionConfig& config);
    
    /**
     * Create a complete transition plan between two tracks.
     */
    TransitionPlan create_plan(
        const TrackInfo& from_track,
        const TrackInfo& to_track,
        const TransitionConfig& config
    );
    
    /**
     * Find phrase boundaries (8-bar and 16-bar) from beat positions.
     * @return Vector of time positions at phrase boundaries
     */
    std::vector<float> find_phrase_boundaries(const std::vector<float>& beats, int bars_per_phrase = 8);
    
private:
    // Find beat index closest to a time position
    int find_closest_beat(const std::vector<float>& beats, float time);
    
    // Get energy at a specific time
    float get_energy_at(const std::vector<float>& energy_curve, float time, float duration);
    
    // Get energy trend (derivative) at a specific time: negative = decreasing
    float get_energy_trend(const std::vector<float>& energy_curve, float time, float duration);
    
    // Multi-factor score for a candidate transition point (lower = better)
    float score_out_candidate(float t, float energy, float energy_trend,
                              float phrase_alignment, float default_time, float duration);
    float score_in_candidate(float t, float energy, float energy_trend,
                             float phrase_alignment);
    
    // Check how well a time aligns with phrase boundaries (0 = perfect, 1 = worst)
    float phrase_alignment_score(float time, const std::vector<float>& phrase_boundaries);
    
    // Calculate crossfade duration based on BPM and config
    float calculate_crossfade_duration(float bpm, const TransitionConfig& config);
};

} // namespace automix

#endif // AUTOMIX_TRANSITION_POINTS_H
