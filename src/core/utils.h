/**
 * AutoMix Engine - Utility Functions
 */

#ifndef AUTOMIX_UTILS_H
#define AUTOMIX_UTILS_H

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace automix {
namespace utils {

/* ============================================================================
 * Math Utilities
 * ============================================================================ */

inline float clamp(float value, float min_val, float max_val) {
    return std::max(min_val, std::min(max_val, value));
}

inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

inline float normalize(float value, float min_val, float max_val) {
    if (max_val <= min_val) return 0.0f;
    return (value - min_val) / (max_val - min_val);
}

/* ============================================================================
 * Vector Math
 * ============================================================================ */

inline float cosine_distance(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 1.0f;
    
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    
    if (norm_a == 0.0f || norm_b == 0.0f) return 1.0f;
    
    float similarity = dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
    return 1.0f - clamp(similarity, -1.0f, 1.0f);
}

inline float euclidean_distance(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return std::numeric_limits<float>::max();
    
    float sum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        float diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

/* ============================================================================
 * Music Theory Utilities
 * ============================================================================ */

/**
 * Camelot wheel key representation.
 * Keys are represented as "NA" where N is 1-12 and A is 'A' (minor) or 'B' (major).
 * Adjacent keys on the wheel are harmonically compatible.
 */

inline int parse_camelot_number(const std::string& key) {
    if (key.empty()) return 0;
    try {
        return std::stoi(key.substr(0, key.length() - 1));
    } catch (...) {
        return 0;
    }
}

inline char parse_camelot_mode(const std::string& key) {
    if (key.empty()) return 'A';
    return key.back();
}

/**
 * Calculate distance on Camelot wheel between two keys.
 * Returns minimum steps needed (0-6 for same mode, or considering mode change).
 */
inline int camelot_distance(const std::string& key1, const std::string& key2) {
    if (key1.empty() || key2.empty()) return 0;
    
    int num1 = parse_camelot_number(key1);
    int num2 = parse_camelot_number(key2);
    char mode1 = parse_camelot_mode(key1);
    char mode2 = parse_camelot_mode(key2);
    
    if (num1 == 0 || num2 == 0) return 0;
    
    // Distance on the wheel (circular, 1-12)
    int diff = std::abs(num1 - num2);
    int wheel_dist = std::min(diff, 12 - diff);
    
    // Same mode: just wheel distance
    if (mode1 == mode2) {
        return wheel_dist;
    }
    
    // Different mode: check relative major/minor (same number = 0 distance)
    if (num1 == num2) {
        return 0;  // Relative major/minor
    }
    
    // Cross-mode: add 1 for mode change penalty
    return wheel_dist + 1;
}

/**
 * Check if two keys are harmonically compatible.
 * Compatible means distance <= 1 on Camelot wheel.
 */
inline bool keys_compatible(const std::string& key1, const std::string& key2) {
    return camelot_distance(key1, key2) <= 1;
}

/* ============================================================================
 * BPM Utilities
 * ============================================================================ */

/**
 * Calculate BPM distance, accounting for double/half time relationships.
 */
inline float bpm_distance(float bpm1, float bpm2) {
    if (bpm1 <= 0 || bpm2 <= 0) return 0.0f;
    
    // Check direct ratio
    float ratio = bpm1 / bpm2;
    
    // Check half time and double time relationships
    float distances[] = {
        std::abs(1.0f - ratio),
        std::abs(2.0f - ratio),
        std::abs(0.5f - ratio)
    };
    
    return *std::min_element(std::begin(distances), std::end(distances));
}

/**
 * Calculate stretch ratio needed to match BPMs.
 * Returns ratio to apply to track2 to match track1's BPM.
 */
inline float calculate_stretch_ratio(float target_bpm, float source_bpm) {
    if (source_bpm <= 0 || target_bpm <= 0) return 1.0f;
    
    float ratio = target_bpm / source_bpm;
    
    // If very close, no stretch needed
    if (std::abs(1.0f - ratio) < 0.01f) return 1.0f;
    
    // Check if half/double time gives better ratio
    if (ratio > 1.5f) ratio /= 2.0f;
    if (ratio < 0.67f) ratio *= 2.0f;
    
    return ratio;
}

/* ============================================================================
 * File Utilities
 * ============================================================================ */

inline bool is_audio_file(const std::filesystem::path& path) {
    static const std::vector<std::string> extensions = {
        ".mp3", ".flac", ".m4a", ".aac", ".ogg", ".wav", ".aiff", ".dsd", ".dsf", ".dff"
    };
    
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

inline std::vector<std::filesystem::path> find_audio_files(
    const std::filesystem::path& path,
    bool recursive = true
) {
    std::vector<std::filesystem::path> files;
    
    try {
        if (std::filesystem::is_regular_file(path)) {
            if (is_audio_file(path)) {
                files.push_back(path);
            }
            return files;
        }

        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                if (entry.is_regular_file() && is_audio_file(entry.path())) {
                    files.push_back(entry.path());
                }
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file() && is_audio_file(entry.path())) {
                    files.push_back(entry.path());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Ignore permission errors, etc.
    }
    
    return files;
}

/* ============================================================================
 * Time Utilities
 * ============================================================================ */

inline int64_t current_timestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

inline int64_t file_modified_time(const std::filesystem::path& path) {
    try {
        auto ftime = std::filesystem::last_write_time(path);
        // C++17 compatible: use duration since epoch
        auto duration = ftime.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
        return seconds.count();
    } catch (...) {
        return 0;
    }
}

} // namespace utils
} // namespace automix

#endif // AUTOMIX_UTILS_H
