/**
 * AutoMix Engine - Playback Scheduler Implementation
 */

#include "scheduler.h"
#include "../core/utils.h"
#include <cstring>

namespace automix {

Scheduler::Scheduler()
    : deck_a_(std::make_unique<Deck>())
    , deck_b_(std::make_unique<Deck>()) {
    active_deck_ = deck_a_.get();
    next_deck_ = deck_b_.get();
}

Scheduler::~Scheduler() = default;

void Scheduler::set_track_loader(TrackLoadCallback loader) {
    track_loader_ = std::move(loader);
}

void Scheduler::set_status_callback(StatusCallback callback) {
    status_callback_ = std::move(callback);
}

bool Scheduler::load_playlist(const Playlist& playlist) {
    stop();
    
    playlist_ = playlist;
    current_index_ = 0;
    
    if (playlist_.empty()) {
        return false;
    }
    
    // Load first track
    if (!load_track_to_deck(*active_deck_, playlist_.entries[0].track_id)) {
        return false;
    }
    
    // Pre-load next track if available
    if (playlist_.size() > 1) {
        load_track_to_deck(*next_deck_, playlist_.entries[1].track_id);
    }
    
    crossfader_.set_position(-1.0f);  // Full deck A
    
    return true;
}

void Scheduler::play() {
    if (playlist_.empty() || !active_deck_->is_loaded()) {
        return;
    }
    
    active_deck_->play();
    state_ = PlaybackState::Playing;
    notify_status();
}

void Scheduler::pause() {
    active_deck_->pause();
    next_deck_->pause();
    state_ = PlaybackState::Paused;
    notify_status();
}

void Scheduler::resume() {
    if (state_ == PlaybackState::Paused) {
        active_deck_->play();
        if (transitioning_) {
            next_deck_->play();
        }
        state_ = transitioning_ ? PlaybackState::Transitioning : PlaybackState::Playing;
        notify_status();
    }
}

void Scheduler::stop() {
    active_deck_->pause();
    next_deck_->pause();
    active_deck_->unload();
    next_deck_->unload();
    
    transitioning_ = false;
    crossfader_.stop_automation();
    crossfader_.set_position(-1.0f);
    
    state_ = PlaybackState::Stopped;
    notify_status();
}

void Scheduler::skip() {
    if (current_index_ + 1 >= playlist_.size()) {
        // No more tracks
        stop();
        return;
    }
    
    // Force immediate transition
    start_transition();
}

void Scheduler::seek(float position) {
    if (active_deck_->is_loaded()) {
        active_deck_->seek(position);
    }
}

float Scheduler::position() const {
    return active_deck_ ? active_deck_->position() : 0.0f;
}

int64_t Scheduler::current_track_id() const {
    return active_deck_ ? active_deck_->track_id() : 0;
}

int64_t Scheduler::next_track_id() const {
    if (current_index_ + 1 < playlist_.size()) {
        return playlist_.entries[current_index_ + 1].track_id;
    }
    return 0;
}

int Scheduler::render(float* output, int frames, int sample_rate) {
    if (state_ == PlaybackState::Stopped || state_ == PlaybackState::Paused) {
        std::memset(output, 0, frames * 2 * sizeof(float));
        return frames;
    }
    
    // Update scheduler state
    update(frames, sample_rate);
    
    // Get crossfader volumes
    float vol_a, vol_b;
    crossfader_.get_volumes(vol_a, vol_b, frames);
    
    // Render from both decks
    std::vector<float> buffer_a(frames * 2, 0.0f);
    std::vector<float> buffer_b(frames * 2, 0.0f);
    
    int rendered_a = 0, rendered_b = 0;
    
    if (deck_a_->is_playing()) {
        float original_vol = deck_a_->volume();
        deck_a_->set_volume(vol_a);
        rendered_a = deck_a_->render(buffer_a.data(), frames);
        deck_a_->set_volume(original_vol);
    }
    
    if (deck_b_->is_playing()) {
        float original_vol = deck_b_->volume();
        deck_b_->set_volume(vol_b);
        rendered_b = deck_b_->render(buffer_b.data(), frames);
        deck_b_->set_volume(original_vol);
    }
    
    // Mix both decks
    for (int i = 0; i < frames * 2; ++i) {
        output[i] = buffer_a[i] + buffer_b[i];
        // Soft clip to prevent distortion
        output[i] = utils::clamp(output[i], -1.0f, 1.0f);
    }
    
    return std::max(rendered_a, rendered_b);
}

void Scheduler::set_transition_config(const TransitionConfig& config) {
    transition_config_ = config;
}

void Scheduler::update(int frames, int sample_rate) {
    if (!active_deck_->is_loaded()) return;
    
    float current_pos = active_deck_->position();
    float duration = active_deck_->duration();
    
    // Check if we should start transition
    if (!transitioning_ && current_index_ < playlist_.size()) {
        const auto& entry = playlist_.entries[current_index_];
        
        float transition_point = duration - transition_config_.max_transition_seconds;
        
        // Use transition plan if available
        if (entry.transition_to_next) {
            transition_point = entry.transition_to_next->out_point.time_seconds;
        }
        
        if (current_pos >= transition_point && current_index_ + 1 < playlist_.size()) {
            start_transition();
        }
    }
    
    // Check if transition is complete
    if (transitioning_ && !crossfader_.is_automating()) {
        // Transition complete - swap decks
        std::swap(active_deck_, next_deck_);
        
        // Stop old deck
        next_deck_->pause();
        next_deck_->unload();
        
        current_index_++;
        transitioning_ = false;
        state_ = PlaybackState::Playing;
        
        // Pre-load next track
        if (current_index_ + 1 < playlist_.size()) {
            load_track_to_deck(*next_deck_, playlist_.entries[current_index_ + 1].track_id);
        }
        
        // Reset crossfader for next transition
        crossfader_.set_position(-1.0f);
        
        notify_status();
    }
    
    // Check if playback finished
    if (active_deck_->is_finished() && !transitioning_) {
        if (current_index_ + 1 < playlist_.size()) {
            // Move to next track
            current_index_++;
            std::swap(active_deck_, next_deck_);
            active_deck_->play();
            
            // Pre-load next
            if (current_index_ + 1 < playlist_.size()) {
                load_track_to_deck(*next_deck_, playlist_.entries[current_index_ + 1].track_id);
            }
            
            notify_status();
        } else {
            // Playlist finished
            stop();
        }
    }
}

bool Scheduler::load_track_to_deck(Deck& deck, int64_t track_id) {
    if (!track_loader_) {
        return false;
    }
    
    auto result = track_loader_(track_id);
    if (result.failed()) {
        return false;
    }
    
    return deck.load(result.value(), track_id);
}

void Scheduler::start_transition() {
    if (current_index_ + 1 >= playlist_.size()) {
        return;
    }
    
    const auto& entry = playlist_.entries[current_index_];
    
    // Ensure next track is loaded
    if (!next_deck_->is_loaded()) {
        if (!load_track_to_deck(*next_deck_, playlist_.entries[current_index_ + 1].track_id)) {
            return;
        }
    }
    
    // Get transition parameters
    float crossfade_duration = transition_config_.crossfade_beats * 60.0f / 120.0f;  // Default 120 BPM
    float stretch_ratio = 1.0f;
    float in_point = 0.0f;
    
    if (entry.transition_to_next) {
        crossfade_duration = entry.transition_to_next->crossfade_duration;
        stretch_ratio = entry.transition_to_next->bpm_stretch_ratio;
        in_point = entry.transition_to_next->in_point.time_seconds;
    }
    
    // Setup next deck
    next_deck_->set_stretch_ratio(stretch_ratio);
    next_deck_->seek(in_point);
    next_deck_->play();
    
    // Start crossfade automation
    int crossfade_frames = static_cast<int>(crossfade_duration * 44100);  // Assuming 44.1kHz
    crossfader_.start_automation(-1.0f, 1.0f, crossfade_frames);
    
    transitioning_ = true;
    state_ = PlaybackState::Transitioning;
    
    notify_status();
}

void Scheduler::notify_status() {
    if (status_callback_) {
        status_callback_(
            state_,
            current_track_id(),
            position(),
            next_track_id()
        );
    }
}

} // namespace automix
