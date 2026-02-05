/**
 * AutoMix Engine - Transition Point Selection Implementation
 */

#include "transition_points.h"
#include "../core/utils.h"
#include <algorithm>
#include <cmath>

namespace automix {

TransitionPoint TransitionPointFinder::find_out_point(const TrackInfo& track, const TransitionConfig& config) {
    TransitionPoint point;
    
    if (track.duration <= 0) {
        return point;
    }
    
    // Default: 16 seconds before end
    float default_out_time = std::max(0.0f, track.duration - 16.0f);
    
    // Search window: last 8-32 seconds
    float search_start = std::max(0.0f, track.duration - config.max_transition_seconds);
    float search_end = std::max(0.0f, track.duration - config.min_transition_seconds);
    
    if (search_start >= search_end) {
        // Track too short, just use a point near the middle of what we have
        point.time_seconds = track.duration * 0.7f;
        point.beat_index = find_closest_beat(track.beats, point.time_seconds);
        point.energy = get_energy_at(track.energy_curve, point.time_seconds, track.duration);
        return point;
    }
    
    // Find energy valleys in the search window
    float best_time = default_out_time;
    float best_score = std::numeric_limits<float>::max();
    
    // Sample points in the search window
    const int num_samples = 20;
    for (int i = 0; i < num_samples; ++i) {
        float t = search_start + (search_end - search_start) * i / (num_samples - 1);
        
        // Snap to nearest beat
        int beat_idx = find_closest_beat(track.beats, t);
        if (beat_idx >= 0 && beat_idx < static_cast<int>(track.beats.size())) {
            t = track.beats[beat_idx];
        }
        
        // Score: prefer low energy and being beat-aligned
        float energy = get_energy_at(track.energy_curve, t, track.duration);
        float score = energy;  // Lower energy = better
        
        // Slight preference for positions closer to the default
        score += 0.1f * std::abs(t - default_out_time) / track.duration;
        
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
    
    // Search window: first 8-30 seconds
    float search_start = config.min_transition_seconds;
    float search_end = std::min(track.duration, config.max_transition_seconds);
    
    if (search_start >= search_end) {
        // Track too short
        point.time_seconds = 0.0f;
        point.beat_index = 0;
        point.energy = get_energy_at(track.energy_curve, 0.0f, track.duration);
        return point;
    }
    
    // Find energy valleys in the search window
    float best_time = search_start;
    float best_score = std::numeric_limits<float>::max();
    
    // Sample points in the search window
    const int num_samples = 20;
    for (int i = 0; i < num_samples; ++i) {
        float t = search_start + (search_end - search_start) * i / (num_samples - 1);
        
        // Snap to nearest beat
        int beat_idx = find_closest_beat(track.beats, t);
        if (beat_idx >= 0 && beat_idx < static_cast<int>(track.beats.size())) {
            t = track.beats[beat_idx];
        }
        
        // Score: prefer low energy
        float energy = get_energy_at(track.energy_curve, t, track.duration);
        float score = energy;
        
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
            // Don't stretch if it would exceed the limit
            plan.bpm_stretch_ratio = 1.0f;
        }
    } else {
        plan.bpm_stretch_ratio = 1.0f;
    }
    
    // Calculate pitch shift (if keys are incompatible and we want to fix it)
    // For now, we don't auto-pitch-shift
    plan.pitch_shift_semitones = 0;
    
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
