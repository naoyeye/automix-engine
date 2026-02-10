/**
 * AutoMix Engine - Audio Analyzer Implementation
 * 
 * Uses Essentia if available, otherwise falls back to simplified analysis.
 */

#include "analyzer.h"
#include "bpm_detector.h"
#include "key_detector.h"
#include "energy_analyzer.h"
#include "../core/utils.h"

#ifdef AUTOMIX_HAS_ESSENTIA
#include <essentia/algorithmfactory.h>
#include <essentia/essentiamath.h>
#include <essentia/pool.h>
#include <mutex>

namespace {
    class EssentiaManager {
    public:
        static EssentiaManager& instance() {
            static EssentiaManager instance;
            return instance;
        }
        
        void ensure_initialized() {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_) {
                essentia::init();
                initialized_ = true;
            }
        }
        
        // We don't call shutdown() in the destructor because it can be tricky 
        // with global statics during program exit. Essentia will clean up 
        // with the process exit.
        
    private:
        EssentiaManager() : initialized_(false) {}
        ~EssentiaManager() = default;
        
        bool initialized_;
        std::mutex mutex_;
    };
}
#endif

#include <cmath>
#include <algorithm>
#include <numeric>

namespace automix {

class Analyzer::Impl {
public:
    Impl() {
#ifdef AUTOMIX_HAS_ESSENTIA
        EssentiaManager::instance().ensure_initialized();
#endif
    }
    
    ~Impl() = default;
    
    Result<TrackFeatures> analyze(const AudioBuffer& audio) {
        TrackFeatures features;
        features.duration = audio.duration_seconds();
        
        // BPM detection
        auto bpm_result = detect_bpm(audio);
        if (bpm_result.ok()) {
            features.bpm = bpm_result.value();
        }
        
        // Beat detection
        auto beats_result = detect_beats(audio);
        if (beats_result.ok()) {
            features.beats = beats_result.value();
        }
        
        // Key detection
        auto key_result = detect_key(audio);
        if (key_result.ok()) {
            features.key = key_result.value();
        }
        
        // MFCC
        auto mfcc_result = compute_mfcc(audio);
        if (mfcc_result.ok()) {
            features.mfcc = mfcc_result.value();
        }
        
        // Chroma
        auto chroma_result = compute_chroma(audio);
        if (chroma_result.ok()) {
            features.chroma = chroma_result.value();
        }
        
        // Energy curve
        auto energy_result = compute_energy_curve(audio);
        if (energy_result.ok()) {
            features.energy_curve = energy_result.value();
        }
        
        return features;
    }
    
    Result<float> detect_bpm(const AudioBuffer& audio) {
#ifdef AUTOMIX_HAS_ESSENTIA
        try {
            using namespace essentia;
            using namespace essentia::standard;
            
            // Convert to mono
            std::vector<Real> mono = to_mono(audio);
            
            AlgorithmFactory& factory = AlgorithmFactory::instance();
            
            // Use RhythmExtractor2013 for robust BPM detection
            Algorithm* rhythm = factory.create("RhythmExtractor2013",
                "method", "multifeature");
                
            std::vector<Real> ticks, estimates, bpmIntervals;
            Real bpm, confidence;
            
            rhythm->input("signal").set(mono);
            rhythm->output("bpm").set(bpm);
            rhythm->output("ticks").set(ticks);
            rhythm->output("confidence").set(confidence);
            rhythm->output("estimates").set(estimates);
            rhythm->output("bpmIntervals").set(bpmIntervals);
            
            rhythm->compute();
            
            delete rhythm;
            
            if (bpm > 0) {
                auto octave_distance = [](float a, float b) -> float {
                    if (a <= 0.0f || b <= 0.0f) return 1000.0f;
                    return std::abs(std::log2(a / b));
                };
                
                auto range_prior = [](float candidate) -> float {
                    // Prefer beat-level tempo range while still allowing outliers.
                    if (candidate >= 70.0f && candidate <= 150.0f) return 1.0f;
                    if (candidate < 70.0f) return std::exp(-(70.0f - candidate) / 12.0f);
                    return std::exp(-(candidate - 150.0f) / 18.0f);
                };
                
                auto support_score = [&](float candidate) -> float {
                    if (candidate <= 0.0f || estimates.empty()) return 0.0f;
                    float score = 0.0f;
                    for (auto e : estimates) {
                        float est = static_cast<float>(e);
                        if (est <= 0.0f) continue;
                        score += std::exp(-8.0f * octave_distance(est, candidate));
                    }
                    return score;
                };
                
                std::vector<float> candidates;
                float raw_bpm = static_cast<float>(bpm);
                candidates.push_back(raw_bpm);
                if (raw_bpm * 0.5f >= 40.0f) candidates.push_back(raw_bpm * 0.5f);
                if (raw_bpm * 2.0f <= 220.0f) candidates.push_back(raw_bpm * 2.0f);
                
                std::sort(candidates.begin(), candidates.end());
                candidates.erase(
                    std::unique(candidates.begin(), candidates.end(),
                        [](float a, float b) { return std::abs(a - b) < 0.01f; }),
                    candidates.end()
                );
                
                float best_bpm = raw_bpm;
                float best_score = -1.0f;
                float conf = std::clamp(static_cast<float>(confidence), 0.1f, 1.0f);
                for (float candidate : candidates) {
                    float score = support_score(candidate) * conf
                                + range_prior(candidate) * (1.2f - conf);
                    if (score > best_score) {
                        best_score = score;
                        best_bpm = candidate;
                    }
                }
                
                return best_bpm;
            }
            
        } catch (const std::exception& e) {
            // Fallback to internal detector if Essentia fails
        }
#endif
        return bpm_detector_.detect(audio);
    }
    
    Result<std::vector<float>> detect_beats(const AudioBuffer& audio) {
        return bpm_detector_.detect_beats(audio);
    }
    
    Result<std::string> detect_key(const AudioBuffer& audio) {
        return key_detector_.detect(audio);
    }
    
    Result<std::vector<float>> compute_mfcc(const AudioBuffer& audio) {
#ifdef AUTOMIX_HAS_ESSENTIA
        return compute_mfcc_essentia(audio);
#else
        return compute_mfcc_simple(audio);
#endif
    }
    
    Result<std::vector<float>> compute_chroma(const AudioBuffer& audio) {
        return key_detector_.compute_chroma(audio);
    }
    
    Result<std::vector<float>> compute_energy_curve(const AudioBuffer& audio) {
        return energy_analyzer_.compute_curve(audio);
    }
    
private:
    BPMDetector bpm_detector_;
    KeyDetector key_detector_;
    EnergyAnalyzer energy_analyzer_;
    
#ifdef AUTOMIX_HAS_ESSENTIA
    Result<std::vector<float>> compute_mfcc_essentia(const AudioBuffer& audio) {
        try {
            using namespace essentia;
            using namespace essentia::standard;
            
            // Convert to mono
            std::vector<Real> mono = to_mono(audio);
            
            AlgorithmFactory& factory = AlgorithmFactory::instance();
            
            // Frame cutting
            Algorithm* fc = factory.create("FrameCutter",
                "frameSize", 2048,
                "hopSize", 1024,
                "startFromZero", true);
            
            // Windowing
            Algorithm* w = factory.create("Windowing",
                "type", "hann",
                "zeroPadding", 0);
            
            // Spectrum
            Algorithm* spec = factory.create("Spectrum",
                "size", 2048);
            
            // MFCC
            Algorithm* mfcc = factory.create("MFCC",
                "inputSize", 1025,
                "numberCoefficients", 13,
                "numberBands", 40,
                "lowFrequencyBound", 0,
                "highFrequencyBound", audio.sample_rate / 2.0f);
            
            std::vector<Real> frame, windowed, spectrum, mfcc_bands, mfcc_coeffs;
            std::vector<std::vector<Real>> all_mfcc;
            
            fc->input("signal").set(mono);
            fc->output("frame").set(frame);
            
            w->input("frame").set(frame);
            w->output("frame").set(windowed);
            
            spec->input("frame").set(windowed);
            spec->output("spectrum").set(spectrum);
            
            mfcc->input("spectrum").set(spectrum);
            mfcc->output("bands").set(mfcc_bands);
            mfcc->output("mfcc").set(mfcc_coeffs);
            
            while (true) {
                fc->compute();
                if (frame.empty() || std::all_of(frame.begin(), frame.end(), 
                    [](Real v) { return v == 0; })) break;
                
                w->compute();
                spec->compute();
                mfcc->compute();
                
                all_mfcc.push_back(mfcc_coeffs);
            }
            
            delete fc;
            delete w;
            delete spec;
            delete mfcc;
            
            // Average MFCC
            if (all_mfcc.empty()) return std::vector<float>(13, 0.0f);
            
            std::vector<float> mean_mfcc(13, 0.0f);
            for (const auto& m : all_mfcc) {
                for (size_t i = 0; i < 13 && i < m.size(); ++i) {
                    mean_mfcc[i] += m[i];
                }
            }
            for (auto& v : mean_mfcc) {
                v /= all_mfcc.size();
            }
            
            return mean_mfcc;
        } catch (const std::exception& e) {
            return std::string("MFCC computation failed: ") + e.what();
        }
    }
    
    std::vector<essentia::Real> to_mono(const AudioBuffer& audio) {
        auto mono_float = audio.to_mono();
        return std::vector<essentia::Real>(mono_float.begin(), mono_float.end());
    }
#endif
    
    Result<std::vector<float>> compute_mfcc_simple(const AudioBuffer& audio) {
        // Simplified MFCC - just compute basic spectral features
        // This is a placeholder; real MFCC requires mel filterbanks
        
        const size_t frame_size = 2048;
        const size_t hop_size = 1024;
        
        if (audio.samples.size() < frame_size * 2) {
            return std::vector<float>(13, 0.0f);
        }
        
        // Convert to mono (no-op if already mono)
        auto mono = audio.to_mono();
        
        // Simple spectral centroid as proxy for MFCC[0]
        std::vector<float> mfcc(13, 0.0f);
        
        float total_energy = 0.0f;
        float weighted_freq = 0.0f;
        
        for (size_t i = 0; i < mono.size(); ++i) {
            float energy = mono[i] * mono[i];
            total_energy += energy;
        }
        
        if (total_energy > 0) {
            mfcc[0] = std::log(total_energy / mono.size() + 1e-10f);
        }
        
        // Fill remaining coefficients with simple statistics
        float sum = 0.0f, sum_sq = 0.0f;
        for (float v : mono) {
            sum += v;
            sum_sq += v * v;
        }
        float mean = sum / mono.size();
        float variance = sum_sq / mono.size() - mean * mean;
        
        mfcc[1] = mean;
        mfcc[2] = std::sqrt(std::max(0.0f, variance));
        
        return mfcc;
    }
};

Analyzer::Analyzer() : impl_(std::make_unique<Impl>()) {}
Analyzer::~Analyzer() = default;

Result<TrackFeatures> Analyzer::analyze(const AudioBuffer& audio) {
    return impl_->analyze(audio);
}

Result<float> Analyzer::detect_bpm(const AudioBuffer& audio) {
    return impl_->detect_bpm(audio);
}

Result<std::vector<float>> Analyzer::detect_beats(const AudioBuffer& audio) {
    return impl_->detect_beats(audio);
}

Result<std::string> Analyzer::detect_key(const AudioBuffer& audio) {
    return impl_->detect_key(audio);
}

Result<std::vector<float>> Analyzer::compute_mfcc(const AudioBuffer& audio) {
    return impl_->compute_mfcc(audio);
}

Result<std::vector<float>> Analyzer::compute_chroma(const AudioBuffer& audio) {
    return impl_->compute_chroma(audio);
}

Result<std::vector<float>> Analyzer::compute_energy_curve(const AudioBuffer& audio) {
    return impl_->compute_energy_curve(audio);
}

} // namespace automix
