/**
 * AutoMix Engine - C API Implementation
 */

#include "automix/automix.h"
#include "../mixer/engine.h"
#include <cstring>
#include <unordered_map>
#include <mutex>

using namespace automix;

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

struct AutoMixEngine {
    std::unique_ptr<Engine> engine;
    std::string last_error;
    TransitionConfig transition_config;
    AutoMixStatusCallback status_callback = nullptr;
    void* status_callback_user_data = nullptr;
};

struct PlaylistHandleImpl {
    Playlist playlist;
};

/* ============================================================================
 * Engine Lifecycle
 * ============================================================================ */

AutoMixEngine* automix_create(const char* db_path) {
    if (!db_path) return nullptr;
    
    auto handle = new AutoMixEngine();
    handle->engine = std::make_unique<Engine>(db_path);
    
    if (!handle->engine->is_valid()) {
        handle->last_error = handle->engine->error();
        delete handle;
        return nullptr;
    }
    
    // Setup status callback bridge
    handle->engine->set_status_callback([handle](
        PlaybackState state,
        int64_t current_track_id,
        float position,
        int64_t next_track_id
    ) {
        if (handle->status_callback) {
            handle->status_callback(
                static_cast<AutoMixPlaybackState>(state),
                current_track_id,
                position,
                next_track_id,
                handle->status_callback_user_data
            );
        }
    });
    
    return handle;
}

void automix_destroy(AutoMixEngine* engine) {
    delete engine;
}

const char* automix_get_error(AutoMixEngine* engine) {
    if (!engine) return "Invalid engine";
    return engine->last_error.c_str();
}

/* ============================================================================
 * Library Scanning
 * ============================================================================ */

int automix_scan(AutoMixEngine* engine, const char* music_dir, int recursive) {
    if (!engine || !engine->engine || !music_dir) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    
    int result = engine->engine->scan(music_dir, recursive != 0);
    if (result < 0) {
        engine->last_error = engine->engine->error();
    }
    return result;
}

int automix_scan_with_callback(
    AutoMixEngine* engine,
    const char* music_dir,
    int recursive,
    AutoMixScanCallback callback,
    void* user_data
) {
    if (!engine || !engine->engine || !music_dir) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    
    ScanCallback cpp_callback = nullptr;
    if (callback) {
        cpp_callback = [callback, user_data](const std::string& file, int processed, int total) {
            callback(file.c_str(), processed, total, user_data);
        };
    }
    
    int result = engine->engine->scan(music_dir, recursive != 0, cpp_callback);
    if (result < 0) {
        engine->last_error = engine->engine->error();
    }
    return result;
}

int automix_get_track_count(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return 0;
    return engine->engine->track_count();
}

AutoMixError automix_get_track_info(
    AutoMixEngine* engine,
    int64_t track_id,
    AutoMixTrackInfo* info
) {
    if (!engine || !engine->engine || !info) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    
    auto track = engine->engine->get_track(track_id);
    if (!track) {
        return AUTOMIX_ERROR_FILE_NOT_FOUND;
    }
    
    info->id = track->id;
    info->path = strdup(track->path.c_str());
    info->bpm = track->bpm;
    info->key = strdup(track->key.c_str());
    info->duration = track->duration;
    info->analyzed_at = track->analyzed_at;
    
    return AUTOMIX_OK;
}

AutoMixError automix_search_tracks(
    AutoMixEngine* engine,
    const char* pattern,
    int64_t** out_ids,
    int* out_count
) {
    if (!engine || !engine->engine || !pattern || !out_ids || !out_count) {
        return AUTOMIX_ERROR_INVALID_ARGUMENT;
    }
    
    auto tracks = engine->engine->search_tracks(pattern);
    
    *out_count = static_cast<int>(tracks.size());
    *out_ids = new int64_t[tracks.size()];
    
    for (size_t i = 0; i < tracks.size(); ++i) {
        (*out_ids)[i] = tracks[i].id;
    }
    
    return AUTOMIX_OK;
}

/* ============================================================================
 * Playlist Generation
 * ============================================================================ */

PlaylistHandle automix_generate_playlist(
    AutoMixEngine* engine,
    int64_t seed_track_id,
    int count,
    const AutoMixPlaylistRules* rules
) {
    if (!engine || !engine->engine) return nullptr;
    
    PlaylistRules cpp_rules;
    if (rules) {
        cpp_rules.bpm_tolerance = rules->bpm_tolerance;
        cpp_rules.allow_key_change = rules->allow_key_change != 0;
        cpp_rules.max_key_distance = rules->max_key_distance;
        cpp_rules.min_energy_match = rules->min_energy_match;
        cpp_rules.allow_cross_style = rules->allow_cross_style != 0;
    }
    
    auto playlist = engine->engine->generate_playlist(seed_track_id, count, cpp_rules);
    
    if (playlist.empty()) {
        engine->last_error = engine->engine->error();
        return nullptr;
    }
    
    auto handle = new PlaylistHandleImpl();
    handle->playlist = std::move(playlist);
    return handle;
}

PlaylistHandle automix_create_playlist(
    AutoMixEngine* engine,
    const int64_t* track_ids,
    int count
) {
    if (!engine || !engine->engine || !track_ids || count <= 0) return nullptr;
    
    std::vector<int64_t> ids(track_ids, track_ids + count);
    auto playlist = engine->engine->create_playlist(ids);
    
    if (playlist.empty()) {
        return nullptr;
    }
    
    auto handle = new PlaylistHandleImpl();
    handle->playlist = std::move(playlist);
    return handle;
}

AutoMixError automix_playlist_get_tracks(
    PlaylistHandle playlist,
    int64_t** out_ids,
    int* out_count
) {
    if (!playlist || !out_ids || !out_count) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    
    auto* impl = static_cast<PlaylistHandleImpl*>(playlist);
    
    *out_count = static_cast<int>(impl->playlist.size());
    *out_ids = new int64_t[impl->playlist.size()];
    
    for (size_t i = 0; i < impl->playlist.size(); ++i) {
        (*out_ids)[i] = impl->playlist.entries[i].track_id;
    }
    
    return AUTOMIX_OK;
}

void automix_playlist_free(PlaylistHandle playlist) {
    delete static_cast<PlaylistHandleImpl*>(playlist);
}

/* ============================================================================
 * Playback Control
 * ============================================================================ */

AutoMixError automix_play(AutoMixEngine* engine, PlaylistHandle playlist) {
    if (!engine || !engine->engine || !playlist) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    
    auto* impl = static_cast<PlaylistHandleImpl*>(playlist);
    
    if (!engine->engine->play(impl->playlist)) {
        engine->last_error = engine->engine->error();
        return AUTOMIX_ERROR_PLAYBACK_ERROR;
    }
    
    return AUTOMIX_OK;
}

AutoMixError automix_pause(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    engine->engine->pause();
    return AUTOMIX_OK;
}

AutoMixError automix_resume(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    engine->engine->resume();
    return AUTOMIX_OK;
}

AutoMixError automix_stop(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    engine->engine->stop();
    return AUTOMIX_OK;
}

AutoMixError automix_skip(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    engine->engine->skip();
    return AUTOMIX_OK;
}

AutoMixError automix_seek(AutoMixEngine* engine, float position_seconds) {
    if (!engine || !engine->engine) return AUTOMIX_ERROR_INVALID_ARGUMENT;
    engine->engine->seek(position_seconds);
    return AUTOMIX_OK;
}

AutoMixPlaybackState automix_get_state(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return AUTOMIX_STATE_STOPPED;
    return static_cast<AutoMixPlaybackState>(engine->engine->playback_state());
}

float automix_get_position(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return 0.0f;
    return engine->engine->playback_position();
}

int64_t automix_get_current_track(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return 0;
    return engine->engine->current_track_id();
}

void automix_set_status_callback(
    AutoMixEngine* engine,
    AutoMixStatusCallback callback,
    void* user_data
) {
    if (!engine) return;
    engine->status_callback = callback;
    engine->status_callback_user_data = user_data;
}

void automix_set_transition_config(
    AutoMixEngine* engine,
    const AutoMixTransitionConfig* config
) {
    if (!engine || !engine->engine || !config) return;
    
    TransitionConfig cpp_config;
    cpp_config.crossfade_beats = config->crossfade_beats;
    cpp_config.use_eq_swap = config->use_eq_swap != 0;
    cpp_config.stretch_limit = config->stretch_limit;
    
    engine->engine->set_transition_config(cpp_config);
    engine->transition_config = cpp_config;
}

/* ============================================================================
 * Audio Rendering
 * ============================================================================ */

int automix_render(AutoMixEngine* engine, float* buffer, int frames) {
    if (!engine || !engine->engine || !buffer) return 0;
    return engine->engine->render(buffer, frames);
}

int automix_get_sample_rate(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return 44100;
    return engine->engine->sample_rate();
}

int automix_get_channels(AutoMixEngine* engine) {
    if (!engine || !engine->engine) return 2;
    return engine->engine->channels();
}
