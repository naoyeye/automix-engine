/**
 * AutoMix CLI - Default Database Path Implementation
 */

#include "db_path.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#if defined(__APPLE__) || defined(__MACH__)
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#elif defined(__linux__) || defined(__unix__)
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#else
#include <cstdlib>
#endif

namespace automix {
namespace cli {

static std::string get_home_dir() {
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home);
    }
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return std::string(pw->pw_dir);
    }
#endif
    return ".";
}

static bool ensure_dir_exists(const std::string& path) {
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    // Create directory (mkdir -p style: parent may not exist)
    size_t pos = 0;
    std::string p = path;
    if (p[0] == '/') pos = 1;
    while ((pos = p.find('/', pos)) != std::string::npos) {
        std::string sub = p.substr(0, pos);
        if (!sub.empty() && mkdir(sub.c_str(), 0755) != 0) {
            struct stat st2;
            if (stat(sub.c_str(), &st2) != 0 || !S_ISDIR(st2.st_mode)) {
                return false;
            }
        }
        ++pos;
    }
    return mkdir(p.c_str(), 0755) == 0 || (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
#else
    (void)path;
    return true;
#endif
}

std::string get_default_db_path() {
    std::string home = get_home_dir();
    std::string path;

#if defined(__APPLE__) || defined(__MACH__)
    path = home + "/Library/Application Support/Automix/automix.db";
#elif defined(__linux__) || defined(__unix__)
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] != '\0') {
        path = std::string(xdg) + "/automix/automix.db";
    } else {
        path = home + "/.local/share/automix/automix.db";
    }
#else
    path = home + "/automix.db";
#endif

    // Ensure parent directory exists
    size_t last_slash = path.rfind('/');
    if (last_slash != std::string::npos && last_slash > 0) {
        ensure_dir_exists(path.substr(0, last_slash));
    }

    return path;
}

std::string resolve_db_path(const std::string& explicit_path) {
    if (!explicit_path.empty()) {
        return explicit_path;
    }
    const char* env = std::getenv("AUTOMIX_DB");
    if (env && env[0] != '\0') {
        return std::string(env);
    }
    return get_default_db_path();
}

std::string get_playlist_path_for_db(const std::string& db_path) {
    size_t last_slash = db_path.rfind('/');
    std::string dir = (last_slash != std::string::npos && last_slash > 0)
                          ? db_path.substr(0, last_slash)
                          : ".";
    return dir + "/automix_playlist.txt";
}

bool save_playlist(const std::string& db_path, const int64_t* track_ids, int count) {
    if (!track_ids || count <= 0) return false;
    std::string path = get_playlist_path_for_db(db_path);
    std::ofstream out(path);
    if (!out) return false;
    for (int i = 0; i < count; ++i) {
        out << track_ids[i] << "\n";
    }
    return out.good();
}

bool load_playlist(const std::string& db_path, std::vector<int64_t>& track_ids) {
    track_ids.clear();
    std::string path = get_playlist_path_for_db(db_path);
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            track_ids.push_back(std::stoll(line));
        } catch (...) {
            continue;
        }
    }
    return !track_ids.empty();
}

}  // namespace cli
}  // namespace automix
