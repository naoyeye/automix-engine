/**
 * AutoMix Engine - Energy Analyzer
 */

#ifndef AUTOMIX_ENERGY_ANALYZER_H
#define AUTOMIX_ENERGY_ANALYZER_H

#include "automix/types.h"

namespace automix {

/**
 * Energy curve analysis for finding transition points.
 */
class EnergyAnalyzer {
public:
    EnergyAnalyzer() = default;
    
    /**
     * Compute normalized energy curve over time.
     * @param audio Audio buffer to analyze
     * @param resolution Time resolution in seconds (default: 0.5s)
     * @return Vector of energy values (0.0 to 1.0)
     */
    Result<std::vector<float>> compute_curve(const AudioBuffer& audio, float resolution = 0.5f);
    
    /**
     * Find energy valleys (good transition points).
     * @return Indices into energy curve where energy is locally minimum
     */
    std::vector<int> find_valleys(const std::vector<float>& energy_curve, float threshold = 0.3f);
    
    /**
     * Find energy peaks (high energy sections).
     */
    std::vector<int> find_peaks(const std::vector<float>& energy_curve, float threshold = 0.7f);
    
    /**
     * Compute RMS energy of a segment.
     */
    static float compute_rms(const float* samples, size_t count);
};

} // namespace automix

#endif // AUTOMIX_ENERGY_ANALYZER_H
