/**
 * AutoMix Engine - Audio Output Implementation
 *
 * macOS/iOS: CoreAudio AudioUnit
 * Other:     Stub (render callback not driven; user must call render manually)
 */

#include "audio_output.h"
#include <cstring>

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
        
        // Set stream format: stereo float32 non-interleaved -> we'll do interleaved in callback
        AudioStreamBasicDescription format = {};
        format.mSampleRate       = sample_rate_;
        format.mFormatID         = kAudioFormatLinearPCM;
        format.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        format.mBytesPerPacket   = sizeof(float) * 2;
        format.mFramesPerPacket  = 1;
        format.mBytesPerFrame    = sizeof(float) * 2;
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
        
        // Output buffer
        float* buffer = static_cast<float*>(ioData->mBuffers[0].mData);
        int frames = static_cast<int>(inNumberFrames);
        
        if (self->owner_->render_callback_) {
            self->owner_->render_callback_(buffer, frames);
        } else {
            std::memset(buffer, 0, frames * 2 * sizeof(float));
        }
        
        return noErr;
    }
    
    AudioOutput* owner_;
    int sample_rate_;
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
