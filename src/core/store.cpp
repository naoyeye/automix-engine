/**
 * AutoMix Engine - Database Store Implementation
 */

#include "store.h"
#include "utils.h"
#include <cstring>
#include <filesystem>

namespace automix {

Store::Store(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    
    // Enable WAL mode for better concurrency
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    
    init_schema();
}

Store::~Store() {
    if (db_) {
        sqlite3_close(db_);
    }
}

Store::Store(Store&& other) noexcept
    : db_(other.db_), last_error_(std::move(other.last_error_)) {
    other.db_ = nullptr;
}

Store& Store::operator=(Store&& other) noexcept {
    if (this != &other) {
        if (db_) sqlite3_close(db_);
        db_ = other.db_;
        last_error_ = std::move(other.last_error_);
        other.db_ = nullptr;
    }
    return *this;
}

void Store::init_schema() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS tracks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            bpm REAL DEFAULT 0,
            beats BLOB,
            key TEXT,
            mfcc BLOB,
            chroma BLOB,
            energy_curve BLOB,
            duration REAL DEFAULT 0,
            analyzed_at INTEGER DEFAULT 0,
            file_modified_at INTEGER DEFAULT 0
        );
        
        CREATE INDEX IF NOT EXISTS idx_tracks_path ON tracks(path);
        CREATE INDEX IF NOT EXISTS idx_tracks_bpm ON tracks(bpm);
        CREATE INDEX IF NOT EXISTS idx_tracks_key ON tracks(key);
    )";
    
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        last_error_ = err_msg ? err_msg : "Failed to create schema";
        sqlite3_free(err_msg);
    }
}

std::vector<uint8_t> Store::serialize_floats(const std::vector<float>& data) {
    std::vector<uint8_t> result(data.size() * sizeof(float));
    std::memcpy(result.data(), data.data(), result.size());
    return result;
}

std::vector<float> Store::deserialize_floats(const void* data, int size) {
    if (!data || size <= 0) return {};
    
    size_t count = size / sizeof(float);
    std::vector<float> result(count);
    std::memcpy(result.data(), data, size);
    return result;
}

Result<int64_t> Store::upsert_track(const TrackInfo& track) {
    if (!db_) return "Database not open";
    
    const char* sql = R"(
        INSERT INTO tracks (path, bpm, beats, key, mfcc, chroma, energy_curve, duration, analyzed_at, file_modified_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            bpm = excluded.bpm,
            beats = excluded.beats,
            key = excluded.key,
            mfcc = excluded.mfcc,
            chroma = excluded.chroma,
            energy_curve = excluded.energy_curve,
            duration = excluded.duration,
            analyzed_at = excluded.analyzed_at,
            file_modified_at = excluded.file_modified_at
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::string("Prepare failed: ") + sqlite3_errmsg(db_);
    }
    
    // Serialize vector fields
    auto beats_data = serialize_floats(track.beats);
    auto mfcc_data = serialize_floats(track.mfcc);
    auto chroma_data = serialize_floats(track.chroma);
    auto energy_data = serialize_floats(track.energy_curve);
    
    sqlite3_bind_text(stmt, 1, track.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, track.bpm);
    sqlite3_bind_blob(stmt, 3, beats_data.data(), beats_data.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, track.key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 5, mfcc_data.data(), mfcc_data.size(), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 6, chroma_data.data(), chroma_data.size(), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 7, energy_data.data(), energy_data.size(), SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, track.duration);
    sqlite3_bind_int64(stmt, 9, track.analyzed_at);
    sqlite3_bind_int64(stmt, 10, track.file_modified_at);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        return std::string("Insert failed: ") + sqlite3_errmsg(db_);
    }
    
    // Get the ID (either new or existing)
    auto existing = get_track_by_path(track.path);
    return existing ? existing->id : sqlite3_last_insert_rowid(db_);
}

std::optional<TrackInfo> Store::get_track(int64_t id) {
    if (!db_) return std::nullopt;
    
    const char* sql = "SELECT * FROM tracks WHERE id = ?";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_int64(stmt, 1, id);
    
    std::optional<TrackInfo> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        TrackInfo track;
        track.id = sqlite3_column_int64(stmt, 0);
        track.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        track.bpm = static_cast<float>(sqlite3_column_double(stmt, 2));
        track.beats = deserialize_floats(sqlite3_column_blob(stmt, 3), sqlite3_column_bytes(stmt, 3));
        
        const char* key_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        track.key = key_text ? key_text : "";
        
        track.mfcc = deserialize_floats(sqlite3_column_blob(stmt, 5), sqlite3_column_bytes(stmt, 5));
        track.chroma = deserialize_floats(sqlite3_column_blob(stmt, 6), sqlite3_column_bytes(stmt, 6));
        track.energy_curve = deserialize_floats(sqlite3_column_blob(stmt, 7), sqlite3_column_bytes(stmt, 7));
        track.duration = static_cast<float>(sqlite3_column_double(stmt, 8));
        track.analyzed_at = sqlite3_column_int64(stmt, 9);
        track.file_modified_at = sqlite3_column_int64(stmt, 10);
        
        result = track;
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::optional<TrackInfo> Store::get_track_by_path(const std::string& path) {
    if (!db_) return std::nullopt;
    
    const char* sql = "SELECT * FROM tracks WHERE path = ?";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    
    std::optional<TrackInfo> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        TrackInfo track;
        track.id = sqlite3_column_int64(stmt, 0);
        track.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        track.bpm = static_cast<float>(sqlite3_column_double(stmt, 2));
        track.beats = deserialize_floats(sqlite3_column_blob(stmt, 3), sqlite3_column_bytes(stmt, 3));
        
        const char* key_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        track.key = key_text ? key_text : "";
        
        track.mfcc = deserialize_floats(sqlite3_column_blob(stmt, 5), sqlite3_column_bytes(stmt, 5));
        track.chroma = deserialize_floats(sqlite3_column_blob(stmt, 6), sqlite3_column_bytes(stmt, 6));
        track.energy_curve = deserialize_floats(sqlite3_column_blob(stmt, 7), sqlite3_column_bytes(stmt, 7));
        track.duration = static_cast<float>(sqlite3_column_double(stmt, 8));
        track.analyzed_at = sqlite3_column_int64(stmt, 9);
        track.file_modified_at = sqlite3_column_int64(stmt, 10);
        
        result = track;
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<TrackInfo> Store::get_all_tracks() {
    std::vector<TrackInfo> tracks;
    if (!db_) return tracks;
    
    const char* sql = "SELECT * FROM tracks ORDER BY id";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return tracks;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TrackInfo track;
        track.id = sqlite3_column_int64(stmt, 0);
        track.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        track.bpm = static_cast<float>(sqlite3_column_double(stmt, 2));
        track.beats = deserialize_floats(sqlite3_column_blob(stmt, 3), sqlite3_column_bytes(stmt, 3));
        
        const char* key_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        track.key = key_text ? key_text : "";
        
        track.mfcc = deserialize_floats(sqlite3_column_blob(stmt, 5), sqlite3_column_bytes(stmt, 5));
        track.chroma = deserialize_floats(sqlite3_column_blob(stmt, 6), sqlite3_column_bytes(stmt, 6));
        track.energy_curve = deserialize_floats(sqlite3_column_blob(stmt, 7), sqlite3_column_bytes(stmt, 7));
        track.duration = static_cast<float>(sqlite3_column_double(stmt, 8));
        track.analyzed_at = sqlite3_column_int64(stmt, 9);
        track.file_modified_at = sqlite3_column_int64(stmt, 10);
        
        tracks.push_back(track);
    }
    
    sqlite3_finalize(stmt);
    return tracks;
}

std::vector<TrackInfo> Store::search_tracks(const std::string& pattern) {
    std::vector<TrackInfo> tracks;
    if (!db_) return tracks;
    
    const char* sql = "SELECT * FROM tracks WHERE path LIKE ? ORDER BY id";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return tracks;
    }
    
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TrackInfo track;
        track.id = sqlite3_column_int64(stmt, 0);
        track.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        track.bpm = static_cast<float>(sqlite3_column_double(stmt, 2));
        track.beats = deserialize_floats(sqlite3_column_blob(stmt, 3), sqlite3_column_bytes(stmt, 3));
        
        const char* key_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        track.key = key_text ? key_text : "";
        
        track.mfcc = deserialize_floats(sqlite3_column_blob(stmt, 5), sqlite3_column_bytes(stmt, 5));
        track.chroma = deserialize_floats(sqlite3_column_blob(stmt, 6), sqlite3_column_bytes(stmt, 6));
        track.energy_curve = deserialize_floats(sqlite3_column_blob(stmt, 7), sqlite3_column_bytes(stmt, 7));
        track.duration = static_cast<float>(sqlite3_column_double(stmt, 8));
        track.analyzed_at = sqlite3_column_int64(stmt, 9);
        track.file_modified_at = sqlite3_column_int64(stmt, 10);
        
        tracks.push_back(track);
    }
    
    sqlite3_finalize(stmt);
    return tracks;
}

int Store::get_track_count() {
    if (!db_) return 0;
    
    const char* sql = "SELECT COUNT(*) FROM tracks";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

bool Store::delete_track(int64_t id) {
    if (!db_) return false;
    
    const char* sql = "DELETE FROM tracks WHERE id = ?";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int64(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Store::delete_track_by_path(const std::string& path) {
    if (!db_) return false;
    
    const char* sql = "DELETE FROM tracks WHERE path = ?";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Store::needs_analysis(const std::string& path, int64_t file_modified_at) {
    auto track = get_track_by_path(path);
    if (!track) return true;  // Not in database
    
    return track->file_modified_at < file_modified_at;
}

std::vector<std::string> Store::get_all_paths() {
    std::vector<std::string> paths;
    if (!db_) return paths;
    
    const char* sql = "SELECT path FROM tracks";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return paths;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (path) paths.push_back(path);
    }
    
    sqlite3_finalize(stmt);
    return paths;
}

int Store::cleanup_missing_files() {
    auto paths = get_all_paths();
    int removed = 0;
    
    for (const auto& path : paths) {
        if (!std::filesystem::exists(path)) {
            if (delete_track_by_path(path)) {
                removed++;
            }
        }
    }
    
    return removed;
}

} // namespace automix
