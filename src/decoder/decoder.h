/**
 * AutoMix Engine - Audio Decoder
 */

#ifndef AUTOMIX_DECODER_H
#define AUTOMIX_DECODER_H

#include "automix/types.h"
#include <string>

namespace automix {

/**
 * Audio decoder that converts various formats to uniform PCM.
 * Supported formats: MP3, FLAC, AAC, M4A, OGG, WAV, AIFF, DSD (DSF/DFF)
 * 
 * Output: float32, 44100Hz (or original sample rate), stereo
 */
class Decoder {
public:
    Decoder();
    ~Decoder();
    
    // Non-copyable
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    
    /**
     * Decode an audio file to PCM buffer.
     * 
     * @param path Path to audio file
     * @param target_sample_rate Target sample rate (0 = keep original)
     * @return AudioBuffer or error
     */
    Result<AudioBuffer> decode(const std::string& path, int target_sample_rate = 44100);
    
    /**
     * Decode an audio file for analysis purposes only.
     * Outputs mono 22050Hz to reduce data by 4x compared to full decode.
     * Use this for scanning/analysis; use decode() for playback.
     * 
     * @param path Path to audio file
     * @return AudioBuffer (mono, 22050Hz) or error
     */
    Result<AudioBuffer> decode_for_analysis(const std::string& path);
    
    /**
     * Get audio duration without full decode.
     * 
     * @param path Path to audio file
     * @return Duration in seconds, or negative on error
     */
    float get_duration(const std::string& path);
    
    /**
     * Check if a file format is supported.
     */
    static bool is_supported(const std::string& path);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace automix

#endif // AUTOMIX_DECODER_H
