/**
 * AutoMix Engine - Decode Test Example
 * 
 * Demonstrates basic audio decoding functionality.
 */

#include "automix/automix.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_file>\n";
        return 1;
    }
    
    std::string audio_file = argv[1];
    
    std::cout << "AutoMix Engine - Decode Test\n";
    std::cout << "============================\n\n";
    
    // Create engine with temporary database
    AutoMixEngine* engine = automix_create(":memory:");
    if (!engine) {
        std::cerr << "Error: Failed to create engine\n";
        return 1;
    }
    
    std::cout << "Scanning file: " << audio_file << "\n";
    
    // Scan the single file (by scanning its parent directory with pattern matching)
    // For a single file test, we'll just use the scan function
    int result = automix_scan(engine, audio_file.c_str(), 0);
    
    if (result < 0) {
        std::cerr << "Error: " << automix_get_error(engine) << "\n";
        automix_destroy(engine);
        return 1;
    }
    
    std::cout << "Analyzed " << result << " file(s)\n\n";
    
    // Get track info
    int track_count = automix_get_track_count(engine);
    std::cout << "Tracks in database: " << track_count << "\n\n";
    
    // Search and print info
    int64_t* ids = nullptr;
    int count = 0;
    
    if (automix_search_tracks(engine, "%", &ids, &count) == AUTOMIX_OK) {
        for (int i = 0; i < count; ++i) {
            AutoMixTrackInfo info;
            if (automix_get_track_info(engine, ids[i], &info) == AUTOMIX_OK) {
                std::cout << "Track " << info.id << ":\n"
                          << "  Path:     " << info.path << "\n"
                          << "  BPM:      " << info.bpm << "\n"
                          << "  Key:      " << info.key << "\n"
                          << "  Duration: " << info.duration << " seconds\n";
                
                free((void*)info.path);
                free((void*)info.key);
            }
        }
        delete[] ids;
    }
    
    std::cout << "\nSample rate: " << automix_get_sample_rate(engine) << " Hz\n";
    std::cout << "Channels:    " << automix_get_channels(engine) << "\n";
    
    automix_destroy(engine);
    
    std::cout << "\nDone!\n";
    return 0;
}
