/**
 * AutoMix CLI - Player
 * 
 * Plays a playlist with automatic transitions.
 * Uses PortAudio for audio output (if available).
 * 
 * Usage: automix-play [options] --seed <track_id>
 */

#include "automix/automix.h"
#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

// Simple audio output using Core Audio on macOS
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#endif

static std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

void status_callback(
    AutoMixPlaybackState state,
    int64_t current_track_id,
    float position,
    int64_t next_track_id,
    void* user_data
) {
    (void)user_data;
    
    const char* state_str = "Unknown";
    switch (state) {
        case AUTOMIX_STATE_STOPPED: state_str = "Stopped"; break;
        case AUTOMIX_STATE_PLAYING: state_str = "Playing"; break;
        case AUTOMIX_STATE_PAUSED: state_str = "Paused"; break;
        case AUTOMIX_STATE_TRANSITIONING: state_str = "Transitioning"; break;
    }
    
    std::cout << "\r[" << state_str << "] Track " << current_track_id 
              << " @ " << position << "s";
    if (next_track_id > 0) {
        std::cout << " -> Next: " << next_track_id;
    }
    std::cout << "          " << std::flush;
}

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [options] --seed <track_id>\n"
              << "\nOptions:\n"
              << "  -d, --database <path>  Database file path (default: automix.db)\n"
              << "  -s, --seed <id>        Seed track ID (required)\n"
              << "  -c, --count <n>        Number of tracks (default: 10)\n"
              << "  -h, --help             Show this help\n";
}

#ifdef __APPLE__
// Core Audio render callback
static OSStatus audio_callback(
    void* inRefCon,
    AudioUnitRenderActionFlags* ioActionFlags,
    const AudioTimeStamp* inTimeStamp,
    UInt32 inBusNumber,
    UInt32 inNumberFrames,
    AudioBufferList* ioData
) {
    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;
    
    AutoMixEngine* engine = static_cast<AutoMixEngine*>(inRefCon);
    
    for (UInt32 i = 0; i < ioData->mNumberBuffers; ++i) {
        float* buffer = static_cast<float*>(ioData->mBuffers[i].mData);
        int frames = ioData->mBuffers[i].mDataByteSize / (sizeof(float) * 2);
        
        automix_render(engine, buffer, frames);
    }
    
    return noErr;
}

bool setup_audio_output(AutoMixEngine* engine, AudioComponentInstance* audioUnit) {
    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (!component) {
        std::cerr << "Error: Could not find audio output component\n";
        return false;
    }
    
    if (AudioComponentInstanceNew(component, audioUnit) != noErr) {
        std::cerr << "Error: Could not create audio unit\n";
        return false;
    }
    
    // Set format
    AudioStreamBasicDescription format = {};
    format.mSampleRate = automix_get_sample_rate(engine);
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBitsPerChannel = 32;
    format.mChannelsPerFrame = 2;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = format.mBitsPerChannel / 8 * format.mChannelsPerFrame;
    format.mBytesPerPacket = format.mBytesPerFrame * format.mFramesPerPacket;
    
    if (AudioUnitSetProperty(*audioUnit, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, 0, &format, sizeof(format)) != noErr) {
        std::cerr << "Error: Could not set audio format\n";
        return false;
    }
    
    // Set callback
    AURenderCallbackStruct callback = {};
    callback.inputProc = audio_callback;
    callback.inputProcRefCon = engine;
    
    if (AudioUnitSetProperty(*audioUnit, kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr) {
        std::cerr << "Error: Could not set audio callback\n";
        return false;
    }
    
    if (AudioUnitInitialize(*audioUnit) != noErr) {
        std::cerr << "Error: Could not initialize audio unit\n";
        return false;
    }
    
    return true;
}
#endif

int main(int argc, char* argv[]) {
    std::string db_path = "automix.db";
    int64_t seed_id = -1;
    int count = 10;
    
    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--database") == 0) {
            if (i + 1 < argc) {
                db_path = argv[++i];
            }
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--seed") == 0) {
            if (i + 1 < argc) {
                seed_id = std::stoll(argv[++i]);
            }
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--count") == 0) {
            if (i + 1 < argc) {
                count = std::atoi(argv[++i]);
            }
        }
    }
    
    if (seed_id < 0) {
        std::cerr << "Error: No seed track specified\n";
        print_usage(argv[0]);
        return 1;
    }
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    
    // Create engine
    AutoMixEngine* engine = automix_create(db_path.c_str());
    if (!engine) {
        std::cerr << "Error: Failed to create engine\n";
        return 1;
    }
    
    // Set status callback
    automix_set_status_callback(engine, status_callback, nullptr);
    
    // Set transition config
    AutoMixTransitionConfig config = {};
    config.crossfade_beats = 16.0f;
    config.use_eq_swap = 0;
    config.stretch_limit = 0.06f;
    automix_set_transition_config(engine, &config);
    
    // Generate playlist
    std::cout << "Generating playlist...\n";
    
    AutoMixPlaylistRules rules = {};
    rules.bpm_tolerance = 0.1f;
    rules.allow_key_change = 1;
    rules.max_key_distance = 2;
    
    PlaylistHandle playlist = automix_generate_playlist(engine, seed_id, count, &rules);
    if (!playlist) {
        std::cerr << "Error: " << automix_get_error(engine) << "\n";
        automix_destroy(engine);
        return 1;
    }
    
    std::cout << "Playlist generated. Starting playback...\n";
    std::cout << "Press Ctrl+C to stop.\n\n";
    
#ifdef __APPLE__
    // Setup audio output
    AudioComponentInstance audioUnit = nullptr;
    if (!setup_audio_output(engine, &audioUnit)) {
        automix_playlist_free(playlist);
        automix_destroy(engine);
        return 1;
    }
    
    // Start playback
    if (automix_play(engine, playlist) != AUTOMIX_OK) {
        std::cerr << "Error: " << automix_get_error(engine) << "\n";
        AudioComponentInstanceDispose(audioUnit);
        automix_playlist_free(playlist);
        automix_destroy(engine);
        return 1;
    }
    
    // Start audio
    AudioOutputUnitStart(audioUnit);
    
    // Wait for stop signal
    while (g_running && automix_get_state(engine) != AUTOMIX_STATE_STOPPED) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Cleanup
    AudioOutputUnitStop(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
#else
    std::cerr << "Audio output not supported on this platform.\n";
    std::cerr << "Use automix_render() to get audio samples.\n";
#endif
    
    std::cout << "\nStopped.\n";
    
    automix_playlist_free(playlist);
    automix_destroy(engine);
    
    return 0;
}
