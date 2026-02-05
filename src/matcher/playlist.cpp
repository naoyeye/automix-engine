/**
 * AutoMix Engine - Playlist Generator Implementation
 */

#include "playlist.h"
#include <algorithm>
#include <unordered_set>

namespace automix {

PlaylistGenerator::PlaylistGenerator() 
    : similarity_(SimilarityWeights::defaults()) {}

Playlist PlaylistGenerator::generate(
    const TrackInfo& seed,
    const std::vector<TrackInfo>& candidates,
    int count,
    const PlaylistRules& rules,
    const TransitionConfig& config
) {
    Playlist playlist;
    
    // Configure similarity calculator with rules weights
    similarity_.set_weights(rules.weights);
    
    // Track which songs we've used
    std::unordered_set<int64_t> used_ids;
    
    // Start with seed
    PlaylistEntry seed_entry;
    seed_entry.track_id = seed.id;
    playlist.entries.push_back(seed_entry);
    used_ids.insert(seed.id);
    
    // Build available pool (excluding seed)
    std::vector<TrackInfo> available;
    available.reserve(candidates.size());
    for (const auto& track : candidates) {
        if (track.id != seed.id) {
            available.push_back(track);
        }
    }
    
    // Generate playlist
    TrackInfo current = seed;
    
    while (static_cast<int>(playlist.entries.size()) < count && !available.empty()) {
        auto next_opt = select_next(current, available, rules);
        
        if (!next_opt) {
            // No compatible track found, relax constraints and try again
            PlaylistRules relaxed = rules;
            relaxed.bpm_tolerance = 0;  // Allow any BPM
            relaxed.max_key_distance = 12;  // Allow any key
            relaxed.allow_key_change = true;
            
            next_opt = select_next(current, available, relaxed);
            
            if (!next_opt) {
                break;  // Really no tracks left
            }
        }
        
        const TrackInfo& next = *next_opt;
        
        // Create transition plan
        auto plan = transition_finder_.create_plan(current, next, config);
        
        // Update previous entry with transition
        playlist.entries.back().transition_to_next = plan;
        
        // Add new entry
        PlaylistEntry entry;
        entry.track_id = next.id;
        playlist.entries.push_back(entry);
        
        // Update state
        used_ids.insert(next.id);
        available.erase(
            std::remove_if(available.begin(), available.end(),
                [&used_ids](const TrackInfo& t) { return used_ids.count(t.id) > 0; }),
            available.end()
        );
        
        current = next;
    }
    
    return playlist;
}

Playlist PlaylistGenerator::create_with_transitions(
    const std::vector<TrackInfo>& tracks,
    const TransitionConfig& config
) {
    Playlist playlist;
    
    for (size_t i = 0; i < tracks.size(); ++i) {
        PlaylistEntry entry;
        entry.track_id = tracks[i].id;
        
        // Create transition to next track (if not last)
        if (i + 1 < tracks.size()) {
            entry.transition_to_next = transition_finder_.create_plan(
                tracks[i], tracks[i + 1], config);
        }
        
        playlist.entries.push_back(entry);
    }
    
    return playlist;
}

std::optional<TrackInfo> PlaylistGenerator::select_next(
    const TrackInfo& current,
    const std::vector<TrackInfo>& available,
    const PlaylistRules& rules
) {
    if (available.empty()) {
        return std::nullopt;
    }
    
    // Filter compatible tracks
    std::vector<TrackInfo> compatible;
    for (const auto& track : available) {
        if (similarity_.are_compatible(current, track, rules)) {
            compatible.push_back(track);
        }
    }
    
    if (compatible.empty()) {
        return std::nullopt;
    }
    
    // Find most similar among compatible
    auto similar = similarity_.find_similar(current, compatible, 5);
    
    if (similar.empty()) {
        return compatible.front();  // Fallback to first compatible
    }
    
    // Add some randomization - pick from top 3
    int pick_from = std::min(3, static_cast<int>(similar.size()));
    int pick_idx = rand() % pick_from;
    
    return similar[pick_idx].first;
}

} // namespace automix
