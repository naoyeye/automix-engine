/**
 * AutoMix Engine - Deck Implementation
 *
 * Features:
 *   - Volume smoothing (linear ramp per render call to prevent clicks)
 *   - Pre-allocated channel buffers for Rubber Band (no alloc in audio path)
 *   - 3-band EQ via cascaded biquad filters (low-shelf / peaking / high-shelf)
 */

#include "deck.h"
#include "../core/utils.h"

#ifdef AUTOMIX_HAS_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

#include <mutex>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace automix {

// =============================================================================
// Biquad filter — single channel, direct-form II transposed
// =============================================================================

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
};

struct BiquadState {
    float z1 = 0.0f, z2 = 0.0f;
    
    float process(float x, const BiquadCoeffs& c) {
        float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    
    void reset() { z1 = z2 = 0.0f; }
};

// Cook low-shelf biquad coefficients
static BiquadCoeffs make_low_shelf(float sample_rate, float freq, float gain_db) {
    BiquadCoeffs c;
    float A  = std::pow(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * static_cast<float>(M_PI) * freq / sample_rate;
    float cs = std::cos(w0);
    float sn = std::sin(w0);
    float alpha = sn / 2.0f * std::sqrt(2.0f);  // Q = 0.707
    
    float a0 = (A + 1) + (A - 1) * cs + 2 * std::sqrt(A) * alpha;
    c.b0 = A * ((A + 1) - (A - 1) * cs + 2 * std::sqrt(A) * alpha) / a0;
    c.b1 = 2 * A * ((A - 1) - (A + 1) * cs) / a0;
    c.b2 = A * ((A + 1) - (A - 1) * cs - 2 * std::sqrt(A) * alpha) / a0;
    c.a1 = -2 * ((A - 1) + (A + 1) * cs) / a0;
    c.a2 = ((A + 1) + (A - 1) * cs - 2 * std::sqrt(A) * alpha) / a0;
    return c;
}

// Cook high-shelf biquad coefficients
static BiquadCoeffs make_high_shelf(float sample_rate, float freq, float gain_db) {
    BiquadCoeffs c;
    float A  = std::pow(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * static_cast<float>(M_PI) * freq / sample_rate;
    float cs = std::cos(w0);
    float sn = std::sin(w0);
    float alpha = sn / 2.0f * std::sqrt(2.0f);
    
    float a0 = (A + 1) - (A - 1) * cs + 2 * std::sqrt(A) * alpha;
    c.b0 = A * ((A + 1) + (A - 1) * cs + 2 * std::sqrt(A) * alpha) / a0;
    c.b1 = -2 * A * ((A - 1) + (A + 1) * cs) / a0;
    c.b2 = A * ((A + 1) + (A - 1) * cs - 2 * std::sqrt(A) * alpha) / a0;
    c.a1 = 2 * ((A - 1) - (A + 1) * cs) / a0;
    c.a2 = ((A + 1) - (A - 1) * cs - 2 * std::sqrt(A) * alpha) / a0;
    return c;
}

// Cook peaking EQ biquad coefficients
static BiquadCoeffs make_peaking(float sample_rate, float freq, float gain_db, float Q = 1.0f) {
    BiquadCoeffs c;
    float A  = std::pow(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * static_cast<float>(M_PI) * freq / sample_rate;
    float cs = std::cos(w0);
    float sn = std::sin(w0);
    float alpha = sn / (2.0f * Q);
    
    float a0 = 1 + alpha / A;
    c.b0 = (1 + alpha * A) / a0;
    c.b1 = (-2 * cs) / a0;
    c.b2 = (1 - alpha * A) / a0;
    c.a1 = (-2 * cs) / a0;
    c.a2 = (1 - alpha / A) / a0;
    return c;
}

// =============================================================================
// 3-band EQ (per-channel)
// =============================================================================

struct EQ3Band {
    // Coefficients
    BiquadCoeffs low_coeffs, mid_coeffs, high_coeffs;
    // Per-channel state (stereo = 2)
    BiquadState low_state[2], mid_state[2], high_state[2];
    
    float low_db = 0.0f, mid_db = 0.0f, high_db = 0.0f;
    float sample_rate = 44100.0f;
    bool active = false;  // bypass when all gains are 0 dB
    
    void update(float sr, float lo, float mi, float hi) {
        sample_rate = sr;
        low_db = lo; mid_db = mi; high_db = hi;
        active = (std::abs(lo) > 0.01f || std::abs(mi) > 0.01f || std::abs(hi) > 0.01f);
        
        if (active) {
            low_coeffs  = make_low_shelf(sr, 250.0f, lo);
            mid_coeffs  = make_peaking(sr, 1000.0f, mi, 0.7f);
            high_coeffs = make_high_shelf(sr, 4000.0f, hi);
        }
    }
    
    void reset() {
        for (int ch = 0; ch < 2; ++ch) {
            low_state[ch].reset();
            mid_state[ch].reset();
            high_state[ch].reset();
        }
    }
    
    // Process a single sample for given channel
    float process(float x, int channel) {
        if (!active) return x;
        x = low_state[channel].process(x, low_coeffs);
        x = mid_state[channel].process(x, mid_coeffs);
        x = high_state[channel].process(x, high_coeffs);
        return x;
    }
};

// =============================================================================
// Deck::Impl
// =============================================================================

static constexpr int kRubberBandBlockSize = 512;

class Deck::Impl {
public:
    Impl() {
        // Pre-allocate channel buffers for Rubber Band
        ch_buf_l_.resize(kRubberBandBlockSize, 0.0f);
        ch_buf_r_.resize(kRubberBandBlockSize, 0.0f);
    }
    ~Impl() = default;
    
    bool load(const AudioBuffer& audio, int64_t track_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        buffer_ = audio;
        position_ = 0;
        track_id_ = track_id;
        prev_volume_ = -1.0f;  // signal "no previous volume"
        
        eq_.reset();
        eq_.sample_rate = static_cast<float>(buffer_.sample_rate);
        
#ifdef AUTOMIX_HAS_RUBBERBAND
        // Initialize time-stretcher
        stretcher_ = std::make_unique<RubberBand::RubberBandStretcher>(
            buffer_.sample_rate,
            buffer_.channels,
            RubberBand::RubberBandStretcher::OptionProcessRealTime |
            RubberBand::RubberBandStretcher::OptionStretchElastic
        );
        stretcher_->setMaxProcessSize(kRubberBandBlockSize);
#endif
        
        return true;
    }
    
    void unload() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.samples.clear();
        position_ = 0;
        track_id_ = 0;
        prev_volume_ = -1.0f;
        eq_.reset();
        
#ifdef AUTOMIX_HAS_RUBBERBAND
        stretcher_.reset();
#endif
    }
    
    void seek(float position_seconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (buffer_.sample_rate <= 0) return;
        
        size_t frame = static_cast<size_t>(position_seconds * buffer_.sample_rate);
        frame = std::min(frame, buffer_.frame_count());
        position_ = frame * buffer_.channels;
    }
    
    float position() const {
        if (buffer_.sample_rate <= 0 || buffer_.channels <= 0) return 0.0f;
        return static_cast<float>(position_ / buffer_.channels) / buffer_.sample_rate;
    }
    
    float duration() const {
        return buffer_.duration_seconds();
    }
    
    void update_eq(float low_db, float mid_db, float high_db) {
        float sr = buffer_.sample_rate > 0 ? static_cast<float>(buffer_.sample_rate) : 44100.0f;
        eq_.update(sr, low_db, mid_db, high_db);
    }
    
    int render(float* output, int frames, float volume, float stretch_ratio,
               float low_db, float mid_db, float high_db) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (buffer_.samples.empty() || buffer_.channels <= 0) {
            std::memset(output, 0, frames * 2 * sizeof(float));
            return frames;
        }
        
        // Update EQ coefficients if changed
        if (std::abs(low_db - eq_.low_db) > 0.01f ||
            std::abs(mid_db - eq_.mid_db) > 0.01f ||
            std::abs(high_db - eq_.high_db) > 0.01f) {
            float sr = buffer_.sample_rate > 0 ? static_cast<float>(buffer_.sample_rate) : 44100.0f;
            eq_.update(sr, low_db, mid_db, high_db);
        }
        
        // Setup volume ramp
        float vol_start = (prev_volume_ < 0.0f) ? volume : prev_volume_;
        float vol_end = volume;
        prev_volume_ = volume;
        
        int rendered = 0;
        
#ifdef AUTOMIX_HAS_RUBBERBAND
        if (stretcher_ && std::abs(stretch_ratio - 1.0f) > 0.001f) {
            // Use time-stretcher with pre-allocated buffers
            stretcher_->setTimeRatio(1.0 / stretch_ratio);
            
            while (rendered < frames) {
                int available = stretcher_->available();
                if (available > 0) {
                    int to_retrieve = std::min(available, frames - rendered);
                    to_retrieve = std::min(to_retrieve, kRubberBandBlockSize);
                    
                    float* temp_ptrs[2] = {ch_buf_l_.data(), ch_buf_r_.data()};
                    int retrieved = stretcher_->retrieve(temp_ptrs, to_retrieve);
                    
                    // Interleave, apply volume ramp and EQ
                    for (int i = 0; i < retrieved; ++i) {
                        float t = (frames > 1) ? static_cast<float>(rendered + i) / (frames - 1) : 1.0f;
                        float vol = vol_start + t * (vol_end - vol_start);
                        
                        float l = eq_.process(ch_buf_l_[i], 0) * vol;
                        float r = eq_.process(ch_buf_r_[i], 1) * vol;
                        
                        output[(rendered + i) * 2]     = l;
                        output[(rendered + i) * 2 + 1] = r;
                    }
                    
                    rendered += retrieved;
                } else {
                    // Feed more input using pre-allocated buffers
                    int input_frames = kRubberBandBlockSize;
                    size_t samples_needed = input_frames * buffer_.channels;
                    
                    if (position_ + samples_needed > buffer_.samples.size()) {
                        samples_needed = buffer_.samples.size() - position_;
                        input_frames = static_cast<int>(samples_needed / buffer_.channels);
                    }
                    
                    if (input_frames <= 0) break;
                    
                    // Deinterleave into pre-allocated buffers
                    for (int i = 0; i < input_frames; ++i) {
                        ch_buf_l_[i] = buffer_.samples[position_ + i * 2];
                        ch_buf_r_[i] = buffer_.samples[position_ + i * 2 + 1];
                    }
                    
                    const float* in_ptrs[2] = {ch_buf_l_.data(), ch_buf_r_.data()};
                    stretcher_->process(in_ptrs, input_frames, false);
                    
                    position_ += input_frames * buffer_.channels;
                }
            }
        } else
#endif
        {
            // No stretching — direct copy with volume ramp and EQ
            while (rendered < frames && position_ < buffer_.samples.size()) {
                float t = (frames > 1) ? static_cast<float>(rendered) / (frames - 1) : 1.0f;
                float vol = vol_start + t * (vol_end - vol_start);
                
                float l = eq_.process(buffer_.samples[position_], 0) * vol;
                float r = eq_.process(buffer_.samples[position_ + 1], 1) * vol;
                
                output[rendered * 2]     = l;
                output[rendered * 2 + 1] = r;
                
                position_ += buffer_.channels;
                rendered++;
            }
        }
        
        // Zero-fill remaining
        for (int i = rendered; i < frames; ++i) {
            output[i * 2]     = 0.0f;
            output[i * 2 + 1] = 0.0f;
        }
        
        return rendered;
    }
    
    bool is_finished() const {
        return position_ >= buffer_.samples.size();
    }
    
private:
    mutable std::mutex mutex_;
    AudioBuffer buffer_;
    size_t position_{0};
    int64_t track_id_{0};
    float prev_volume_{-1.0f};
    
    // 3-band EQ
    EQ3Band eq_;
    
    // Pre-allocated channel buffers for Rubber Band (avoid alloc in audio path)
    std::vector<float> ch_buf_l_;
    std::vector<float> ch_buf_r_;
    
#ifdef AUTOMIX_HAS_RUBBERBAND
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher_;
#endif
};

// =============================================================================
// Deck public interface
// =============================================================================

Deck::Deck() : impl_(std::make_unique<Impl>()) {}
Deck::~Deck() = default;

bool Deck::load(const AudioBuffer& audio, int64_t track_id) {
    track_id_ = track_id;
    bool result = impl_->load(audio, track_id);
    loaded_ = result;
    return result;
}

void Deck::unload() {
    playing_ = false;
    loaded_ = false;
    track_id_ = 0;
    eq_low_db_ = 0.0f;
    eq_mid_db_ = 0.0f;
    eq_high_db_ = 0.0f;
    impl_->unload();
}

void Deck::play() {
    if (loaded_) {
        playing_ = true;
    }
}

void Deck::pause() {
    playing_ = false;
}

void Deck::seek(float position) {
    impl_->seek(position);
}

float Deck::position() const {
    return impl_->position();
}

float Deck::duration() const {
    return impl_->duration();
}

void Deck::set_stretch_ratio(float ratio) {
    stretch_ratio_ = utils::clamp(ratio, 0.5f, 2.0f);
}

void Deck::set_volume(float volume) {
    volume_ = utils::clamp(volume, 0.0f, 1.0f);
}

void Deck::set_eq(float low_db, float mid_db, float high_db) {
    eq_low_db_  = utils::clamp(low_db,  -60.0f, 12.0f);
    eq_mid_db_  = utils::clamp(mid_db,  -60.0f, 12.0f);
    eq_high_db_ = utils::clamp(high_db, -60.0f, 12.0f);
}

void Deck::get_eq(float& low_db, float& mid_db, float& high_db) const {
    low_db  = eq_low_db_;
    mid_db  = eq_mid_db_;
    high_db = eq_high_db_;
}

int Deck::render(float* output, int frames) {
    if (!playing_ || !loaded_) {
        std::memset(output, 0, frames * 2 * sizeof(float));
        return 0;
    }
    
    return impl_->render(output, frames, volume_, stretch_ratio_,
                         eq_low_db_, eq_mid_db_, eq_high_db_);
}

bool Deck::is_finished() const {
    return impl_->is_finished();
}

} // namespace automix
