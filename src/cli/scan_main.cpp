/**
 * AutoMix CLI - Scan Tool
 * 
 * Scans a directory for music files and analyzes them.
 * 
 * Usage: automix-scan [options] <music_directory>
 */

#include "automix/automix.h"
#include <iostream>
#include <string>
#include <cstring>

void print_usage(const char* program) {
    std::string default_db = "automix.db";
#ifdef AUTOMIX_DEFAULT_DB_PATH
    default_db = AUTOMIX_DEFAULT_DB_PATH;
#endif

    std::cerr << "Usage: " << program << " [options] <music_directory>\n"
              << "\nOptions:\n"
              << "  -d, --database <path>  Database file path (default: " << default_db << ")\n"
              << "  -r, --recursive        Scan subdirectories (default: true)\n"
              << "  -n, --no-recursive     Don't scan subdirectories\n"
              << "  -h, --help             Show this help\n";
}

void scan_callback(const char* file, int processed, int total, void* user_data) {
    (void)user_data;
    std::cout << "\r[" << processed << "/" << total << "] " << file << std::flush;
    if (processed == total) {
        std::cout << std::endl;
    }
}

int main(int argc, char* argv[]) {
#ifdef AUTOMIX_DEFAULT_DB_PATH
    std::string db_path = AUTOMIX_DEFAULT_DB_PATH;
#else
    std::string db_path = "automix.db";
#endif
    std::string music_dir;
    bool recursive = true;
    
    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--database") == 0) {
            if (i + 1 < argc) {
                db_path = argv[++i];
            } else {
                std::cerr << "Error: -d requires a path argument\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recursive") == 0) {
            recursive = true;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-recursive") == 0) {
            recursive = false;
        } else if (argv[i][0] != '-') {
            music_dir = argv[i];
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (music_dir.empty()) {
        std::cerr << "Error: No music directory specified\n";
        print_usage(argv[0]);
        return 1;
    }
    
    // Create engine
    AutoMixEngine* engine = automix_create(db_path.c_str());
    if (!engine) {
        std::cerr << "Error: Failed to create engine\n";
        return 1;
    }
    
    std::cout << "Scanning " << music_dir << "...\n";
    
    // Scan
    int result = automix_scan_with_callback(
        engine, music_dir.c_str(), recursive ? 1 : 0, scan_callback, nullptr);
    
    if (result < 0) {
        std::cerr << "Error: " << automix_get_error(engine) << "\n";
        automix_destroy(engine);
        return 1;
    }
    
    int total = automix_get_track_count(engine);
    std::cout << "\nDone! " << result << " tracks analyzed.\n";
    std::cout << "Total tracks in library: " << total << "\n";
    
    automix_destroy(engine);
    return 0;
}
