/**
 * AutoMix CLI - Default Database Path & Shared Playlist
 *
 * Resolves database path: -d flag > AUTOMIX_DB env > platform default.
 * Default: macOS ~/Library/Application Support/Automix/automix.db
 *          Linux ~/.local/share/automix/automix.db
 *
 * Shared playlist: CLI and Demo both read/write automix_playlist.txt
 * in the same directory as the database. Format: one track ID per line.
 */

#ifndef AUTOMIX_CLI_DB_PATH_H
#define AUTOMIX_CLI_DB_PATH_H

#include <string>
#include <vector>

namespace automix {
namespace cli {

/// Returns the platform-specific default database path.
/// Creates parent directory if it does not exist.
std::string get_default_db_path();

/// Returns the effective database path:
/// 1. explicit_path if non-empty (from -d)
/// 2. AUTOMIX_DB environment variable if set
/// 3. get_default_db_path()
std::string resolve_db_path(const std::string& explicit_path);

/// Returns the shared playlist file path for a given database path.
/// e.g. /path/to/automix.db -> /path/to/automix_playlist.txt
std::string get_playlist_path_for_db(const std::string& db_path);

/// Saves track IDs to the shared playlist file. Returns true on success.
bool save_playlist(const std::string& db_path, const int64_t* track_ids, int count);

/// Loads track IDs from the shared playlist file. Returns true if file exists and is valid.
bool load_playlist(const std::string& db_path, std::vector<int64_t>& track_ids);

}  // namespace cli
}  // namespace automix

#endif  // AUTOMIX_CLI_DB_PATH_H
