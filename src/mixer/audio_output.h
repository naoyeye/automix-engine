/**
 * AutoMix Engine - Audio Output
 *
 * Platform audio output layer. Drives the audio callback that pulls
 * rendered samples from the mixer engine.
 *
 * macOS/iOS: CoreAudio AudioUnit (DefaultOutput / RemoteIO)
 * Other platforms: stub (users provide their own audio callback)
 */

#ifndef AUTOMIX_AUDIO_OUTPUT_H
#define AUTOMIX_AUDIO_OUTPUT_H

#include <functional>
#include <memory>
#include <atomic>

namespace automix {

/**
 * Render callback signature.
 * @param buffer  Interleaved stereo float32 output buffer.
 * @param frames  Number of frames to fill.
 * @return  Number of frames actually rendered.
 */
using AudioRenderCallback = std::function<int(float* buffer, int frames)>;

/**
 * AudioOutput manages a platform audio device and drives
 * the render callback from the audio thread.
 */
class AudioOutput {
public:
    /**
     * @param sample_rate   Desired output sample rate (e.g. 44100).
     * @param buffer_size   Preferred hardware buffer size in frames (e.g. 512).
     */
    explicit AudioOutput(int sample_rate = 44100, int buffer_size = 512);
    ~AudioOutput();
    
    // Non-copyable
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    
    /**
     * Set the render callback.
     * Must be called before start().
     */
    void set_render_callback(AudioRenderCallback callback);
    
    /**
     * Start audio output.
     * @return true on success.
     */
    bool start();
    
    /**
     * Stop audio output.
     */
    void stop();
    
    /**
     * Check if audio output is running.
     */
    bool is_running() const { return running_; }
    
    /**
     * Get actual sample rate (may differ from requested).
     */
    int sample_rate() const { return sample_rate_; }
    
    /**
     * Get actual buffer size.
     */
    int buffer_size() const { return buffer_size_; }
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    AudioRenderCallback render_callback_;
    int sample_rate_;
    int buffer_size_;
    std::atomic<bool> running_{false};
};

} // namespace automix

#endif // AUTOMIX_AUDIO_OUTPUT_H
