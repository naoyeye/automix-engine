/**
 * AutoMix Engine - Playlist Generator
 */

#ifndef AUTOMIX_PLAYLIST_H
#define AUTOMIX_PLAYLIST_H

#include "automix/types.h"
#include "similarity.h"
#include "transition_points.h"
#include <vector>
#include <random>
#include <deque>

namespace automix {

/**
 * Generates intelligent playlists with transition plans.
 */
class PlaylistGenerator {
public:
    PlaylistGenerator();
    
    /**
     * Generate a playlist starting from a seed track.
     * 
     * @param seed Starting track
     * @param candidates Available tracks to choose from
     * @param count Number of tracks to include
     * @param rules Playlist generation rules
     * @param config Transition configuration
     * @return Generated playlist with transition plans
     */
    Playlist generate(
        const TrackInfo& seed,
        const std::vector<TrackInfo>& candidates,
        int count,
        const PlaylistRules& rules,
        const TransitionConfig& config
    );
    
    /**
     * Create transition plans for an existing track list.
     * 
     * @param tracks Ordered list of tracks
     * @param config Transition configuration
     * @return Playlist with transition plans
     */
    Playlist create_with_transitions(
        const std::vector<TrackInfo>& tracks,
        const TransitionConfig& config
    );
    
private:
    SimilarityCalculator similarity_;
    TransitionPointFinder transition_finder_;
    std::mt19937 rng_;
    
    /**
     * Select next track using comprehensive scoring.
     * @param current Current track
     * @param available Available tracks to choose from
     * @param rules Playlist generation rules
     * @param progress Current playlist progress (0.0 - 1.0)
     * @param recent_tracks Recently added tracks (for variety scoring)
     * @param target_count Total number of tracks to generate
     */
    std::optional<TrackInfo> select_next(
        const TrackInfo& current,
        const std::vector<TrackInfo>& available,
        const PlaylistRules& rules,
        float progress,
        const std::deque<TrackInfo>& recent_tracks,
        int target_count
    );
    
    // Calculate target energy for a given progress based on EnergyArc
    float target_energy_for_progress(EnergyArc arc, float progress);
    
    // Calculate average energy of a track from its energy curve
    float track_average_energy(const TrackInfo& track);
    
    // Score a candidate track (higher = better)
    float score_candidate(
        const TrackInfo& current,
        const TrackInfo& candidate,
        const PlaylistRules& rules,
        float progress,
        const std::deque<TrackInfo>& recent_tracks,
        int target_count
    );
};

} // namespace automix

#endif // AUTOMIX_PLAYLIST_H
