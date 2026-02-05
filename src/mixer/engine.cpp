/**
 * AutoMix Engine - Main Engine Implementation
 */

#include "engine.h"
#include "../core/utils.h"
#include <filesystem>

namespace automix {

Engine::Engine(const std::string& db_path)
    : store_(std::make_unique<Store>(db_path))
    , decoder_(std::make_unique<Decoder>())
    , analyzer_(std::make_unique<Analyzer>())
    , playlist_generator_(std::make_unique<PlaylistGenerator>())
    , scheduler_(std::make_unique<Scheduler>()) {
    
    if (!store_->is_open()) {
        last_error_ = "Failed to open database: " + store_->error();
        return;
    }
    
    // Setup track loader for scheduler
    scheduler_->set_track_loader([this](int64_t track_id) {
        return this->load_track_audio(track_id);
    });
}

Engine::~Engine() = default;

bool Engine::is_valid() const {
    return store_ && store_->is_open();
}

int Engine::scan(const std::string& music_dir, bool recursive, ScanCallback callback) {
    if (!is_valid()) {
        last_error_ = "Engine not initialized";
        return -1;
    }
    
    std::filesystem::path dir_path(music_dir);
    if (!std::filesystem::exists(dir_path)) {
        last_error_ = "Directory does not exist: " + music_dir;
        return -1;
    }
    
    // Find all audio files
    auto files = utils::find_audio_files(dir_path, recursive);
    int total = static_cast<int>(files.size());
    int analyzed = 0;
    
    for (int i = 0; i < total; ++i) {
        const auto& file = files[i];
        std::string path_str = file.string();
        
        if (callback) {
            callback(path_str, i, total);
        }
        
        // Check if already analyzed and up-to-date
        int64_t file_mtime = utils::file_modified_time(file);
        if (!store_->needs_analysis(path_str, file_mtime)) {
            analyzed++;
            continue;
        }
        
        // Decode
        auto decode_result = decoder_->decode(path_str);
        if (decode_result.failed()) {
            continue;  // Skip files that can't be decoded
        }
        
        // Analyze
        auto analyze_result = analyzer_->analyze(decode_result.value());
        if (analyze_result.failed()) {
            continue;
        }
        
        // Store
        TrackInfo track;
        track.path = path_str;
        track.bpm = analyze_result.value().bpm;
        track.beats = analyze_result.value().beats;
        track.key = analyze_result.value().key;
        track.mfcc = analyze_result.value().mfcc;
        track.chroma = analyze_result.value().chroma;
        track.energy_curve = analyze_result.value().energy_curve;
        track.duration = analyze_result.value().duration;
        track.analyzed_at = utils::current_timestamp();
        track.file_modified_at = file_mtime;
        
        auto upsert_result = store_->upsert_track(track);
        if (upsert_result.ok()) {
            analyzed++;
        }
    }
    
    // Cleanup removed files
    store_->cleanup_missing_files();
    
    return analyzed;
}

int Engine::track_count() const {
    return store_ ? store_->get_track_count() : 0;
}

std::optional<TrackInfo> Engine::get_track(int64_t id) {
    return store_ ? store_->get_track(id) : std::nullopt;
}

std::vector<TrackInfo> Engine::search_tracks(const std::string& pattern) {
    return store_ ? store_->search_tracks(pattern) : std::vector<TrackInfo>{};
}

std::vector<TrackInfo> Engine::get_all_tracks() {
    return store_ ? store_->get_all_tracks() : std::vector<TrackInfo>{};
}

Playlist Engine::generate_playlist(
    int64_t seed_track_id,
    int count,
    const PlaylistRules& rules
) {
    auto seed_opt = store_->get_track(seed_track_id);
    if (!seed_opt) {
        last_error_ = "Seed track not found";
        return Playlist{};
    }
    
    auto candidates = store_->get_all_tracks();
    
    return playlist_generator_->generate(
        *seed_opt,
        candidates,
        count,
        rules,
        transition_config_
    );
}

Playlist Engine::create_playlist(const std::vector<int64_t>& track_ids) {
    std::vector<TrackInfo> tracks;
    tracks.reserve(track_ids.size());
    
    for (int64_t id : track_ids) {
        auto track_opt = store_->get_track(id);
        if (track_opt) {
            tracks.push_back(*track_opt);
        }
    }
    
    return playlist_generator_->create_with_transitions(tracks, transition_config_);
}

bool Engine::play(const Playlist& playlist) {
    if (!scheduler_->load_playlist(playlist)) {
        last_error_ = "Failed to load playlist";
        return false;
    }
    
    scheduler_->play();
    return true;
}

void Engine::pause() {
    scheduler_->pause();
}

void Engine::resume() {
    scheduler_->resume();
}

void Engine::stop() {
    scheduler_->stop();
}

void Engine::skip() {
    scheduler_->skip();
}

void Engine::seek(float position) {
    scheduler_->seek(position);
}

PlaybackState Engine::playback_state() const {
    return scheduler_->state();
}

float Engine::playback_position() const {
    return scheduler_->position();
}

int64_t Engine::current_track_id() const {
    return scheduler_->current_track_id();
}

void Engine::set_status_callback(StatusCallback callback) {
    scheduler_->set_status_callback(std::move(callback));
}

void Engine::set_transition_config(const TransitionConfig& config) {
    transition_config_ = config;
    scheduler_->set_transition_config(config);
}

int Engine::render(float* buffer, int frames) {
    return scheduler_->render(buffer, frames, sample_rate());
}

Result<AudioBuffer> Engine::load_track_audio(int64_t track_id) {
    auto track_opt = store_->get_track(track_id);
    if (!track_opt) {
        return "Track not found";
    }
    
    return decoder_->decode(track_opt->path);
}

} // namespace automix
