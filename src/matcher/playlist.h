/**
 * AutoMix Engine - Playlist Generator
 */

#ifndef AUTOMIX_PLAYLIST_H
#define AUTOMIX_PLAYLIST_H

#include "automix/types.h"
#include "similarity.h"
#include "transition_points.h"
#include <vector>

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
    
    /**
     * Select next track based on similarity and rules.
     */
    std::optional<TrackInfo> select_next(
        const TrackInfo& current,
        const std::vector<TrackInfo>& available,
        const PlaylistRules& rules
    );
};

} // namespace automix

#endif // AUTOMIX_PLAYLIST_H
