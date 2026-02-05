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
    
private:
    // Find beat index closest to a time position
    int find_closest_beat(const std::vector<float>& beats, float time);
    
    // Get energy at a specific time
    float get_energy_at(const std::vector<float>& energy_curve, float time, float duration);
    
    // Calculate crossfade duration based on BPM and config
    float calculate_crossfade_duration(float bpm, const TransitionConfig& config);
};

} // namespace automix

#endif // AUTOMIX_TRANSITION_POINTS_H
