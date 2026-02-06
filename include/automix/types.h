/**
 * AutoMix Engine - Internal Types
 */

#ifndef AUTOMIX_TYPES_H
#define AUTOMIX_TYPES_H

#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <memory>
#include <cstdint>

namespace automix {

/* ============================================================================
 * Result Type
 * ============================================================================ */

// Error wrapper type to avoid variant<T, T> when T = std::string
struct ResultError {
    std::string message;
    ResultError() = default;
    ResultError(std::string m) : message(std::move(m)) {}
    ResultError(const char* m) : message(m) {}
};

template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(ResultError error) : data_(std::move(error)) {}
    Result(const char* error) : data_(ResultError{error}) {}
    
    // Only enable this constructor when T is not std::string to avoid ambiguity
    template<typename U = T, typename = std::enable_if_t<!std::is_same_v<U, std::string>>>
    Result(std::string error) : data_(ResultError{std::move(error)}) {}
    
    bool ok() const { return std::holds_alternative<T>(data_); }
    bool failed() const { return !ok(); }
    
    T& value() { return std::get<T>(data_); }
    const T& value() const { return std::get<T>(data_); }
    
    const std::string& error() const { return std::get<ResultError>(data_).message; }
    
    T value_or(T default_value) const {
        return ok() ? value() : default_value;
    }
    
private:
    std::variant<T, ResultError> data_;
};

/* ============================================================================
 * Audio Types
 * ============================================================================ */

struct AudioBuffer {
    std::vector<float> samples;  // Interleaved L/R
    int sample_rate = 44100;
    int channels = 2;
    
    size_t frame_count() const {
        return channels > 0 ? samples.size() / channels : 0;
    }
    
    float duration_seconds() const {
        return sample_rate > 0 ? static_cast<float>(frame_count()) / sample_rate : 0.0f;
    }
};

/* ============================================================================
 * Track Features
 * ============================================================================ */

struct TrackFeatures {
    float bpm = 0.0f;
    std::vector<float> beats;           // Beat positions in seconds
    std::string key;                    // Camelot notation (e.g., "8A")
    std::vector<float> mfcc;            // 13-dimensional MFCC mean
    std::vector<float> chroma;          // 12-dimensional chroma
    std::vector<float> energy_curve;    // Energy over time (normalized)
    float duration = 0.0f;              // Total duration in seconds
    
    // Optional extended features
    std::optional<float> loudness_lufs;
    std::optional<std::string> genre;
};

/* ============================================================================
 * Track Info (Database Record)
 * ============================================================================ */

struct TrackInfo {
    int64_t id = 0;
    std::string path;
    float bpm = 0.0f;
    std::vector<float> beats;
    std::string key;
    std::vector<float> mfcc;
    std::vector<float> chroma;
    std::vector<float> energy_curve;
    float duration = 0.0f;
    int64_t analyzed_at = 0;            // Unix timestamp
    int64_t file_modified_at = 0;       // File modification time
};

/* ============================================================================
 * Transition Types
 * ============================================================================ */

struct TransitionPoint {
    float time_seconds = 0.0f;
    int beat_index = 0;
    float energy = 0.0f;
};

struct EQTransitionHint {
    bool use_eq_swap = false;
    float low_cut_start = 0.0f;         // When to start cutting low freq (0-1 of transition progress)
    float low_cut_end = 0.5f;           // When low freq is fully cut
    float low_restore_start = 0.5f;     // When to start restoring low freq on incoming track
    float low_restore_end = 1.0f;       // When low freq is fully restored
};

struct TransitionPlan {
    int64_t from_track_id = 0;
    int64_t to_track_id = 0;
    TransitionPoint out_point;          // Where to start fading out current track
    TransitionPoint in_point;           // Where next track starts mixing in
    float bpm_stretch_ratio = 1.0f;     // 1.0 = no stretch
    int pitch_shift_semitones = 0;      // 0 = no shift
    float crossfade_duration = 0.0f;    // Seconds
    EQTransitionHint eq_hint;           // EQ transition metadata
};

/* ============================================================================
 * Playlist Types
 * ============================================================================ */

struct PlaylistEntry {
    int64_t track_id = 0;
    std::optional<TransitionPlan> transition_to_next;
};

struct Playlist {
    std::vector<PlaylistEntry> entries;
    
    size_t size() const { return entries.size(); }
    bool empty() const { return entries.empty(); }
};

/* ============================================================================
 * Similarity Weights
 * ============================================================================ */

struct SimilarityWeights {
    float bpm = 1.0f;
    float key = 1.0f;
    float mfcc = 0.5f;
    float energy = 0.3f;
    float chroma = 0.4f;
    float duration = 0.2f;
    
    static SimilarityWeights defaults() {
        SimilarityWeights w;
        w.bpm = 1.0f; w.key = 1.0f; w.mfcc = 0.5f;
        w.energy = 0.3f; w.chroma = 0.4f; w.duration = 0.2f;
        return w;
    }
    
    static SimilarityWeights for_electronic() {
        SimilarityWeights w;
        w.bpm = 1.5f; w.key = 1.2f; w.mfcc = 0.3f;
        w.energy = 0.5f; w.chroma = 0.3f; w.duration = 0.1f;
        return w;
    }
    
    static SimilarityWeights for_ambient() {
        SimilarityWeights w;
        w.bpm = 0.3f; w.key = 0.8f; w.mfcc = 0.8f;
        w.energy = 1.0f; w.chroma = 0.6f; w.duration = 0.3f;
        return w;
    }
};

/* ============================================================================
 * Playlist Generation Rules
 * ============================================================================ */

enum class EnergyArc {
    None,           // No energy control
    Ascending,      // Gradually increase energy
    Peak,           // Low -> High -> Low (party mode)
    Descending,     // Gradually decrease energy (closing set)
    Wave            // Oscillating energy (wave pattern)
};

struct PlaylistRules {
    float bpm_tolerance = 0.0f;         // 0 = any BPM difference allowed
    bool allow_key_change = true;
    int max_key_distance = 0;           // 0 = any distance on Camelot wheel
    float min_energy_match = 0.0f;      // 0.0-1.0
    std::vector<std::string> style_filter;
    bool allow_cross_style = true;
    SimilarityWeights weights;
    
    // Energy arc control
    EnergyArc energy_arc = EnergyArc::None;
    
    // BPM progression
    float bpm_step_limit = 0.0f;        // 0 = no limit on BPM jump between tracks
    bool prefer_bpm_progression = false; // Prefer gradual BPM changes
    
    // Random seed (0 = non-deterministic)
    uint32_t random_seed = 0;
};

/* ============================================================================
 * Transition Configuration
 * ============================================================================ */

struct TransitionConfig {
    float crossfade_beats = 16.0f;      // Number of beats for crossfade
    bool use_eq_swap = false;           // Use EQ-based transition
    float stretch_limit = 0.06f;        // Max ±6% time stretch
    float min_transition_seconds = 4.0f;
    float max_transition_seconds = 32.0f;
};

/* ============================================================================
 * Playback State
 * ============================================================================ */

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Transitioning
};

} // namespace automix

#endif // AUTOMIX_TYPES_H
