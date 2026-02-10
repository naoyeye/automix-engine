/**
 * AutoMix Engine - Audio Output Implementation
 *
 * macOS/iOS: CoreAudio AudioUnit
 * Other:     Stub (render callback not driven; user must call render manually)
 */

#include "audio_output.h"
#include <cstring>
#include <vector>
#include <cstdio>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#endif

namespace automix {

// =============================================================================
// Platform implementation
// =============================================================================

#ifdef __APPLE__

// ---- macOS / iOS CoreAudio implementation -----------------------------------

class AudioOutput::Impl {
public:
    Impl(AudioOutput* owner, int sample_rate, int buffer_size)
        : owner_(owner)
        , sample_rate_(sample_rate)
        , buffer_size_(buffer_size)
        , audio_unit_(nullptr) {}
    
    ~Impl() {
        stop();
    }
    
    bool start() {
        if (audio_unit_) return true;  // already running
        const int requested_sample_rate = (sample_rate_ > 0) ? sample_rate_ : 44100;
        
        // Describe the output audio unit
        AudioComponentDescription desc = {};
        desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IPHONE
        desc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
        desc.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        
        AudioComponent component = AudioComponentFindNext(nullptr, &desc);
        if (!component) return false;
        
        OSStatus status = AudioComponentInstanceNew(component, &audio_unit_);
        if (status != noErr) return false;
        
        // Set client stream format (engine side): stereo float32 non-interleaved.
        // This matches CoreAudio's common native layout and avoids implicit layout conversion.
        AudioStreamBasicDescription format = {};
        format.mSampleRate       = requested_sample_rate;
        format.mFormatID         = kAudioFormatLinearPCM;
        format.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved;
        format.mBytesPerPacket   = sizeof(float);
        format.mFramesPerPacket  = 1;
        format.mBytesPerFrame    = sizeof(float);
        format.mChannelsPerFrame = 2;
        format.mBitsPerChannel   = 32;
        
        status = AudioUnitSetProperty(
            audio_unit_,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input,
            0,  // output bus
            &format,
            sizeof(format)
        );
        if (status != noErr) {
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
            return false;
        }
        
        // Set render callback
        AURenderCallbackStruct callback_struct = {};
        callback_struct.inputProc = render_callback;
        callback_struct.inputProcRefCon = this;
        
        status = AudioUnitSetProperty(
            audio_unit_,
            kAudioUnitProperty_SetRenderCallback,
            kAudioUnitScope_Input,
            0,
            &callback_struct,
            sizeof(callback_struct)
        );
        if (status != noErr) {
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
            return false;
        }
        
        // Set preferred buffer size (best-effort)
        UInt32 preferred_frames = static_cast<UInt32>(buffer_size_);
        AudioUnitSetProperty(
            audio_unit_,
            kAudioUnitProperty_MaximumFramesPerSlice,
            kAudioUnitScope_Global,
            0,
            &preferred_frames,
            sizeof(preferred_frames)
        );
        
        // Initialize and start
        status = AudioUnitInitialize(audio_unit_);
        if (status != noErr) {
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
            return false;
        }
        
        status = AudioOutputUnitStart(audio_unit_);
        if (status != noErr) {
            AudioUnitUninitialize(audio_unit_);
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
            return false;
        }
        
        // Read back both client-side and device-side stream formats.
        // Some systems run device at a different sample rate than the client format.
        AudioStreamBasicDescription client_format = {};
        UInt32 client_size = sizeof(client_format);
        status = AudioUnitGetProperty(
            audio_unit_,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input,
            0,
            &client_format,
            &client_size
        );
        if (status == noErr && client_format.mSampleRate > 0) {
            client_sample_rate_ = static_cast<int>(client_format.mSampleRate);
        } else {
            client_sample_rate_ = sample_rate_;
        }
        
        AudioStreamBasicDescription device_format = {};
        UInt32 device_size = sizeof(device_format);
        status = AudioUnitGetProperty(
            audio_unit_,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Output,
            0,
            &device_format,
            &device_size
        );
        if (status == noErr && device_format.mSampleRate > 0) {
            device_sample_rate_ = static_cast<int>(device_format.mSampleRate);
        } else {
            device_sample_rate_ = client_sample_rate_;
        }
        
        // Render callback timing follows the output side rate on some setups.
        // Expose the device side as the effective rate to the engine.
        sample_rate_ = device_sample_rate_;
        
        std::fprintf(stderr,
            "[AudioOutput] requested=%dHz client=%dHz device=%dHz\n",
            requested_sample_rate, client_sample_rate_, device_sample_rate_);
        
        return true;
    }
    
    void stop() {
        if (audio_unit_) {
            AudioOutputUnitStop(audio_unit_);
            AudioUnitUninitialize(audio_unit_);
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
        }
    }
    
private:
    static OSStatus render_callback(
        void* inRefCon,
        AudioUnitRenderActionFlags* ioActionFlags,
        const AudioTimeStamp* inTimeStamp,
        UInt32 inBusNumber,
        UInt32 inNumberFrames,
        AudioBufferList* ioData
    ) {
        auto* self = static_cast<Impl*>(inRefCon);
        (void)ioActionFlags;
        (void)inTimeStamp;
        (void)inBusNumber;
        
        int frames = static_cast<int>(inNumberFrames);
        
        if (!self->owner_->render_callback_ || !ioData || ioData->mNumberBuffers == 0) {
            for (UInt32 b = 0; ioData && b < ioData->mNumberBuffers; ++b) {
                std::memset(ioData->mBuffers[b].mData, 0, ioData->mBuffers[b].mDataByteSize);
            }
            return noErr;
        }
        
        // Single-buffer output: render interleaved directly.
        if (ioData->mNumberBuffers == 1) {
            float* interleaved = static_cast<float*>(ioData->mBuffers[0].mData);
            self->owner_->render_callback_(interleaved, frames);
            return noErr;
        }
        
        // Non-interleaved output (typically 2 buffers): render interleaved temp and split.
        if (ioData->mNumberBuffers >= 2) {
            thread_local std::vector<float> interleaved_tmp;
            interleaved_tmp.resize(static_cast<size_t>(frames) * 2, 0.0f);
            self->owner_->render_callback_(interleaved_tmp.data(), frames);
            
            float* left = static_cast<float*>(ioData->mBuffers[0].mData);
            float* right = static_cast<float*>(ioData->mBuffers[1].mData);
            for (int i = 0; i < frames; ++i) {
                left[i] = interleaved_tmp[static_cast<size_t>(i) * 2];
                right[i] = interleaved_tmp[static_cast<size_t>(i) * 2 + 1];
            }
            return noErr;
        }
        
        return noErr;
    }
    
    AudioOutput* owner_;
    int sample_rate_;
    int client_sample_rate_{44100};
    int device_sample_rate_{44100};
    int buffer_size_;
    AudioComponentInstance audio_unit_;
};

#else

// ---- Stub implementation for non-Apple platforms ----------------------------

class AudioOutput::Impl {
public:
    Impl(AudioOutput* /*owner*/, int /*sample_rate*/, int /*buffer_size*/) {}
    ~Impl() = default;
    
    bool start() {
        // No platform audio available — user must call Engine::render() manually
        return false;
    }
    
    void stop() {}
};

#endif

// =============================================================================
// AudioOutput public interface
// =============================================================================

AudioOutput::AudioOutput(int sample_rate, int buffer_size)
    : sample_rate_(sample_rate)
    , buffer_size_(buffer_size)
    , impl_(std::make_unique<Impl>(this, sample_rate, buffer_size)) {}

AudioOutput::~AudioOutput() {
    stop();
}

void AudioOutput::set_render_callback(AudioRenderCallback callback) {
    render_callback_ = std::move(callback);
}

bool AudioOutput::start() {
    if (running_) return true;
    
    bool ok = impl_->start();
    running_ = ok;
    return ok;
}

void AudioOutput::stop() {
    if (!running_) return;
    
    impl_->stop();
    running_ = false;
}

} // namespace automix
