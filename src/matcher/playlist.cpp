/**
 * AutoMix Engine - Playlist Generator Implementation
 */

#include "playlist.h"
#include "../core/utils.h"
#include <algorithm>
#include <unordered_set>
#include <numeric>
#include <cmath>

namespace automix {

PlaylistGenerator::PlaylistGenerator() 
    : similarity_(SimilarityWeights::defaults()),
      rng_(std::random_device{}()) {}

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
    
    // Initialize RNG with seed for reproducibility (if specified)
    if (rules.random_seed != 0) {
        rng_.seed(rules.random_seed);
    }
    
    // Track which songs we've used
    std::unordered_set<int64_t> used_ids;
    
    // Recent tracks window for variety scoring
    std::deque<TrackInfo> recent_tracks;
    
    // Start with seed
    PlaylistEntry seed_entry;
    seed_entry.track_id = seed.id;
    playlist.entries.push_back(seed_entry);
    used_ids.insert(seed.id);
    recent_tracks.push_back(seed);
    
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
        float progress = static_cast<float>(playlist.entries.size()) / static_cast<float>(count);
        
        auto next_opt = select_next(current, available, rules, progress, recent_tracks, count);
        
        if (!next_opt) {
            // No compatible track found, relax constraints and try again
            PlaylistRules relaxed = rules;
            relaxed.bpm_tolerance = 0;  // Allow any BPM
            relaxed.max_key_distance = 12;  // Allow any key
            relaxed.allow_key_change = true;
            relaxed.min_energy_match = 0;
            relaxed.bpm_step_limit = 0;
            
            next_opt = select_next(current, available, relaxed, progress, recent_tracks, count);
            
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
        
        // Maintain recent tracks window (keep last 5)
        recent_tracks.push_back(next);
        if (recent_tracks.size() > 5) {
            recent_tracks.pop_front();
        }
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

// ============================================================================
// Comprehensive candidate scoring
// ============================================================================

std::optional<TrackInfo> PlaylistGenerator::select_next(
    const TrackInfo& current,
    const std::vector<TrackInfo>& available,
    const PlaylistRules& rules,
    float progress,
    const std::deque<TrackInfo>& recent_tracks,
    int target_count
) {
    if (available.empty()) {
        return std::nullopt;
    }
    
    // Filter compatible tracks
    std::vector<TrackInfo> compatible;
    for (const auto& track : available) {
        if (similarity_.are_compatible(current, track, rules)) {
            // Additional BPM step limit check
            if (rules.bpm_step_limit > 0 && current.bpm > 0 && track.bpm > 0) {
                float bpm_diff = utils::bpm_distance(current.bpm, track.bpm);
                if (bpm_diff > rules.bpm_step_limit / 100.0f) {
                    continue;
                }
            }
            compatible.push_back(track);
        }
    }
    
    if (compatible.empty()) {
        return std::nullopt;
    }
    
    // Score all compatible tracks
    std::vector<std::pair<TrackInfo, float>> scored;
    scored.reserve(compatible.size());
    
    for (const auto& candidate : compatible) {
        float score = score_candidate(current, candidate, rules, progress, recent_tracks, target_count);
        scored.push_back({candidate, score});
    }
    
    // Sort by score (descending - higher is better)
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Pick from top candidates with weighted randomization
    int pick_from = std::min(5, static_cast<int>(scored.size()));
    
    // Weight distribution: exponentially favor higher scores
    std::vector<float> weights(pick_from);
    for (int i = 0; i < pick_from; ++i) {
        weights[i] = std::exp(-0.5f * i);  // Exponential decay
    }
    
    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    int pick_idx = dist(rng_);
    
    return scored[pick_idx].first;
}

float PlaylistGenerator::score_candidate(
    const TrackInfo& current,
    const TrackInfo& candidate,
    const PlaylistRules& rules,
    float progress,
    const std::deque<TrackInfo>& recent_tracks,
    int target_count
) {
    // 1) Similarity score (0-1, higher = more similar)
    float sim_score = similarity_.similarity(current, candidate);
    
    // 2) Energy arc score (0-1, higher = better match to target energy)
    float energy_arc_score = 1.0f;
    if (rules.energy_arc != EnergyArc::None) {
        float target_energy = target_energy_for_progress(rules.energy_arc, progress);
        float track_energy = track_average_energy(candidate);
        float energy_diff = std::abs(target_energy - track_energy);
        energy_arc_score = 1.0f - utils::clamp(energy_diff, 0.0f, 1.0f);
    }
    
    // 3) BPM progression score (0-1, higher = smoother BPM transition)
    float bpm_prog_score = 1.0f;
    if (rules.prefer_bpm_progression && current.bpm > 0 && candidate.bpm > 0) {
        float bpm_diff = utils::bpm_distance(current.bpm, candidate.bpm);
        // Prefer small BPM differences
        bpm_prog_score = 1.0f / (1.0f + bpm_diff * 20.0f);
    }
    
    // 4) Variety score (0-1, higher = more different from recent tracks)
    float variety_score = 1.0f;
    if (!recent_tracks.empty()) {
        float total_distance = 0.0f;
        for (const auto& recent : recent_tracks) {
            total_distance += similarity_.distance(candidate, recent);
        }
        float avg_distance = total_distance / recent_tracks.size();
        // Map average distance to variety score (higher distance = higher variety)
        variety_score = utils::clamp(avg_distance * 2.0f, 0.0f, 1.0f);
    }
    
    // Combine with weights
    const float w_similarity = 0.35f;
    const float w_energy_arc = 0.25f;
    const float w_bpm_prog = 0.20f;
    const float w_variety = 0.20f;
    
    float final_score = w_similarity * sim_score
                      + w_energy_arc * energy_arc_score
                      + w_bpm_prog * bpm_prog_score
                      + w_variety * variety_score;
    
    return final_score;
}

// ============================================================================
// Energy arc helpers
// ============================================================================

float PlaylistGenerator::target_energy_for_progress(EnergyArc arc, float progress) {
    progress = utils::clamp(progress, 0.0f, 1.0f);
    
    switch (arc) {
        case EnergyArc::Ascending:
            // Linear ramp from 0.2 to 0.9
            return 0.2f + 0.7f * progress;
            
        case EnergyArc::Peak:
            // Parabolic: peaks at 60% of the set
            if (progress < 0.6f) {
                // Rising phase
                return 0.3f + 0.7f * (progress / 0.6f);
            } else {
                // Falling phase
                float t = (progress - 0.6f) / 0.4f;
                return 1.0f - 0.6f * t;
            }
            
        case EnergyArc::Descending:
            // Linear ramp from 0.9 to 0.2
            return 0.9f - 0.7f * progress;
            
        case EnergyArc::Wave:
            // Sinusoidal wave (2 cycles over the set)
            return 0.5f + 0.3f * std::sin(progress * 4.0f * 3.14159265f);
            
        case EnergyArc::None:
        default:
            return 0.5f;
    }
}

float PlaylistGenerator::track_average_energy(const TrackInfo& track) {
    if (track.energy_curve.empty()) return 0.5f;
    
    float sum = std::accumulate(track.energy_curve.begin(), track.energy_curve.end(), 0.0f);
    return sum / track.energy_curve.size();
}

} // namespace automix
