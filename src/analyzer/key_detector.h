/**
 * AutoMix Engine - Key Detector
 */

#ifndef AUTOMIX_KEY_DETECTOR_H
#define AUTOMIX_KEY_DETECTOR_H

#include "automix/types.h"

namespace automix {

/**
 * Musical key detection.
 * Returns key in Camelot notation (e.g., "8A", "11B").
 */
class KeyDetector {
public:
    KeyDetector() = default;
    
    /**
     * Detect musical key from audio buffer.
     * @return Key in Camelot notation (e.g., "8A", "11B")
     */
    Result<std::string> detect(const AudioBuffer& audio);
    
    /**
     * Compute chroma features (12-dimensional pitch class profile).
     */
    Result<std::vector<float>> compute_chroma(const AudioBuffer& audio);
    
private:
    // Key profiles for major and minor keys
    static const float major_profile_[12];
    static const float minor_profile_[12];
    
    // Convert pitch class to Camelot notation
    static std::string pitch_class_to_camelot(int pitch_class, bool is_major);
    
    // Correlate chroma with key profiles
    float correlate_with_profile(const std::vector<float>& chroma, const float* profile, int shift);
};

} // namespace automix

#endif // AUTOMIX_KEY_DETECTOR_H
