/**
 * AutoMix Engine - Key Detector Implementation
 */

#include "key_detector.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace automix {

// Krumhansl-Kessler key profiles (normalized)
const float KeyDetector::major_profile_[12] = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
    2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
};

const float KeyDetector::minor_profile_[12] = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
    2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
};

Result<std::string> KeyDetector::detect(const AudioBuffer& audio) {
    auto chroma_result = compute_chroma(audio);
    if (chroma_result.failed()) {
        return ResultError{chroma_result.error()};
    }
    
    const auto& chroma = chroma_result.value();
    if (chroma.size() != 12) {
        return ResultError{"Invalid chroma vector"};
    }
    
    // Find best matching key
    float best_correlation = -1.0f;
    int best_pitch_class = 0;
    bool best_is_major = true;
    
    for (int shift = 0; shift < 12; ++shift) {
        // Try major key
        float major_corr = correlate_with_profile(chroma, major_profile_, shift);
        if (major_corr > best_correlation) {
            best_correlation = major_corr;
            best_pitch_class = shift;
            best_is_major = true;
        }
        
        // Try minor key
        float minor_corr = correlate_with_profile(chroma, minor_profile_, shift);
        if (minor_corr > best_correlation) {
            best_correlation = minor_corr;
            best_pitch_class = shift;
            best_is_major = false;
        }
    }
    
    return pitch_class_to_camelot(best_pitch_class, best_is_major);
}

Result<std::vector<float>> KeyDetector::compute_chroma(const AudioBuffer& audio) {
    if (audio.samples.empty()) {
        return "Empty audio buffer";
    }
    
    // Convert to mono
    std::vector<float> mono(audio.frame_count());
    for (size_t i = 0; i < audio.frame_count(); ++i) {
        mono[i] = (audio.samples[i * 2] + audio.samples[i * 2 + 1]) / 2.0f;
    }
    
    const int frame_size = 4096;
    const int hop_size = 2048;
    
    if (mono.size() < static_cast<size_t>(frame_size)) {
        return std::vector<float>(12, 1.0f / 12.0f);  // Uniform if too short
    }
    
    // Accumulate chroma
    std::vector<float> chroma(12, 0.0f);
    int frame_count = 0;
    
    // Hann window
    std::vector<float> window(frame_size);
    for (int i = 0; i < frame_size; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (frame_size - 1)));
    }
    
    // Reference frequency for A4 (440 Hz)
    const float a4_freq = 440.0f;
    const int a4_midi = 69;
    
    // Frequency bins to pitch class mapping
    std::vector<int> bin_to_pitch(frame_size / 2 + 1, -1);
    for (int bin = 1; bin < frame_size / 2 + 1; ++bin) {
        float freq = static_cast<float>(bin) * audio.sample_rate / frame_size;
        if (freq > 20.0f && freq < 5000.0f) {
            // Convert frequency to MIDI note number
            float midi_note = 12.0f * std::log2(freq / a4_freq) + a4_midi;
            int pitch_class = static_cast<int>(std::round(midi_note)) % 12;
            if (pitch_class < 0) pitch_class += 12;
            bin_to_pitch[bin] = pitch_class;
        }
    }
    
    for (size_t start = 0; start + frame_size <= mono.size(); start += hop_size) {
        // Apply window
        std::vector<float> windowed(frame_size);
        for (int i = 0; i < frame_size; ++i) {
            windowed[i] = mono[start + i] * window[i];
        }
        
        // Simple DFT for magnitude spectrum (simplified - production would use FFT)
        std::vector<float> magnitude(frame_size / 2 + 1, 0.0f);
        
        for (int k = 0; k < frame_size / 2 + 1; ++k) {
            float real = 0.0f, imag = 0.0f;
            for (int n = 0; n < frame_size; ++n) {
                float angle = -2.0f * M_PI * k * n / frame_size;
                real += windowed[n] * std::cos(angle);
                imag += windowed[n] * std::sin(angle);
            }
            magnitude[k] = std::sqrt(real * real + imag * imag);
        }
        
        // Accumulate into chroma bins
        for (int bin = 0; bin < frame_size / 2 + 1; ++bin) {
            int pitch_class = bin_to_pitch[bin];
            if (pitch_class >= 0 && pitch_class < 12) {
                chroma[pitch_class] += magnitude[bin] * magnitude[bin];
            }
        }
        
        frame_count++;
        
        // Limit processing for long files
        if (frame_count > 1000) break;
    }
    
    // Normalize
    float sum = std::accumulate(chroma.begin(), chroma.end(), 0.0f);
    if (sum > 0) {
        for (float& v : chroma) {
            v /= sum;
        }
    }
    
    return chroma;
}

float KeyDetector::correlate_with_profile(const std::vector<float>& chroma, const float* profile, int shift) {
    // Pearson correlation coefficient
    float chroma_mean = std::accumulate(chroma.begin(), chroma.end(), 0.0f) / 12.0f;
    
    float profile_sum = 0.0f;
    for (int i = 0; i < 12; ++i) profile_sum += profile[i];
    float profile_mean = profile_sum / 12.0f;
    
    float numerator = 0.0f;
    float chroma_var = 0.0f;
    float profile_var = 0.0f;
    
    for (int i = 0; i < 12; ++i) {
        int shifted_i = (i + shift) % 12;
        float c = chroma[shifted_i] - chroma_mean;
        float p = profile[i] - profile_mean;
        
        numerator += c * p;
        chroma_var += c * c;
        profile_var += p * p;
    }
    
    float denominator = std::sqrt(chroma_var * profile_var);
    if (denominator < 1e-10f) return 0.0f;
    
    return numerator / denominator;
}

std::string KeyDetector::pitch_class_to_camelot(int pitch_class, bool is_major) {
    // Camelot wheel mapping
    // Major keys (B): C=8B, C#=3B, D=10B, D#=5B, E=12B, F=7B, F#=2B, G=9B, G#=4B, A=11B, A#=6B, B=1B
    // Minor keys (A): C=5A, C#=12A, D=7A, D#=2A, E=9A, F=4A, F#=11A, G=6A, G#=1A, A=8A, A#=3A, B=10A
    
    static const int major_camelot[] = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};
    static const int minor_camelot[] = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};
    
    int camelot_num;
    char mode_char;
    
    if (is_major) {
        camelot_num = major_camelot[pitch_class];
        mode_char = 'B';
    } else {
        camelot_num = minor_camelot[pitch_class];
        mode_char = 'A';
    }
    
    return std::to_string(camelot_num) + mode_char;
}

} // namespace automix
