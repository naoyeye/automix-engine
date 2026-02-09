/**
 * AutoMix CLI - Player
 * 
 * Plays a playlist with automatic transitions using the built-in
 * AudioOutput (CoreAudio on macOS). The main loop calls automix_poll()
 * to drive track pre-loading, transition triggers, and status callbacks.
 * 
 * Usage: automix-play [options] --seed <track_id>
 */

#include "automix/automix.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

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
        case AUTOMIX_STATE_STOPPED:       state_str = "Stopped"; break;
        case AUTOMIX_STATE_PLAYING:       state_str = "Playing"; break;
        case AUTOMIX_STATE_PAUSED:        state_str = "Paused"; break;
        case AUTOMIX_STATE_TRANSITIONING: state_str = "Mixing "; break;
    }
    
    std::cout << "\r  [" << state_str << "] Track " << current_track_id
              << "  " << std::fixed << std::setprecision(1) << position << "s";
    if (next_track_id > 0) {
        std::cout << "  -> Next: " << next_track_id;
    }
    std::cout << "          " << std::flush;
}

void print_usage(const char* program) {
    std::string default_db = "automix.db";
#ifdef AUTOMIX_DEFAULT_DB_PATH
    default_db = AUTOMIX_DEFAULT_DB_PATH;
#endif

    std::cerr << "Usage: " << program << " [options] --seed <track_id>\n"
              << "\nOptions:\n"
              << "  -d, --database <path>  Database file path (default: " << default_db << ")\n"
              << "  -s, --seed <id>        Seed track ID (required)\n"
              << "  -c, --count <n>        Number of tracks (default: 10)\n"
              << "  -r, --random-seed <n>  Random seed for reproducible playlists (0 = random)\n"
              << "  -e, --eq-swap          Use EQ swap transitions\n"
              << "  -b, --beats <n>        Crossfade beats (default: 16)\n"
              << "  -h, --help             Show this help\n";
}

int main(int argc, char* argv[]) {
#ifdef AUTOMIX_DEFAULT_DB_PATH
    std::string db_path = AUTOMIX_DEFAULT_DB_PATH;
#else
    std::string db_path = "automix.db";
#endif
    int64_t seed_id = -1;
    int count = 10;
    uint32_t random_seed = 0;
    bool eq_swap = false;
    float crossfade_beats = 16.0f;
    
    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--database") == 0) {
            if (i + 1 < argc) db_path = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--seed") == 0) {
            if (i + 1 < argc) seed_id = std::stoll(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--count") == 0) {
            if (i + 1 < argc) count = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--random-seed") == 0) {
            if (i + 1 < argc) random_seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--eq-swap") == 0) {
            eq_swap = true;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--beats") == 0) {
            if (i + 1 < argc) crossfade_beats = std::atof(argv[++i]);
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
    config.crossfade_beats = crossfade_beats;
    config.use_eq_swap = eq_swap ? 1 : 0;
    config.stretch_limit = 0.06f;
    automix_set_transition_config(engine, &config);
    
    // Generate playlist
    std::cout << "Generating playlist from seed track " << seed_id << "...\n";
    
    AutoMixPlaylistRules rules = {};
    rules.bpm_tolerance = 0.1f;
    rules.allow_key_change = 1;
    rules.max_key_distance = 2;
    rules.random_seed = random_seed;
    
    PlaylistHandle playlist = automix_generate_playlist(engine, seed_id, count, &rules);
    if (!playlist) {
        std::cerr << "Error: " << automix_get_error(engine) << "\n";
        automix_destroy(engine);
        return 1;
    }
    
    // Print playlist
    int64_t* track_ids = nullptr;
    int track_count = 0;
    if (automix_playlist_get_tracks(playlist, &track_ids, &track_count) == AUTOMIX_OK) {
        std::cout << "\nPlaylist (" << track_count << " tracks):\n";
        for (int i = 0; i < track_count; ++i) {
            AutoMixTrackInfo info;
            if (automix_get_track_info(engine, track_ids[i], &info) == AUTOMIX_OK) {
                std::cout << "  " << (i + 1) << ". [" << info.id << "] " << info.path
                          << "  (BPM:" << info.bpm << " Key:" << info.key << ")\n";
                free((void*)info.path);
                free((void*)info.key);
            }
        }
        delete[] track_ids;
    }
    
    std::cout << "\nStarting playback" << (eq_swap ? " (EQ Swap)" : "") << "...\n";
    std::cout << "Press Ctrl+C to stop.\n\n";
    
    // Start playback — this loads the playlist into the scheduler
    if (automix_play(engine, playlist) != AUTOMIX_OK) {
        std::cerr << "Error: " << automix_get_error(engine) << "\n";
        automix_playlist_free(playlist);
        automix_destroy(engine);
        return 1;
    }
    
    // Start platform audio output (CoreAudio on macOS)
    if (automix_start_audio(engine) != AUTOMIX_OK) {
        std::cerr << "Warning: Could not start audio output.\n"
                  << "Use automix_render() to pull audio manually.\n";
    }
    
    // =========================================================================
    // Main loop: poll() drives the non-real-time scheduler work
    //   - pre-loads upcoming tracks
    //   - completes deck swaps after transitions
    //   - fires status callbacks
    // =========================================================================
    while (g_running && automix_get_state(engine) != AUTOMIX_STATE_STOPPED) {
        automix_poll(engine);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Cleanup
    automix_stop(engine);
    automix_stop_audio(engine);
    
    std::cout << "\nStopped.\n";
    
    automix_playlist_free(playlist);
    automix_destroy(engine);
    
    return 0;
}
