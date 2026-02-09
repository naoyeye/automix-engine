/**
 * AutoMix CLI - Playlist Generator
 * 
 * Generates a playlist starting from a seed track.
 * 
 * Usage: automix-playlist [options] --seed <track_id>
 */

#include "automix/automix.h"
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

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
              << "  -l, --list             List all tracks in library\n"
              << "  -h, --help             Show this help\n";
}

void list_tracks(AutoMixEngine* engine) {
    int count = automix_get_track_count(engine);
    std::cout << "Tracks in library: " << count << "\n\n";
    
    // Search for all tracks (using wildcard pattern)
    int64_t* ids = nullptr;
    int result_count = 0;
    
    if (automix_search_tracks(engine, "%", &ids, &result_count) == AUTOMIX_OK) {
        for (int i = 0; i < result_count; ++i) {
            AutoMixTrackInfo info;
            if (automix_get_track_info(engine, ids[i], &info) == AUTOMIX_OK) {
                std::cout << "  [" << info.id << "] " << info.path << "\n"
                          << "       BPM: " << info.bpm << ", Key: " << info.key 
                          << ", Duration: " << info.duration << "s\n";
                free((void*)info.path);
                free((void*)info.key);
            }
        }
        delete[] ids;
    }
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
    bool list_only = false;
    
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
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--random-seed") == 0) {
            if (i + 1 < argc) {
                random_seed = static_cast<uint32_t>(std::stoul(argv[++i]));
            }
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_only = true;
        }
    }
    
    // Create engine
    AutoMixEngine* engine = automix_create(db_path.c_str());
    if (!engine) {
        std::cerr << "Error: Failed to create engine. Database: " << db_path << "\n";
        return 1;
    }
    
    if (list_only) {
        list_tracks(engine);
        automix_destroy(engine);
        return 0;
    }
    
    if (seed_id < 0) {
        std::cerr << "Error: No seed track specified\n";
        print_usage(argv[0]);
        automix_destroy(engine);
        return 1;
    }
    
    // Generate playlist
    std::cout << "Generating playlist starting from track " << seed_id << "...\n\n";
    
    AutoMixPlaylistRules rules = {};
    rules.bpm_tolerance = 0.1f;  // 10% BPM tolerance
    rules.allow_key_change = 1;
    rules.max_key_distance = 2;  // Allow up to 2 steps on Camelot wheel
    rules.min_energy_match = 0.0f;
    rules.allow_cross_style = 1;
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
        std::cout << "Generated playlist with " << track_count << " tracks:\n\n";
        
        for (int i = 0; i < track_count; ++i) {
            AutoMixTrackInfo info;
            if (automix_get_track_info(engine, track_ids[i], &info) == AUTOMIX_OK) {
                std::cout << "  " << (i + 1) << ". [" << info.id << "] " << info.path << "\n"
                          << "       BPM: " << info.bpm << ", Key: " << info.key << "\n";
                free((void*)info.path);
                free((void*)info.key);
            }
        }
        
        delete[] track_ids;
    }
    
    automix_playlist_free(playlist);
    automix_destroy(engine);
    
    return 0;
}
