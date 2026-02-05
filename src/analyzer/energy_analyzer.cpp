/**
 * AutoMix Engine - Energy Analyzer Implementation
 */

#include "energy_analyzer.h"
#include <cmath>
#include <algorithm>

namespace automix {

Result<std::vector<float>> EnergyAnalyzer::compute_curve(const AudioBuffer& audio, float resolution) {
    if (audio.samples.empty()) {
        return "Empty audio buffer";
    }
    
    // Samples per analysis window
    int window_samples = static_cast<int>(resolution * audio.sample_rate) * audio.channels;
    if (window_samples <= 0) {
        return "Invalid resolution";
    }
    
    std::vector<float> energy_curve;
    energy_curve.reserve(audio.samples.size() / window_samples + 1);
    
    // Compute RMS energy for each window
    for (size_t start = 0; start < audio.samples.size(); start += window_samples) {
        size_t end = std::min(start + window_samples, audio.samples.size());
        float rms = compute_rms(&audio.samples[start], end - start);
        energy_curve.push_back(rms);
    }
    
    if (energy_curve.empty()) {
        return "No energy data computed";
    }
    
    // Normalize to 0-1 range
    float max_energy = *std::max_element(energy_curve.begin(), energy_curve.end());
    if (max_energy > 0) {
        for (float& e : energy_curve) {
            e /= max_energy;
        }
    }
    
    // Apply smoothing (simple moving average)
    const int smooth_window = 3;
    std::vector<float> smoothed(energy_curve.size());
    
    for (size_t i = 0; i < energy_curve.size(); ++i) {
        float sum = 0.0f;
        int count = 0;
        
        for (int j = -smooth_window; j <= smooth_window; ++j) {
            int idx = static_cast<int>(i) + j;
            if (idx >= 0 && idx < static_cast<int>(energy_curve.size())) {
                sum += energy_curve[idx];
                count++;
            }
        }
        
        smoothed[i] = sum / count;
    }
    
    return smoothed;
}

std::vector<int> EnergyAnalyzer::find_valleys(const std::vector<float>& energy_curve, float threshold) {
    std::vector<int> valleys;
    
    if (energy_curve.size() < 3) {
        return valleys;
    }
    
    for (size_t i = 1; i < energy_curve.size() - 1; ++i) {
        // Local minimum
        if (energy_curve[i] < energy_curve[i - 1] &&
            energy_curve[i] <= energy_curve[i + 1] &&
            energy_curve[i] < threshold) {
            valleys.push_back(static_cast<int>(i));
        }
    }
    
    return valleys;
}

std::vector<int> EnergyAnalyzer::find_peaks(const std::vector<float>& energy_curve, float threshold) {
    std::vector<int> peaks;
    
    if (energy_curve.size() < 3) {
        return peaks;
    }
    
    for (size_t i = 1; i < energy_curve.size() - 1; ++i) {
        // Local maximum
        if (energy_curve[i] > energy_curve[i - 1] &&
            energy_curve[i] >= energy_curve[i + 1] &&
            energy_curve[i] > threshold) {
            peaks.push_back(static_cast<int>(i));
        }
    }
    
    return peaks;
}

float EnergyAnalyzer::compute_rms(const float* samples, size_t count) {
    if (count == 0 || !samples) {
        return 0.0f;
    }
    
    float sum_sq = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        sum_sq += samples[i] * samples[i];
    }
    
    return std::sqrt(sum_sq / count);
}

} // namespace automix
