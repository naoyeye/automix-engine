/**
 * AutoMix Engine - Audio Decoder Implementation
 * 
 * Uses FFmpeg for decoding various audio formats.
 */

#include "decoder.h"
#include "../core/utils.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <cstring>

namespace automix {

class Decoder::Impl {
public:
    Impl() = default;
    ~Impl() = default;
    
    Result<AudioBuffer> decode(const std::string& path, int target_sample_rate) {
        AVFormatContext* format_ctx = nullptr;
        AVCodecContext* codec_ctx = nullptr;
        SwrContext* swr_ctx = nullptr;
        AVPacket* packet = nullptr;
        AVFrame* frame = nullptr;
        
        AudioBuffer buffer;
        buffer.sample_rate = target_sample_rate > 0 ? target_sample_rate : 44100;
        buffer.channels = 2;
        
        // Cleanup helper
        auto cleanup = [&]() {
            if (frame) av_frame_free(&frame);
            if (packet) av_packet_free(&packet);
            if (swr_ctx) swr_free(&swr_ctx);
            if (codec_ctx) avcodec_free_context(&codec_ctx);
            if (format_ctx) avformat_close_input(&format_ctx);
        };
        
        // Open file
        int ret = avformat_open_input(&format_ctx, path.c_str(), nullptr, nullptr);
        if (ret < 0) {
            cleanup();
            return "Failed to open file: " + path;
        }
        
        // Find stream info
        ret = avformat_find_stream_info(format_ctx, nullptr);
        if (ret < 0) {
            cleanup();
            return "Failed to find stream info";
        }
        
        // Find audio stream
        int audio_stream_idx = -1;
        for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
            if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audio_stream_idx = i;
                break;
            }
        }
        
        if (audio_stream_idx < 0) {
            cleanup();
            return "No audio stream found";
        }
        
        AVStream* audio_stream = format_ctx->streams[audio_stream_idx];
        AVCodecParameters* codecpar = audio_stream->codecpar;
        
        // Find decoder
        const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            cleanup();
            return "Unsupported codec";
        }
        
        // Allocate codec context
        codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx) {
            cleanup();
            return "Failed to allocate codec context";
        }
        
        ret = avcodec_parameters_to_context(codec_ctx, codecpar);
        if (ret < 0) {
            cleanup();
            return "Failed to copy codec parameters";
        }
        
        // Open codec
        ret = avcodec_open2(codec_ctx, codec, nullptr);
        if (ret < 0) {
            cleanup();
            return "Failed to open codec";
        }
        
        // Setup resampler
        AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        AVChannelLayout in_ch_layout;
        
        if (codec_ctx->ch_layout.nb_channels > 0) {
            av_channel_layout_copy(&in_ch_layout, &codec_ctx->ch_layout);
        } else {
            av_channel_layout_default(&in_ch_layout, codecpar->ch_layout.nb_channels > 0 ? 
                codecpar->ch_layout.nb_channels : 2);
        }
        
        int in_sample_rate = codec_ctx->sample_rate > 0 ? codec_ctx->sample_rate : 44100;
        
        ret = swr_alloc_set_opts2(&swr_ctx,
            &out_ch_layout,
            AV_SAMPLE_FMT_FLT,
            buffer.sample_rate,
            &in_ch_layout,
            codec_ctx->sample_fmt,
            in_sample_rate,
            0, nullptr);
        
        if (ret < 0 || !swr_ctx) {
            cleanup();
            return "Failed to create resampler";
        }
        
        ret = swr_init(swr_ctx);
        if (ret < 0) {
            cleanup();
            return "Failed to initialize resampler";
        }
        
        // Allocate packet and frame
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!packet || !frame) {
            cleanup();
            return "Failed to allocate packet/frame";
        }
        
        // Estimate output size and reserve
        int64_t duration_samples = 0;
        if (format_ctx->duration > 0) {
            duration_samples = av_rescale_q(format_ctx->duration, 
                AV_TIME_BASE_Q, {1, buffer.sample_rate});
            buffer.samples.reserve(duration_samples * buffer.channels);
        }
        
        // Decode loop
        while (av_read_frame(format_ctx, packet) >= 0) {
            if (packet->stream_index == audio_stream_idx) {
                ret = avcodec_send_packet(codec_ctx, packet);
                if (ret < 0) {
                    av_packet_unref(packet);
                    continue;
                }
                
                while (ret >= 0) {
                    ret = avcodec_receive_frame(codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        break;
                    }
                    
                    // Resample
                    int out_samples = av_rescale_rnd(
                        swr_get_delay(swr_ctx, in_sample_rate) + frame->nb_samples,
                        buffer.sample_rate, in_sample_rate, AV_ROUND_UP);
                    
                    std::vector<float> out_buffer(out_samples * buffer.channels);
                    uint8_t* out_ptr = reinterpret_cast<uint8_t*>(out_buffer.data());
                    
                    int converted = swr_convert(swr_ctx,
                        &out_ptr, out_samples,
                        (const uint8_t**)frame->extended_data, frame->nb_samples);
                    
                    if (converted > 0) {
                        buffer.samples.insert(buffer.samples.end(),
                            out_buffer.begin(),
                            out_buffer.begin() + converted * buffer.channels);
                    }
                    
                    av_frame_unref(frame);
                }
            }
            av_packet_unref(packet);
        }
        
        // Flush decoder
        avcodec_send_packet(codec_ctx, nullptr);
        while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
            int out_samples = av_rescale_rnd(
                swr_get_delay(swr_ctx, in_sample_rate) + frame->nb_samples,
                buffer.sample_rate, in_sample_rate, AV_ROUND_UP);
            
            std::vector<float> out_buffer(out_samples * buffer.channels);
            uint8_t* out_ptr = reinterpret_cast<uint8_t*>(out_buffer.data());
            
            int converted = swr_convert(swr_ctx,
                &out_ptr, out_samples,
                (const uint8_t**)frame->extended_data, frame->nb_samples);
            
            if (converted > 0) {
                buffer.samples.insert(buffer.samples.end(),
                    out_buffer.begin(),
                    out_buffer.begin() + converted * buffer.channels);
            }
            
            av_frame_unref(frame);
        }
        
        // Flush resampler
        int out_samples = swr_get_delay(swr_ctx, buffer.sample_rate);
        if (out_samples > 0) {
            std::vector<float> out_buffer(out_samples * buffer.channels);
            uint8_t* out_ptr = reinterpret_cast<uint8_t*>(out_buffer.data());
            
            int converted = swr_convert(swr_ctx, &out_ptr, out_samples, nullptr, 0);
            if (converted > 0) {
                buffer.samples.insert(buffer.samples.end(),
                    out_buffer.begin(),
                    out_buffer.begin() + converted * buffer.channels);
            }
        }
        
        cleanup();
        
        if (buffer.samples.empty()) {
            return "No audio data decoded";
        }
        
        return buffer;
    }
    
    float get_duration(const std::string& path) {
        AVFormatContext* format_ctx = nullptr;
        
        int ret = avformat_open_input(&format_ctx, path.c_str(), nullptr, nullptr);
        if (ret < 0) {
            return -1.0f;
        }
        
        ret = avformat_find_stream_info(format_ctx, nullptr);
        if (ret < 0) {
            avformat_close_input(&format_ctx);
            return -1.0f;
        }
        
        float duration = -1.0f;
        if (format_ctx->duration > 0) {
            duration = static_cast<float>(format_ctx->duration) / AV_TIME_BASE;
        }
        
        avformat_close_input(&format_ctx);
        return duration;
    }
};

Decoder::Decoder() : impl_(std::make_unique<Impl>()) {}
Decoder::~Decoder() = default;

Result<AudioBuffer> Decoder::decode(const std::string& path, int target_sample_rate) {
    return impl_->decode(path, target_sample_rate);
}

float Decoder::get_duration(const std::string& path) {
    return impl_->get_duration(path);
}

bool Decoder::is_supported(const std::string& path) {
    return utils::is_audio_file(path);
}

} // namespace automix
