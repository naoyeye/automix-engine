/**
 * AutoMix Engine - Transition Point Selection Implementation
 */

#include "transition_points.h"
#include "../core/utils.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace automix {

// ============================================================================
// Phrase boundary detection
// ============================================================================

std::vector<float> TransitionPointFinder::find_phrase_boundaries(
    const std::vector<float>& beats, int bars_per_phrase
) {
    std::vector<float> boundaries;
    if (beats.size() < 4 || bars_per_phrase <= 0) return boundaries;
    
    // Each bar = 4 beats (assuming 4/4 time signature)
    int beats_per_phrase = bars_per_phrase * 4;
    
    for (size_t i = 0; i < beats.size(); i += beats_per_phrase) {
        boundaries.push_back(beats[i]);
    }
    
    return boundaries;
}

float TransitionPointFinder::phrase_alignment_score(
    float time, const std::vector<float>& phrase_boundaries
) {
    if (phrase_boundaries.empty()) return 1.0f;  // No info = worst score
    
    float min_dist = std::numeric_limits<float>::max();
    for (float boundary : phrase_boundaries) {
        float dist = std::abs(time - boundary);
        if (dist < min_dist) min_dist = dist;
    }
    
    // Normalize: 0 = exactly on phrase boundary, 1 = far away
    // A beat at 120 BPM is 0.5s; a phrase (8 bars) is ~16s
    // Distances < 1s are considered well-aligned
    return utils::clamp(min_dist / 2.0f, 0.0f, 1.0f);
}

// ============================================================================
// Multi-factor scoring
// ============================================================================

float TransitionPointFinder::score_out_candidate(
    float t, float energy, float energy_trend,
    float phrase_alignment, float default_time, float duration
) {
    // Weights for multi-factor scoring
    const float w_energy = 0.35f;
    const float w_phrase = 0.30f;
    const float w_position = 0.15f;
    const float w_trend = 0.20f;
    
    float energy_score = energy;  // Lower energy = better (0-1)
    float phrase_score = phrase_alignment;  // 0 = perfect alignment
    float position_score = duration > 0 ? std::abs(t - default_time) / duration : 0.0f;
    // For out point: energy should be decreasing (negative trend is good)
    float trend_score = utils::clamp((energy_trend + 1.0f) / 2.0f, 0.0f, 1.0f);  // -1->0 (best), +1->1 (worst)
    
    return w_energy * energy_score
         + w_phrase * phrase_score
         + w_position * position_score
         + w_trend * trend_score;
}

float TransitionPointFinder::score_in_candidate(
    float t, float energy, float energy_trend,
    float phrase_alignment
) {
    const float w_energy = 0.35f;
    const float w_phrase = 0.35f;
    const float w_trend = 0.30f;
    
    float energy_score = energy;
    float phrase_score = phrase_alignment;
    // For in point: energy should be increasing (positive trend is good)
    float trend_score = utils::clamp((-energy_trend + 1.0f) / 2.0f, 0.0f, 1.0f);
    
    return w_energy * energy_score
         + w_phrase * phrase_score
         + w_trend * trend_score;
}

float TransitionPointFinder::get_energy_trend(
    const std::vector<float>& energy_curve, float time, float duration
) {
    if (energy_curve.empty() || duration <= 0) return 0.0f;
    
    // Look at energy 1 second before and after
    float dt = 1.0f;
    float e_before = get_energy_at(energy_curve, std::max(0.0f, time - dt), duration);
    float e_after = get_energy_at(energy_curve, std::min(duration, time + dt), duration);
    
    // Normalized derivative: -1 to +1
    return utils::clamp(e_after - e_before, -1.0f, 1.0f);
}

// ============================================================================
// Find out/in points with phrase-aware multi-factor scoring
// ============================================================================

TransitionPoint TransitionPointFinder::find_out_point(const TrackInfo& track, const TransitionConfig& config) {
    TransitionPoint point;
    
    if (track.duration <= 0) {
        return point;
    }
    
    // Default: 16 seconds before end
    float default_out_time = std::max(0.0f, track.duration - 16.0f);
    
    // Search window: last min_transition..max_transition seconds
    float search_start = std::max(0.0f, track.duration - config.max_transition_seconds);
    float search_end = std::max(0.0f, track.duration - config.min_transition_seconds);
    
    if (search_start >= search_end) {
        point.time_seconds = track.duration * 0.7f;
        point.beat_index = find_closest_beat(track.beats, point.time_seconds);
        point.energy = get_energy_at(track.energy_curve, point.time_seconds, track.duration);
        return point;
    }
    
    // Build phrase boundaries (both 8-bar and 16-bar)
    auto phrases_8 = find_phrase_boundaries(track.beats, 8);
    auto phrases_16 = find_phrase_boundaries(track.beats, 16);
    // Merge and deduplicate
    std::vector<float> all_phrases;
    all_phrases.insert(all_phrases.end(), phrases_8.begin(), phrases_8.end());
    all_phrases.insert(all_phrases.end(), phrases_16.begin(), phrases_16.end());
    std::sort(all_phrases.begin(), all_phrases.end());
    all_phrases.erase(std::unique(all_phrases.begin(), all_phrases.end()), all_phrases.end());
    
    // Collect candidate points: uniform samples + phrase boundaries in window
    std::vector<float> candidates;
    
    // Uniform samples
    const int num_samples = 40;
    for (int i = 0; i < num_samples; ++i) {
        float t = search_start + (search_end - search_start) * i / (num_samples - 1);
        candidates.push_back(t);
    }
    
    // Add phrase boundaries that fall in the search window
    for (float pb : all_phrases) {
        if (pb >= search_start && pb <= search_end) {
            candidates.push_back(pb);
        }
    }
    
    // Score each candidate
    float best_time = default_out_time;
    float best_score = std::numeric_limits<float>::max();
    
    for (float t : candidates) {
        // Snap to nearest beat
        int beat_idx = find_closest_beat(track.beats, t);
        if (beat_idx >= 0 && beat_idx < static_cast<int>(track.beats.size())) {
            t = track.beats[beat_idx];
        }
        
        // Skip if snapped outside window
        if (t < search_start || t > search_end) continue;
        
        float energy = get_energy_at(track.energy_curve, t, track.duration);
        float trend = get_energy_trend(track.energy_curve, t, track.duration);
        float phrase_align = phrase_alignment_score(t, all_phrases);
        
        float score = score_out_candidate(t, energy, trend, phrase_align, default_out_time, track.duration);
        
        if (score < best_score) {
            best_score = score;
            best_time = t;
        }
    }
    
    point.time_seconds = best_time;
    point.beat_index = find_closest_beat(track.beats, best_time);
    point.energy = get_energy_at(track.energy_curve, best_time, track.duration);
    
    return point;
}

TransitionPoint TransitionPointFinder::find_in_point(const TrackInfo& track, const TransitionConfig& config) {
    TransitionPoint point;
    
    if (track.duration <= 0) {
        return point;
    }
    
    // Search window: first min_transition..max_transition seconds
    float search_start = config.min_transition_seconds;
    float search_end = std::min(track.duration, config.max_transition_seconds);
    
    if (search_start >= search_end) {
        point.time_seconds = 0.0f;
        point.beat_index = 0;
        point.energy = get_energy_at(track.energy_curve, 0.0f, track.duration);
        return point;
    }
    
    // Build phrase boundaries
    auto phrases_8 = find_phrase_boundaries(track.beats, 8);
    auto phrases_16 = find_phrase_boundaries(track.beats, 16);
    std::vector<float> all_phrases;
    all_phrases.insert(all_phrases.end(), phrases_8.begin(), phrases_8.end());
    all_phrases.insert(all_phrases.end(), phrases_16.begin(), phrases_16.end());
    std::sort(all_phrases.begin(), all_phrases.end());
    all_phrases.erase(std::unique(all_phrases.begin(), all_phrases.end()), all_phrases.end());
    
    // Collect candidate points
    std::vector<float> candidates;
    
    const int num_samples = 40;
    for (int i = 0; i < num_samples; ++i) {
        float t = search_start + (search_end - search_start) * i / (num_samples - 1);
        candidates.push_back(t);
    }
    
    for (float pb : all_phrases) {
        if (pb >= search_start && pb <= search_end) {
            candidates.push_back(pb);
        }
    }
    
    // Score each candidate
    float best_time = search_start;
    float best_score = std::numeric_limits<float>::max();
    
    for (float t : candidates) {
        int beat_idx = find_closest_beat(track.beats, t);
        if (beat_idx >= 0 && beat_idx < static_cast<int>(track.beats.size())) {
            t = track.beats[beat_idx];
        }
        
        if (t < search_start || t > search_end) continue;
        
        float energy = get_energy_at(track.energy_curve, t, track.duration);
        float trend = get_energy_trend(track.energy_curve, t, track.duration);
        float phrase_align = phrase_alignment_score(t, all_phrases);
        
        float score = score_in_candidate(t, energy, trend, phrase_align);
        
        if (score < best_score) {
            best_score = score;
            best_time = t;
        }
    }
    
    point.time_seconds = best_time;
    point.beat_index = find_closest_beat(track.beats, best_time);
    point.energy = get_energy_at(track.energy_curve, best_time, track.duration);
    
    return point;
}

TransitionPlan TransitionPointFinder::create_plan(
    const TrackInfo& from_track,
    const TrackInfo& to_track,
    const TransitionConfig& config
) {
    TransitionPlan plan;
    
    plan.from_track_id = from_track.id;
    plan.to_track_id = to_track.id;
    
    // Find transition points
    plan.out_point = find_out_point(from_track, config);
    plan.in_point = find_in_point(to_track, config);
    
    // Calculate BPM stretch ratio
    if (from_track.bpm > 0 && to_track.bpm > 0) {
        plan.bpm_stretch_ratio = utils::calculate_stretch_ratio(from_track.bpm, to_track.bpm);
        
        // Check if stretch is within limits
        float stretch_amount = std::abs(1.0f - plan.bpm_stretch_ratio);
        if (stretch_amount > config.stretch_limit) {
            plan.bpm_stretch_ratio = 1.0f;
        }
    } else {
        plan.bpm_stretch_ratio = 1.0f;
    }
    
    // Smart pitch shift: suggest when keys are close but not compatible
    plan.pitch_shift_semitones = 0;
    if (!from_track.key.empty() && !to_track.key.empty()) {
        int key_dist = utils::camelot_distance(from_track.key, to_track.key);
        if (key_dist > 0 && key_dist <= 2) {
            // Convert Camelot keys to semitones
            // Each Camelot step (same mode) = 7 semitones (perfect 5th)
            // Mode A->B (same number) = +3 semitones (relative major)
            auto camelot_to_semitone = [](const std::string& key) -> int {
                int num = utils::parse_camelot_number(key);
                char mode = utils::parse_camelot_mode(key);
                if (num == 0) return 0;
                // Base: 5A = Cm = 0 semitones
                int semi = ((num - 5) * 7 % 12 + 12) % 12;
                if (mode == 'B' || mode == 'b') semi = (semi + 3) % 12;
                return semi;
            };
            
            int semi1 = camelot_to_semitone(from_track.key);
            int semi2 = camelot_to_semitone(to_track.key);
            
            // Minimum semitone shift to align keys
            int diff = (semi1 - semi2 + 12) % 12;
            int shift = (diff <= 6) ? diff : diff - 12;
            
            // Only suggest if shift is practical (within ±2 semitones)
            if (std::abs(shift) <= 2 && shift != 0) {
                plan.pitch_shift_semitones = shift;
            }
        }
    }
    
    // EQ transition hints
    plan.eq_hint = EQTransitionHint{};
    if (config.use_eq_swap) {
        plan.eq_hint.use_eq_swap = true;
        // Standard EQ swap: cut lows on outgoing track in first half,
        // bring in lows on incoming track in second half
        plan.eq_hint.low_cut_start = 0.0f;
        plan.eq_hint.low_cut_end = 0.5f;
        plan.eq_hint.low_restore_start = 0.5f;
        plan.eq_hint.low_restore_end = 1.0f;
        
        // If energy of out_point is high, start cutting earlier
        if (plan.out_point.energy > 0.7f) {
            plan.eq_hint.low_cut_start = 0.0f;
            plan.eq_hint.low_cut_end = 0.4f;
        }
        
        // If energy of in_point is low, delay restore
        if (plan.in_point.energy < 0.3f) {
            plan.eq_hint.low_restore_start = 0.6f;
            plan.eq_hint.low_restore_end = 1.0f;
        }
    }
    
    // Calculate crossfade duration
    float avg_bpm = (from_track.bpm > 0 && to_track.bpm > 0) 
        ? (from_track.bpm + to_track.bpm) / 2.0f 
        : 120.0f;
    plan.crossfade_duration = calculate_crossfade_duration(avg_bpm, config);
    
    return plan;
}

int TransitionPointFinder::find_closest_beat(const std::vector<float>& beats, float time) {
    if (beats.empty()) return -1;
    
    auto it = std::lower_bound(beats.begin(), beats.end(), time);
    
    if (it == beats.end()) {
        return static_cast<int>(beats.size()) - 1;
    }
    
    if (it == beats.begin()) {
        return 0;
    }
    
    // Check which neighbor is closer
    auto prev = it - 1;
    if (std::abs(*it - time) < std::abs(*prev - time)) {
        return static_cast<int>(it - beats.begin());
    } else {
        return static_cast<int>(prev - beats.begin());
    }
}

float TransitionPointFinder::get_energy_at(const std::vector<float>& energy_curve, float time, float duration) {
    if (energy_curve.empty() || duration <= 0) {
        return 0.5f;  // Default middle energy
    }
    
    // Map time to energy curve index
    float normalized_time = time / duration;
    normalized_time = utils::clamp(normalized_time, 0.0f, 1.0f);
    
    float index_f = normalized_time * (energy_curve.size() - 1);
    size_t index = static_cast<size_t>(index_f);
    
    if (index >= energy_curve.size() - 1) {
        return energy_curve.back();
    }
    
    // Linear interpolation
    float frac = index_f - index;
    return energy_curve[index] * (1.0f - frac) + energy_curve[index + 1] * frac;
}

float TransitionPointFinder::calculate_crossfade_duration(float bpm, const TransitionConfig& config) {
    if (bpm <= 0) {
        return 8.0f;  // Default 8 seconds
    }
    
    // Duration of one beat
    float beat_duration = 60.0f / bpm;
    
    // Crossfade duration in seconds
    float duration = beat_duration * config.crossfade_beats;
    
    // Clamp to reasonable range
    return utils::clamp(duration, config.min_transition_seconds, config.max_transition_seconds);
}

} // namespace automix
