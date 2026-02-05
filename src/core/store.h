/**
 * AutoMix Engine - Database Store
 */

#ifndef AUTOMIX_STORE_H
#define AUTOMIX_STORE_H

#include "automix/types.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace automix {

/**
 * SQLite-based storage for track features and metadata.
 */
class Store {
public:
    explicit Store(const std::string& db_path);
    ~Store();
    
    // Non-copyable
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    
    // Move constructible
    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;
    
    bool is_open() const { return db_ != nullptr; }
    const std::string& error() const { return last_error_; }
    
    /* ========================================================================
     * Track Operations
     * ======================================================================== */
    
    /**
     * Insert or update a track's features.
     */
    Result<int64_t> upsert_track(const TrackInfo& track);
    
    /**
     * Get track by ID.
     */
    std::optional<TrackInfo> get_track(int64_t id);
    
    /**
     * Get track by file path.
     */
    std::optional<TrackInfo> get_track_by_path(const std::string& path);
    
    /**
     * Get all tracks.
     */
    std::vector<TrackInfo> get_all_tracks();
    
    /**
     * Search tracks by path pattern (SQL LIKE).
     */
    std::vector<TrackInfo> search_tracks(const std::string& pattern);
    
    /**
     * Get track count.
     */
    int get_track_count();
    
    /**
     * Delete a track by ID.
     */
    bool delete_track(int64_t id);
    
    /**
     * Delete a track by path.
     */
    bool delete_track_by_path(const std::string& path);
    
    /* ========================================================================
     * Incremental Scan Support
     * ======================================================================== */
    
    /**
     * Check if a track needs re-analysis based on file modification time.
     */
    bool needs_analysis(const std::string& path, int64_t file_modified_at);
    
    /**
     * Get paths of all tracks in the database.
     */
    std::vector<std::string> get_all_paths();
    
    /**
     * Remove tracks whose files no longer exist.
     */
    int cleanup_missing_files();
    
private:
    void init_schema();
    
    // Serialization helpers for vector fields
    std::vector<uint8_t> serialize_floats(const std::vector<float>& data);
    std::vector<float> deserialize_floats(const void* data, int size);
    
    sqlite3* db_ = nullptr;
    std::string last_error_;
};

} // namespace automix

#endif // AUTOMIX_STORE_H
