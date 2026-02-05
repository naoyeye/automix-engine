/**
 * AutoMix Engine - Deck Implementation
 */

#include "deck.h"
#include "../core/utils.h"

#ifdef AUTOMIX_HAS_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

#include <mutex>
#include <cstring>

namespace automix {

class Deck::Impl {
public:
    Impl() = default;
    ~Impl() = default;
    
    bool load(const AudioBuffer& audio, int64_t track_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        buffer_ = audio;
        position_ = 0;
        track_id_ = track_id;
        
#ifdef AUTOMIX_HAS_RUBBERBAND
        // Initialize time-stretcher
        stretcher_ = std::make_unique<RubberBand::RubberBandStretcher>(
            buffer_.sample_rate,
            buffer_.channels,
            RubberBand::RubberBandStretcher::OptionProcessRealTime |
            RubberBand::RubberBandStretcher::OptionStretchElastic
        );
        stretcher_->setMaxProcessSize(1024);
#endif
        
        return true;
    }
    
    void unload() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.samples.clear();
        position_ = 0;
        track_id_ = 0;
        
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
    
    int render(float* output, int frames, float volume, float stretch_ratio) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (buffer_.samples.empty() || buffer_.channels <= 0) {
            std::memset(output, 0, frames * 2 * sizeof(float));
            return frames;
        }
        
        int rendered = 0;
        
#ifdef AUTOMIX_HAS_RUBBERBAND
        if (stretcher_ && std::abs(stretch_ratio - 1.0f) > 0.001f) {
            // Use time-stretcher
            stretcher_->setTimeRatio(1.0 / stretch_ratio);
            
            // Feed input and retrieve output
            while (rendered < frames) {
                // Check if we have output available
                int available = stretcher_->available();
                if (available > 0) {
                    int to_retrieve = std::min(available, frames - rendered);
                    float* out_ptrs[2] = {output + rendered * 2, output + rendered * 2 + 1};
                    
                    // Retrieve as interleaved
                    std::vector<float> temp_l(to_retrieve), temp_r(to_retrieve);
                    float* temp_ptrs[2] = {temp_l.data(), temp_r.data()};
                    
                    int retrieved = stretcher_->retrieve(temp_ptrs, to_retrieve);
                    
                    // Interleave and apply volume
                    for (int i = 0; i < retrieved; ++i) {
                        output[(rendered + i) * 2] = temp_l[i] * volume;
                        output[(rendered + i) * 2 + 1] = temp_r[i] * volume;
                    }
                    
                    rendered += retrieved;
                } else {
                    // Need to feed more input
                    int input_frames = 512;
                    size_t samples_needed = input_frames * buffer_.channels;
                    
                    if (position_ + samples_needed > buffer_.samples.size()) {
                        samples_needed = buffer_.samples.size() - position_;
                        input_frames = samples_needed / buffer_.channels;
                    }
                    
                    if (input_frames <= 0) {
                        // End of buffer
                        break;
                    }
                    
                    // Deinterleave
                    std::vector<float> in_l(input_frames), in_r(input_frames);
                    for (int i = 0; i < input_frames; ++i) {
                        in_l[i] = buffer_.samples[position_ + i * 2];
                        in_r[i] = buffer_.samples[position_ + i * 2 + 1];
                    }
                    
                    const float* in_ptrs[2] = {in_l.data(), in_r.data()};
                    stretcher_->process(in_ptrs, input_frames, false);
                    
                    position_ += input_frames * buffer_.channels;
                }
            }
        } else
#endif
        {
            // No stretching - direct copy
            while (rendered < frames && position_ < buffer_.samples.size()) {
                output[rendered * 2] = buffer_.samples[position_] * volume;
                output[rendered * 2 + 1] = buffer_.samples[position_ + 1] * volume;
                
                position_ += buffer_.channels;
                rendered++;
            }
        }
        
        // Zero-fill remaining
        for (int i = rendered; i < frames; ++i) {
            output[i * 2] = 0.0f;
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
    
#ifdef AUTOMIX_HAS_RUBBERBAND
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher_;
#endif
};

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

int Deck::render(float* output, int frames) {
    if (!playing_ || !loaded_) {
        std::memset(output, 0, frames * 2 * sizeof(float));
        return 0;
    }
    
    return impl_->render(output, frames, volume_, stretch_ratio_);
}

bool Deck::is_finished() const {
    return impl_->is_finished();
}

} // namespace automix
