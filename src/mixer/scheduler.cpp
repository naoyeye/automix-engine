/**
 * AutoMix Engine - Playback Scheduler Implementation
 *
 * Thread model:
 *   render()  — real-time audio thread  (no alloc, no I/O, no callbacks)
 *   poll()    — control thread           (loading, callbacks, deck swaps)
 */

#include "scheduler.h"
#include "../core/utils.h"
#include <cstring>
#include <algorithm>

namespace automix {

Scheduler::Scheduler(int max_buffer_frames)
    : deck_a_(std::make_unique<Deck>())
    , deck_b_(std::make_unique<Deck>())
    , max_buffer_frames_(max_buffer_frames) {
    active_deck_ = deck_a_.get();
    next_deck_ = deck_b_.get();
    
    // Pre-allocate mix buffers (stereo)
    buffer_a_.resize(max_buffer_frames * 2, 0.0f);
    buffer_b_.resize(max_buffer_frames * 2, 0.0f);
}

Scheduler::~Scheduler() = default;

void Scheduler::set_track_loader(TrackLoadCallback loader) {
    track_loader_ = std::move(loader);
}

void Scheduler::set_status_callback(StatusCallback callback) {
    status_callback_ = std::move(callback);
}

void Scheduler::set_sample_rate(int sample_rate) {
    sample_rate_ = sample_rate > 0 ? sample_rate : 44100;
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
        state_ = transitioning_.load() ? PlaybackState::Transitioning : PlaybackState::Playing;
        notify_status();
    }
}

void Scheduler::stop() {
    active_deck_->pause();
    next_deck_->pause();
    active_deck_->unload();
    next_deck_->unload();
    
    transitioning_ = false;
    transition_finished_ = false;
    transition_trigger_pending_ = false;
    playback_finished_ = false;
    need_preload_next_ = false;
    need_status_notify_ = false;
    skip_requested_ = false;
    
    crossfader_.stop_automation();
    crossfader_.set_position(-1.0f);
    
    state_ = PlaybackState::Stopped;
    notify_status();
}

void Scheduler::skip() {
    if (current_index_ + 1 >= playlist_.size()) {
        stop();
        return;
    }
    
    // Signal the control thread to start a transition
    skip_requested_ = true;
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

void Scheduler::set_transition_config(const TransitionConfig& config) {
    transition_config_ = config;
}

// =============================================================================
// render() — AUDIO THREAD (real-time safe)
// =============================================================================

int Scheduler::render(float* output, int frames, int sample_rate) {
    if (state_ == PlaybackState::Stopped || state_ == PlaybackState::Paused) {
        std::memset(output, 0, frames * 2 * sizeof(float));
        return frames;
    }
    
    // Store sample rate for control thread
    sample_rate_ = sample_rate;
    
    // Clamp to pre-allocated buffer size
    frames = std::min(frames, max_buffer_frames_);
    
    // Update audio-thread state (only sets atomic flags, no I/O)
    rt_update(frames);
    
    // Get full mix parameters (volume + EQ)
    MixParams mix;
    crossfader_.get_mix_params(mix, frames);
    
    // Clear pre-allocated buffers (no allocation)
    std::memset(buffer_a_.data(), 0, frames * 2 * sizeof(float));
    std::memset(buffer_b_.data(), 0, frames * 2 * sizeof(float));
    
    int rendered_a = 0, rendered_b = 0;
    
    if (deck_a_->is_playing()) {
        float original_vol = deck_a_->volume();
        deck_a_->set_volume(mix.volume_a);
        deck_a_->set_eq(mix.eq_low_a, mix.eq_mid_a, mix.eq_high_a);
        rendered_a = deck_a_->render(buffer_a_.data(), frames);
        deck_a_->set_volume(original_vol);
    }
    
    if (deck_b_->is_playing()) {
        float original_vol = deck_b_->volume();
        deck_b_->set_volume(mix.volume_b);
        deck_b_->set_eq(mix.eq_low_b, mix.eq_mid_b, mix.eq_high_b);
        rendered_b = deck_b_->render(buffer_b_.data(), frames);
        deck_b_->set_volume(original_vol);
    }
    
    // Mix both decks into output
    for (int i = 0; i < frames * 2; ++i) {
        output[i] = buffer_a_[i] + buffer_b_[i];
        // Soft clip to prevent distortion
        output[i] = utils::clamp(output[i], -1.0f, 1.0f);
    }
    
    return std::max(rendered_a, rendered_b);
}

// Audio-thread state update — only reads state & sets atomic flags
void Scheduler::rt_update(int frames) {
    if (!active_deck_->is_loaded()) return;
    
    float current_pos = active_deck_->position();
    float duration = active_deck_->duration();
    
    // Check if we should signal a transition
    if (!transitioning_ && current_index_ < playlist_.size()) {
        const auto& entry = playlist_.entries[current_index_];
        
        float transition_point = duration - transition_config_.max_transition_seconds;
        
        // Use transition plan if available
        if (entry.transition_to_next) {
            transition_point = entry.transition_to_next->out_point.time_seconds;
        }
        
        if (current_pos >= transition_point && current_index_ + 1 < playlist_.size()) {
            // Signal control thread to start transition
            if (!transition_trigger_pending_) {
                transition_trigger_pending_ = true;
            }
        }
    }
    
    // Check if crossfader automation finished (transition complete)
    if (transitioning_ && !crossfader_.is_automating()) {
        transition_finished_ = true;
    }
    
    // Check if playback finished with no transition active
    if (active_deck_->is_finished() && !transitioning_) {
        playback_finished_ = true;
    }
}

// =============================================================================
// poll() — CONTROL THREAD (non-real-time)
// =============================================================================

void Scheduler::poll() {
    if (state_ == PlaybackState::Stopped) {
        return;
    }
    
    // Handle skip request
    if (skip_requested_.exchange(false)) {
        start_transition();
    }
    
    // Handle transition trigger from audio thread
    if (transition_trigger_pending_.exchange(false)) {
        if (!transitioning_) {
            start_transition();
        }
    }
    
    // Handle transition completion
    if (transition_finished_.exchange(false)) {
        // Swap decks
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
    
    // Handle playback finished (no transition was active)
    if (playback_finished_.exchange(false)) {
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

// =============================================================================
// Control-thread helpers
// =============================================================================

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
    
    // Setup next deck — apply BPM time-stretch
    next_deck_->set_stretch_ratio(stretch_ratio);
    next_deck_->seek(in_point);
    next_deck_->play();
    
    // Select crossfader curve based on transition config
    if (transition_config_.use_eq_swap) {
        crossfader_.set_curve(Crossfader::CurveType::EQSwap);
    } else {
        crossfader_.set_curve(Crossfader::CurveType::EqualPower);
    }
    
    // Use EQ hint from transition plan to override curve if specified
    if (entry.transition_to_next && entry.transition_to_next->eq_hint.use_eq_swap) {
        crossfader_.set_curve(Crossfader::CurveType::EQSwap);
    }
    
    // Start crossfade automation — use actual sample rate
    int crossfade_frames = static_cast<int>(crossfade_duration * sample_rate_);
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
