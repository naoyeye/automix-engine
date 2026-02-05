/**
 * AutoMix Engine - Audio Analyzer
 */

#ifndef AUTOMIX_ANALYZER_H
#define AUTOMIX_ANALYZER_H

#include "automix/types.h"
#include <string>
#include <memory>

namespace automix {

/**
 * Audio feature analyzer.
 * Extracts BPM, beat positions, key, MFCC, chroma, and energy curve.
 */
class Analyzer {
public:
    Analyzer();
    ~Analyzer();
    
    // Non-copyable
    Analyzer(const Analyzer&) = delete;
    Analyzer& operator=(const Analyzer&) = delete;
    
    /**
     * Analyze an audio buffer and extract all features.
     */
    Result<TrackFeatures> analyze(const AudioBuffer& audio);
    
    /**
     * Analyze specific features only.
     */
    Result<float> detect_bpm(const AudioBuffer& audio);
    Result<std::vector<float>> detect_beats(const AudioBuffer& audio);
    Result<std::string> detect_key(const AudioBuffer& audio);
    Result<std::vector<float>> compute_mfcc(const AudioBuffer& audio);
    Result<std::vector<float>> compute_chroma(const AudioBuffer& audio);
    Result<std::vector<float>> compute_energy_curve(const AudioBuffer& audio);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace automix

#endif // AUTOMIX_ANALYZER_H
