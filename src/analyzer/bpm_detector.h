/**
 * AutoMix Engine - BPM Detector
 */

#ifndef AUTOMIX_BPM_DETECTOR_H
#define AUTOMIX_BPM_DETECTOR_H

#include "automix/types.h"

namespace automix {

/**
 * BPM and beat detection.
 */
class BPMDetector {
public:
    BPMDetector() = default;
    
    /**
     * Detect BPM from audio buffer.
     * @return BPM value (typically 60-200)
     */
    Result<float> detect(const AudioBuffer& audio);
    
    /**
     * Detect beat positions.
     * @return Vector of beat times in seconds
     */
    Result<std::vector<float>> detect_beats(const AudioBuffer& audio);
    
private:
    // Energy-based onset detection
    std::vector<float> compute_onset_envelope(const AudioBuffer& audio);
    
    // Auto-correlation based BPM estimation
    float estimate_bpm_autocorr(const std::vector<float>& onset_envelope, int sample_rate);
    
    // Peak picking for beat positions
    std::vector<float> pick_peaks(const std::vector<float>& envelope, float threshold, int min_distance);
};

} // namespace automix

#endif // AUTOMIX_BPM_DETECTOR_H
