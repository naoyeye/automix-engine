/**
 * AutoMix CLI - Transition Renderer
 * 
 * Renders only the transition segments between tracks to a WAV file.
 * This allows for quick verification of transition quality without
 * listening to entire tracks.
 */

#include "automix/automix.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>

// Simple WAV header structure
#pragma pack(push, 1)
struct WAVHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1; // PCM
    uint16_t num_channels = 2;
    uint32_t sample_rate = 44100;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size;
};
#pragma pack(pop)

void write_wav(const std::string& filename, const std::vector<float>& samples, int sample_rate) {
    WAVHeader header;
    header.sample_rate = sample_rate;
    header.byte_rate = sample_rate * 2 * 2;
    header.block_align = 4;
    header.data_size = samples.size() * 2;
    header.chunk_size = header.data_size + 36;

    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<char*>(&header), sizeof(header));

    for (float s : samples) {
        // Clamp and convert to int16
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t pcm = static_cast<int16_t>(s * 32767.0f);
        file.write(reinterpret_cast<char*>(&pcm), sizeof(pcm));
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <db_path> <track_id_1> <track_id_2> [output.wav]\n";
        return 1;
    }

    std::string db_path = argv[1];
    std::string output_file = (argc > 4) ? argv[4] : "transition_preview.wav";

    AutoMixEngine* engine = automix_create(db_path.c_str());
    if (!engine) {
        std::cerr << "Error: Failed to create engine\n";
        return 1;
    }

    // Auto-detect IDs if requested
    std::vector<int64_t> ids;
    int64_t* track_ids_ptr = nullptr;
    int count = 0;
    if (automix_search_tracks(engine, "%", &track_ids_ptr, &count) == AUTOMIX_OK) {
        for (int i = 0; i < count; ++i) {
            ids.push_back(track_ids_ptr[i]);
        }
        delete[] track_ids_ptr;
    }
    
    // Verify we have IDs (simple validation)
    if (ids.size() < 2) {
        std::cerr << "Error: Not enough tracks in database. Need 2, found " << ids.size() << "\n";
        // List what we found
        for (auto id : ids) std::cerr << "Found ID: " << id << "\n";
        automix_destroy(engine);
        return 1;
    }

    int64_t track_ids[] = {ids[0], ids[1]};
    // If user provided specific IDs, use them instead
    if (argc >= 4 && std::string(argv[2]) != "auto") {
        try {
            track_ids[0] = std::stoll(argv[2]);
            track_ids[1] = std::stoll(argv[3]);
        } catch (...) {
            std::cerr << "Error: Invalid track IDs provided\n";
            return 1;
        }
    }
    
    std::cout << "Using Track IDs: " << track_ids[0] << " and " << track_ids[1] << "\n";

    PlaylistHandle playlist = automix_create_playlist(engine, track_ids, 2);
    if (!playlist) {
        std::cerr << "Error: Failed to create playlist: " << automix_get_error(engine) << "\n";
        automix_destroy(engine);
        return 1;
    }

    // Set a default transition config
    AutoMixTransitionConfig config = {};
    config.crossfade_beats = 16.0f;
    config.use_eq_swap = 0;
    config.stretch_limit = 0.06f;
    config.stretch_recovery_seconds = 6.0f;
    automix_set_transition_config(engine, &config);

    if (automix_play(engine, playlist) != AUTOMIX_OK) {
        std::cerr << "Error: Failed to start playback: " << automix_get_error(engine) << "\n";
        automix_playlist_free(playlist);
        automix_destroy(engine);
        return 1;
    }

    int sample_rate = automix_get_sample_rate(engine);
    
    // We want to capture:
    // 1. 10 seconds before transition starts
    // 2. The transition itself (usually 16-32 beats, ~10-20s)
    // 3. 10 seconds after transition ends
    
    std::cout << "Rendering transition between Track " << track_ids[0] << " and Track " << track_ids[1] << "...\n";
    
    // Seek to near the end of the first track to trigger transition quickly
    // We need to know where the transition starts. 
    // For simplicity in this tool, we'll just render in chunks and monitor state.
    
    // First, let's find the out_point for track 1
    // Since we don't have direct access to TransitionPlan in C API easily,
    // we'll "fast-forward" by rendering and discarding until we're close.
    
    // But wait, the engine won't load the next track unless we poll.
    
    std::vector<float> captured_audio;
    const int chunk_size = 4096;
    std::vector<float> buffer(chunk_size * 2);
    
    bool transition_started = false;
    bool transition_finished = false;
    int frames_after_transition = 0;
    const int max_frames_after = 10 * sample_rate; // 10 seconds
    
    // We'll seek to 20 seconds before the end of track 1 to save time
    AutoMixTrackInfo info1;
    if (automix_get_track_info(engine, track_ids[0], &info1) == AUTOMIX_OK) {
        float seek_pos = info1.duration - 30.0f;
        if (seek_pos < 0) seek_pos = 0;
        automix_seek(engine, seek_pos);
        free((void*)info1.path);
        free((void*)info1.key);
    }

    int total_rendered = 0;
    const int timeout_frames = 120 * sample_rate; // 2 minutes max safety

    while (total_rendered < timeout_frames) {
        automix_poll(engine);
        int rendered = automix_render(engine, buffer.data(), chunk_size);
        if (rendered <= 0) break;

        AutoMixPlaybackState state = automix_get_state(engine);
        
        if (state == AUTOMIX_STATE_TRANSITIONING) {
            if (!transition_started) {
                std::cout << "Transition started...\n";
                transition_started = true;
            }
        } else if (transition_started && state == AUTOMIX_STATE_PLAYING) {
            if (!transition_finished) {
                std::cout << "Transition finished.\n";
                transition_finished = true;
            }
        }

        // Capture audio if we are in or near transition
        // For this tool, we just capture everything from our seek point until 10s after transition
        captured_audio.insert(captured_audio.end(), buffer.begin(), buffer.begin() + rendered * 2);
        
        if (transition_finished) {
            frames_after_transition += rendered;
            if (frames_after_transition >= max_frames_after) break;
        }
        
        total_rendered += rendered;
    }

    if (captured_audio.empty()) {
        std::cerr << "Error: No audio captured.\n";
    } else {
        std::cout << "Writing " << captured_audio.size() / (2.0 * sample_rate) << " seconds to " << output_file << "\n";
        write_wav(output_file, captured_audio, sample_rate);
        std::cout << "Done! You can now open " << output_file << " to check the transition.\n";
    }

    automix_playlist_free(playlist);
    automix_destroy(engine);
    return 0;
}
