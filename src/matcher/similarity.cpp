/**
 * AutoMix Engine - Similarity Calculator Implementation
 */

#include "similarity.h"
#include "../core/utils.h"
#include <algorithm>
#include <cmath>

namespace automix {

SimilarityCalculator::SimilarityCalculator(const SimilarityWeights& weights)
    : weights_(weights) {}

float SimilarityCalculator::distance(const TrackInfo& a, const TrackInfo& b) const {
    float d = 0.0f;
    float total_weight = 0.0f;
    
    // BPM distance
    if (weights_.bpm > 0 && a.bpm > 0 && b.bpm > 0) {
        d += weights_.bpm * bpm_distance(a.bpm, b.bpm);
        total_weight += weights_.bpm;
    }
    
    // Key distance
    if (weights_.key > 0 && !a.key.empty() && !b.key.empty()) {
        d += weights_.key * key_distance(a.key, b.key);
        total_weight += weights_.key;
    }
    
    // MFCC distance
    if (weights_.mfcc > 0 && !a.mfcc.empty() && !b.mfcc.empty()) {
        d += weights_.mfcc * mfcc_distance(a.mfcc, b.mfcc);
        total_weight += weights_.mfcc;
    }
    
    // Energy distance
    if (weights_.energy > 0 && !a.energy_curve.empty() && !b.energy_curve.empty()) {
        d += weights_.energy * energy_distance(a.energy_curve, b.energy_curve);
        total_weight += weights_.energy;
    }
    
    // Chroma distance
    if (weights_.chroma > 0 && !a.chroma.empty() && !b.chroma.empty()) {
        d += weights_.chroma * chroma_distance(a.chroma, b.chroma);
        total_weight += weights_.chroma;
    }
    
    // Duration distance
    if (weights_.duration > 0 && a.duration > 0 && b.duration > 0) {
        d += weights_.duration * duration_distance(a.duration, b.duration);
        total_weight += weights_.duration;
    }
    
    // Normalize by total weight
    return total_weight > 0 ? d / total_weight : 0.0f;
}

float SimilarityCalculator::similarity(const TrackInfo& a, const TrackInfo& b) const {
    float d = distance(a, b);
    // Convert distance to similarity (0-1 range)
    return 1.0f / (1.0f + d);
}

std::vector<std::pair<TrackInfo, float>> SimilarityCalculator::find_similar(
    const TrackInfo& target,
    const std::vector<TrackInfo>& candidates,
    int count
) const {
    std::vector<std::pair<TrackInfo, float>> results;
    results.reserve(candidates.size());
    
    for (const auto& candidate : candidates) {
        if (candidate.id == target.id) continue;  // Skip self
        
        float d = distance(target, candidate);
        results.push_back({candidate, d});
    }
    
    // Sort by distance (ascending)
    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    // Limit results
    if (static_cast<int>(results.size()) > count) {
        results.resize(count);
    }
    
    return results;
}

bool SimilarityCalculator::are_compatible(const TrackInfo& a, const TrackInfo& b, const PlaylistRules& rules) const {
    // Check BPM tolerance
    if (rules.bpm_tolerance > 0 && a.bpm > 0 && b.bpm > 0) {
        float bpm_diff = utils::bpm_distance(a.bpm, b.bpm);
        if (bpm_diff > rules.bpm_tolerance) {
            return false;
        }
    }
    
    // Check key compatibility
    if (!rules.allow_key_change && !a.key.empty() && !b.key.empty()) {
        int key_dist = utils::camelot_distance(a.key, b.key);
        if (key_dist > 0) {
            return false;
        }
    } else if (rules.max_key_distance > 0 && !a.key.empty() && !b.key.empty()) {
        int key_dist = utils::camelot_distance(a.key, b.key);
        if (key_dist > rules.max_key_distance) {
            return false;
        }
    }
    
    // Check energy match
    if (rules.min_energy_match > 0 && !a.energy_curve.empty() && !b.energy_curve.empty()) {
        float energy_sim = 1.0f - energy_distance(a.energy_curve, b.energy_curve);
        if (energy_sim < rules.min_energy_match) {
            return false;
        }
    }
    
    return true;
}

float SimilarityCalculator::bpm_distance(float bpm1, float bpm2) const {
    // Use the utility function that handles double/half time
    return utils::bpm_distance(bpm1, bpm2);
}

float SimilarityCalculator::key_distance(const std::string& key1, const std::string& key2) const {
    // Camelot wheel distance normalized to 0-1
    int dist = utils::camelot_distance(key1, key2);
    return static_cast<float>(dist) / 6.0f;  // Max distance is 6
}

float SimilarityCalculator::mfcc_distance(const std::vector<float>& mfcc1, const std::vector<float>& mfcc2) const {
    return utils::cosine_distance(mfcc1, mfcc2);
}

float SimilarityCalculator::energy_distance(const std::vector<float>& energy1, const std::vector<float>& energy2) const {
    if (energy1.empty() || energy2.empty()) {
        return 0.0f;
    }
    
    // Resample to same length
    const size_t target_len = 100;
    
    auto resample = [](const std::vector<float>& curve, size_t len) {
        if (curve.size() <= 1) return std::vector<float>(len, curve.empty() ? 0.0f : curve[0]);
        std::vector<float> resampled(len);
        for (size_t i = 0; i < len; ++i) {
            float src_idx = static_cast<float>(i) * (curve.size() - 1) / (len - 1);
            size_t idx0 = static_cast<size_t>(src_idx);
            size_t idx1 = std::min(idx0 + 1, curve.size() - 1);
            float frac = src_idx - idx0;
            resampled[i] = curve[idx0] * (1.0f - frac) + curve[idx1] * frac;
        }
        return resampled;
    };
    
    auto e1 = resample(energy1, target_len);
    auto e2 = resample(energy2, target_len);
    
    // 1) Global correlation (original approach)
    float mean1 = 0, mean2 = 0;
    for (size_t i = 0; i < target_len; ++i) {
        mean1 += e1[i];
        mean2 += e2[i];
    }
    mean1 /= target_len;
    mean2 /= target_len;
    
    float numerator = 0, var1 = 0, var2 = 0;
    for (size_t i = 0; i < target_len; ++i) {
        float d1 = e1[i] - mean1;
        float d2 = e2[i] - mean2;
        numerator += d1 * d2;
        var1 += d1 * d1;
        var2 += d2 * d2;
    }
    
    float denominator = std::sqrt(var1 * var2);
    float correlation = (denominator > 1e-10f) ? (numerator / denominator) : 0.0f;
    float global_distance = (1.0f - correlation) / 2.0f;
    
    // 2) Segmented comparison (5 segments: intro/buildup/peak/breakdown/outro)
    float seg_distance = segment_energy_distance(e1, e2, 5);
    
    // Blend: 60% global correlation + 40% segmented
    return 0.6f * global_distance + 0.4f * seg_distance;
}

float SimilarityCalculator::segment_energy_distance(
    const std::vector<float>& e1, const std::vector<float>& e2, size_t segments
) const {
    if (e1.size() != e2.size() || e1.empty() || segments == 0) {
        return 0.0f;
    }
    
    size_t len = e1.size();
    size_t seg_len = len / segments;
    if (seg_len == 0) seg_len = 1;
    
    float total_diff = 0.0f;
    size_t actual_segments = 0;
    
    for (size_t s = 0; s < segments; ++s) {
        size_t start = s * seg_len;
        size_t end = (s == segments - 1) ? len : (s + 1) * seg_len;
        if (start >= len) break;
        
        // Compute mean and variance for each segment
        float sum1 = 0, sum2 = 0;
        float sq1 = 0, sq2 = 0;
        size_t count = end - start;
        
        for (size_t i = start; i < end; ++i) {
            sum1 += e1[i];
            sum2 += e2[i];
            sq1 += e1[i] * e1[i];
            sq2 += e2[i] * e2[i];
        }
        
        float mean1 = sum1 / count;
        float mean2 = sum2 / count;
        float var1 = sq1 / count - mean1 * mean1;
        float var2 = sq2 / count - mean2 * mean2;
        
        // Mean difference (normalized)
        float mean_diff = std::abs(mean1 - mean2);
        
        // Variance difference (compare energy dynamics)
        float var_diff = std::abs(std::sqrt(std::max(0.0f, var1)) - std::sqrt(std::max(0.0f, var2)));
        
        total_diff += 0.7f * mean_diff + 0.3f * var_diff;
        actual_segments++;
    }
    
    // Normalize to 0-1 range (energy values are already 0-1)
    return actual_segments > 0 ? utils::clamp(total_diff / actual_segments, 0.0f, 1.0f) : 0.0f;
}

float SimilarityCalculator::chroma_distance(const std::vector<float>& chroma1, const std::vector<float>& chroma2) const {
    return utils::cosine_distance(chroma1, chroma2);
}

float SimilarityCalculator::duration_distance(float dur1, float dur2) const {
    if (dur1 <= 0 || dur2 <= 0) return 0.0f;
    // Ratio-based distance: normalized so that same duration = 0, double duration ~ 0.5
    float ratio = std::max(dur1, dur2) / std::min(dur1, dur2);
    // Map ratio [1, inf) -> distance [0, 1) using 1 - 1/ratio
    return utils::clamp(1.0f - 1.0f / ratio, 0.0f, 1.0f);
}

} // namespace automix
