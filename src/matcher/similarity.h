/**
 * AutoMix Engine - Similarity Calculator
 */

#ifndef AUTOMIX_SIMILARITY_H
#define AUTOMIX_SIMILARITY_H

#include "automix/types.h"
#include <vector>

namespace automix {

/**
 * Calculate similarity/distance between tracks.
 */
class SimilarityCalculator {
public:
    explicit SimilarityCalculator(const SimilarityWeights& weights = SimilarityWeights::defaults());
    
    /**
     * Calculate distance between two tracks (lower = more similar).
     */
    float distance(const TrackInfo& a, const TrackInfo& b) const;
    
    /**
     * Calculate similarity score between two tracks (higher = more similar, 0-1).
     */
    float similarity(const TrackInfo& a, const TrackInfo& b) const;
    
    /**
     * Find most similar tracks to a given track.
     * @param target Target track
     * @param candidates Candidate tracks to compare
     * @param count Maximum number of results
     * @return Sorted vector of (track, distance) pairs
     */
    std::vector<std::pair<TrackInfo, float>> find_similar(
        const TrackInfo& target,
        const std::vector<TrackInfo>& candidates,
        int count = 10
    ) const;
    
    /**
     * Check if two tracks are compatible for mixing.
     */
    bool are_compatible(const TrackInfo& a, const TrackInfo& b, const PlaylistRules& rules) const;
    
    void set_weights(const SimilarityWeights& weights) { weights_ = weights; }
    const SimilarityWeights& weights() const { return weights_; }
    
private:
    SimilarityWeights weights_;
    
    // Component distance functions
    float bpm_distance(float bpm1, float bpm2) const;
    float key_distance(const std::string& key1, const std::string& key2) const;
    float mfcc_distance(const std::vector<float>& mfcc1, const std::vector<float>& mfcc2) const;
    float energy_distance(const std::vector<float>& energy1, const std::vector<float>& energy2) const;
    float chroma_distance(const std::vector<float>& chroma1, const std::vector<float>& chroma2) const;
    float duration_distance(float dur1, float dur2) const;
    
    // Energy curve segmented comparison helper
    float segment_energy_distance(const std::vector<float>& e1, const std::vector<float>& e2, size_t segments) const;
};

} // namespace automix

#endif // AUTOMIX_SIMILARITY_H
