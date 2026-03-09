/**
 * AutoMix CLI - Scan Tool
 * 
 * Scans a directory for music files and analyzes them.
 * 
 * Usage: automix-scan [options] <music_directory>
 */

#include "automix/automix.h"
#include "db_path.h"
#include <iostream>
#include <string>
#include <cstring>

void print_usage(const char* program) {
    std::string default_hint = "AUTOMIX_DB or ~/Library/Application Support/Automix/automix.db";
#ifdef __linux__
    default_hint = "AUTOMIX_DB or ~/.local/share/automix/automix.db";
#endif
    std::cerr << "Usage: " << program << " [options] <music_directory>\n"
              << "\nOptions:\n"
              << "  -d, --database <path>  Database file path (default: " << default_hint << ")\n"
              << "  -r, --recursive        Scan subdirectories (default: true)\n"
              << "  -n, --no-recursive     Don't scan subdirectories\n"
              << "  -m, --metadata-only    Only collect path/duration, skip BPM/key analysis\n"
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
    std::string db_path_arg;  // From -d, empty if not specified
    std::string music_dir;
    bool recursive = true;
    int metadata_only = 0;
    
    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--database") == 0) {
            if (i + 1 < argc) {
                db_path_arg = argv[++i];
            } else {
                std::cerr << "Error: -d requires a path argument\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recursive") == 0) {
            recursive = true;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-recursive") == 0) {
            recursive = false;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--metadata-only") == 0) {
            metadata_only = 1;
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
    
    std::string db_path = automix::cli::resolve_db_path(db_path_arg);
    
    // Create engine
    AutoMixEngine* engine = automix_create(db_path.c_str());
    if (!engine) {
        std::cerr << "Error: Failed to create engine\n";
        return 1;
    }
    
    std::cout << "Scanning " << music_dir << (metadata_only ? " (metadata only)" : "") << "...\n";
    
    // Scan
    int result = automix_scan_with_callback_ex(
        engine, music_dir.c_str(), recursive ? 1 : 0, scan_callback, nullptr, metadata_only);
    
    if (result < 0) {
        std::cerr << "Error: " << automix_get_error(engine) << "\n";
        automix_destroy(engine);
        return 1;
    }
    
    int total = automix_get_track_count(engine);
    std::cout << "\nDone! " << result << " tracks " << (metadata_only ? "processed" : "analyzed") << ".\n";
    std::cout << "Total tracks in library: " << total << "\n";
    
    automix_destroy(engine);
    return 0;
}
