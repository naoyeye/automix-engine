/**
 * AutoMix Engine - BPM Detector Implementation
 */

#include "bpm_detector.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace automix {

Result<float> BPMDetector::detect(const AudioBuffer& audio) {
    if (audio.samples.empty()) {
        return "Empty audio buffer";
    }
    
    auto onset_envelope = compute_onset_envelope(audio);
    if (onset_envelope.empty()) {
        return "Failed to compute onset envelope";
    }
    
    // Onset envelope is at a reduced sample rate
    int onset_sr = audio.sample_rate / 512;  // hop size
    float bpm = estimate_bpm_autocorr(onset_envelope, onset_sr);
    
    // Validate BPM range
    if (bpm < 40.0f) bpm *= 2.0f;
    if (bpm > 220.0f) bpm /= 2.0f;
    
    return bpm;
}

Result<std::vector<float>> BPMDetector::detect_beats(const AudioBuffer& audio) {
    if (audio.samples.empty()) {
        return "Empty audio buffer";
    }
    
    auto onset_envelope = compute_onset_envelope(audio);
    if (onset_envelope.empty()) {
        return "Failed to compute onset envelope";
    }
    
    // Get BPM first
    int onset_sr = audio.sample_rate / 512;
    float bpm = estimate_bpm_autocorr(onset_envelope, onset_sr);
    if (bpm < 40.0f) bpm *= 2.0f;
    if (bpm > 220.0f) bpm /= 2.0f;
    
    // Expected samples between beats
    float beat_period_samples = (60.0f / bpm) * onset_sr;
    int min_distance = static_cast<int>(beat_period_samples * 0.7f);
    
    // Compute adaptive threshold
    float mean = std::accumulate(onset_envelope.begin(), onset_envelope.end(), 0.0f) / onset_envelope.size();
    float sq_sum = 0.0f;
    for (float v : onset_envelope) {
        sq_sum += (v - mean) * (v - mean);
    }
    float std_dev = std::sqrt(sq_sum / onset_envelope.size());
    float threshold = mean + 0.5f * std_dev;
    
    // Pick peaks
    auto peak_indices = pick_peaks(onset_envelope, threshold, min_distance);
    
    // Convert to seconds
    float hop_duration = 512.0f / audio.sample_rate;
    std::vector<float> beat_times;
    beat_times.reserve(peak_indices.size());
    
    for (float idx : peak_indices) {
        beat_times.push_back(idx * hop_duration);
    }
    
    return beat_times;
}

std::vector<float> BPMDetector::compute_onset_envelope(const AudioBuffer& audio) {
    const int frame_size = 1024;
    const int hop_size = 512;
    
    // Convert to mono
    std::vector<float> mono(audio.frame_count());
    for (size_t i = 0; i < audio.frame_count(); ++i) {
        mono[i] = (audio.samples[i * 2] + audio.samples[i * 2 + 1]) / 2.0f;
    }
    
    if (mono.size() < static_cast<size_t>(frame_size)) {
        return {};
    }
    
    // Compute spectral flux
    std::vector<float> envelope;
    envelope.reserve(mono.size() / hop_size);
    
    std::vector<float> prev_spectrum(frame_size / 2 + 1, 0.0f);
    std::vector<float> window(frame_size);
    
    // Hann window
    for (int i = 0; i < frame_size; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (frame_size - 1)));
    }
    
    for (size_t start = 0; start + frame_size <= mono.size(); start += hop_size) {
        // Apply window and compute energy in bands
        std::vector<float> spectrum(frame_size / 2 + 1, 0.0f);
        
        // Simplified: just compute energy in frequency bands
        float low_energy = 0.0f;
        float mid_energy = 0.0f;
        float high_energy = 0.0f;
        
        for (int i = 0; i < frame_size; ++i) {
            float sample = mono[start + i] * window[i];
            float energy = sample * sample;
            
            // Rough frequency band assignment
            if (i < frame_size / 8) {
                low_energy += energy;
            } else if (i < frame_size / 2) {
                mid_energy += energy;
            } else {
                high_energy += energy;
            }
        }
        
        spectrum[0] = low_energy;
        spectrum[1] = mid_energy;
        spectrum[2] = high_energy;
        
        // Spectral flux (half-wave rectified difference)
        float flux = 0.0f;
        for (size_t i = 0; i < 3; ++i) {
            float diff = spectrum[i] - prev_spectrum[i];
            if (diff > 0) flux += diff;
        }
        
        envelope.push_back(flux);
        prev_spectrum = spectrum;
    }
    
    // Normalize
    float max_val = *std::max_element(envelope.begin(), envelope.end());
    if (max_val > 0) {
        for (float& v : envelope) {
            v /= max_val;
        }
    }
    
    return envelope;
}

float BPMDetector::estimate_bpm_autocorr(const std::vector<float>& onset_envelope, int sample_rate) {
    if (onset_envelope.size() < 100) {
        return 120.0f;  // Default
    }
    
    // BPM range: 60-200 -> period in samples
    int min_lag = static_cast<int>(sample_rate * 60.0f / 200.0f);
    int max_lag = static_cast<int>(sample_rate * 60.0f / 60.0f);
    
    max_lag = std::min(max_lag, static_cast<int>(onset_envelope.size() / 2));
    min_lag = std::max(min_lag, 1);
    
    // Compute autocorrelation
    float best_corr = -1.0f;
    int best_lag = min_lag;
    
    for (int lag = min_lag; lag <= max_lag; ++lag) {
        float corr = 0.0f;
        int count = 0;
        
        for (size_t i = 0; i < onset_envelope.size() - lag; ++i) {
            corr += onset_envelope[i] * onset_envelope[i + lag];
            count++;
        }
        
        if (count > 0) {
            corr /= count;
            
            if (corr > best_corr) {
                best_corr = corr;
                best_lag = lag;
            }
        }
    }
    
    // Convert lag to BPM
    float bpm = (sample_rate * 60.0f) / best_lag;
    
    return bpm;
}

std::vector<float> BPMDetector::pick_peaks(const std::vector<float>& envelope, float threshold, int min_distance) {
    std::vector<float> peaks;
    
    for (size_t i = 1; i < envelope.size() - 1; ++i) {
        if (envelope[i] > threshold &&
            envelope[i] > envelope[i - 1] &&
            envelope[i] >= envelope[i + 1]) {
            
            // Check minimum distance from last peak
            if (peaks.empty() || (i - peaks.back()) >= static_cast<size_t>(min_distance)) {
                peaks.push_back(static_cast<float>(i));
            } else if (envelope[i] > envelope[static_cast<size_t>(peaks.back())]) {
                // Replace last peak if this one is stronger
                peaks.back() = static_cast<float>(i);
            }
        }
    }
    
    return peaks;
}

} // namespace automix
