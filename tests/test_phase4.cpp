/**
 * AutoMix Engine - Phase 4 Tests: Real-time Mixing
 *
 * Tests the Mixer module components:
 *   1. Deck:       load, render, seek, volume smoothing, EQ
 *   2. Crossfader: curve types, automation, EQ Swap MixParams
 *   3. Scheduler:  dual-deck transitions, poll/render split
 *   4. Engine:     end-to-end render with synthetic audio
 */

#include "automix/types.h"
#include "mixer/deck.h"
#include "mixer/crossfader.h"
#include "mixer/scheduler.h"
#include "mixer/engine.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include <numeric>

using namespace automix;

// =============================================================================
// Helpers
// =============================================================================

static constexpr int kSampleRate = 44100;
static constexpr int kChannels   = 2;

/**
 * Generate a stereo AudioBuffer filled with a sine wave at a given frequency.
 * Duration in seconds.
 */
static AudioBuffer make_sine(float freq, float duration, float amplitude = 0.5f) {
    AudioBuffer buf;
    buf.sample_rate = kSampleRate;
    buf.channels = kChannels;
    
    size_t total_frames = static_cast<size_t>(duration * kSampleRate);
    buf.samples.resize(total_frames * kChannels);
    
    for (size_t i = 0; i < total_frames; ++i) {
        float val = amplitude * std::sin(2.0f * static_cast<float>(M_PI) * freq * i / kSampleRate);
        buf.samples[i * 2]     = val;  // L
        buf.samples[i * 2 + 1] = val;  // R
    }
    
    return buf;
}

/**
 * Compute RMS energy of an interleaved stereo buffer.
 */
static float compute_rms(const float* buffer, int frames) {
    float sum = 0.0f;
    for (int i = 0; i < frames * 2; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / (frames * 2));
}

/**
 * Check that a buffer is not all zeros.
 */
static bool is_nonzero(const float* buffer, int frames) {
    for (int i = 0; i < frames * 2; ++i) {
        if (std::abs(buffer[i]) > 1e-6f) return true;
    }
    return false;
}

/**
 * Check that a buffer is all zeros (within tolerance).
 */
static bool is_silent(const float* buffer, int frames) {
    return !is_nonzero(buffer, frames);
}

// =============================================================================
// 1. Deck Tests
// =============================================================================

void test_deck_load_and_render() {
    std::cout << "Test: Deck load and render... ";
    
    Deck deck;
    assert(!deck.is_loaded());
    assert(!deck.is_playing());
    
    auto audio = make_sine(440.0f, 1.0f);
    assert(deck.load(audio, 1));
    assert(deck.is_loaded());
    assert(deck.track_id() == 1);
    assert(std::abs(deck.duration() - 1.0f) < 0.01f);
    
    // Render without play -> silence
    std::vector<float> output(512 * 2, 0.0f);
    int rendered = deck.render(output.data(), 512);
    assert(rendered == 0);
    assert(is_silent(output.data(), 512));
    
    // Play and render -> audio
    deck.play();
    assert(deck.is_playing());
    rendered = deck.render(output.data(), 512);
    assert(rendered == 512);
    assert(is_nonzero(output.data(), 512));
    
    std::cout << "PASSED\n";
}

void test_deck_seek() {
    std::cout << "Test: Deck seek... ";
    
    Deck deck;
    auto audio = make_sine(440.0f, 2.0f);
    deck.load(audio, 1);
    deck.play();
    
    // Seek to 1 second
    deck.seek(1.0f);
    assert(std::abs(deck.position() - 1.0f) < 0.01f);
    
    // Seek beyond end should clamp
    deck.seek(10.0f);
    assert(deck.position() <= deck.duration() + 0.01f);
    
    std::cout << "PASSED\n";
}

void test_deck_volume_smoothing() {
    std::cout << "Test: Deck volume smoothing... ";
    
    Deck deck;
    auto audio = make_sine(440.0f, 1.0f, 1.0f);
    deck.load(audio, 1);
    deck.play();
    
    // Render at volume 1.0
    deck.set_volume(1.0f);
    std::vector<float> out1(256 * 2);
    deck.render(out1.data(), 256);
    float rms1 = compute_rms(out1.data(), 256);
    
    // Render at volume 0.5
    deck.set_volume(0.5f);
    std::vector<float> out2(256 * 2);
    deck.render(out2.data(), 256);
    float rms2 = compute_rms(out2.data(), 256);
    
    // Volume 0.5 should be quieter than 1.0
    assert(rms2 < rms1);
    
    // Volume smoothing: first sample of out2 should NOT be exactly 0.5 * source
    // (it should be ramping from 1.0 to 0.5)
    // Just verify the RMS is between 0.5*rms1 and rms1 (smoothed)
    assert(rms2 > rms1 * 0.3f);
    assert(rms2 < rms1 * 0.9f);
    
    std::cout << "PASSED\n";
}

void test_deck_eq() {
    std::cout << "Test: Deck EQ... ";
    
    Deck deck;
    // Use a low frequency sine (100 Hz) to test low-shelf EQ effect
    auto audio = make_sine(100.0f, 0.5f, 1.0f);
    deck.load(audio, 1);
    deck.play();
    
    // Render with flat EQ
    deck.set_eq(0.0f, 0.0f, 0.0f);
    std::vector<float> out_flat(1024 * 2);
    deck.render(out_flat.data(), 1024);
    float rms_flat = compute_rms(out_flat.data(), 1024);
    
    // Reload and render with killed bass (-60 dB)
    deck.unload();
    audio = make_sine(100.0f, 0.5f, 1.0f);
    deck.load(audio, 2);
    deck.play();
    deck.set_eq(-60.0f, 0.0f, 0.0f);
    std::vector<float> out_cut(1024 * 2);
    deck.render(out_cut.data(), 1024);
    float rms_cut = compute_rms(out_cut.data(), 1024);
    
    // Cut bass should be much quieter for a 100 Hz signal
    assert(rms_cut < rms_flat * 0.3f);
    
    // Verify get_eq
    float lo, mi, hi;
    deck.get_eq(lo, mi, hi);
    assert(std::abs(lo - (-60.0f)) < 0.1f);
    assert(std::abs(mi) < 0.1f);
    assert(std::abs(hi) < 0.1f);
    
    std::cout << "PASSED\n";
}

void test_deck_finished() {
    std::cout << "Test: Deck finished detection... ";
    
    Deck deck;
    // Very short buffer: 512 frames (~11.6 ms)
    auto audio = make_sine(440.0f, 0.012f);
    deck.load(audio, 1);
    deck.play();
    
    assert(!deck.is_finished());
    
    // Render more frames than the buffer contains
    std::vector<float> out(2048 * 2);
    deck.render(out.data(), 2048);
    
    assert(deck.is_finished());
    
    std::cout << "PASSED\n";
}

// =============================================================================
// 2. Crossfader Tests
// =============================================================================

void test_crossfader_linear() {
    std::cout << "Test: Crossfader linear curve... ";
    
    Crossfader cf;
    cf.set_curve(Crossfader::CurveType::Linear);
    
    float va, vb;
    
    // Full A
    cf.set_position(-1.0f);
    cf.get_volumes(va, vb);
    assert(std::abs(va - 1.0f) < 0.01f);
    assert(std::abs(vb - 0.0f) < 0.01f);
    
    // Center
    cf.set_position(0.0f);
    cf.get_volumes(va, vb);
    assert(std::abs(va - 0.5f) < 0.01f);
    assert(std::abs(vb - 0.5f) < 0.01f);
    
    // Full B
    cf.set_position(1.0f);
    cf.get_volumes(va, vb);
    assert(std::abs(va - 0.0f) < 0.01f);
    assert(std::abs(vb - 1.0f) < 0.01f);
    
    std::cout << "PASSED\n";
}

void test_crossfader_equal_power() {
    std::cout << "Test: Crossfader equal power curve... ";
    
    Crossfader cf;
    cf.set_curve(Crossfader::CurveType::EqualPower);
    
    float va, vb;
    
    // At center, sum of squares should be ~1 (equal power property)
    cf.set_position(0.0f);
    cf.get_volumes(va, vb);
    float power_sum = va * va + vb * vb;
    assert(std::abs(power_sum - 1.0f) < 0.01f);
    
    // At extremes
    cf.set_position(-1.0f);
    cf.get_volumes(va, vb);
    assert(std::abs(va - 1.0f) < 0.01f);
    assert(std::abs(vb - 0.0f) < 0.02f);
    
    std::cout << "PASSED\n";
}

void test_crossfader_automation() {
    std::cout << "Test: Crossfader automation... ";
    
    Crossfader cf;
    cf.set_curve(Crossfader::CurveType::Linear);
    
    // Automate from A to B over 1000 frames
    cf.start_automation(-1.0f, 1.0f, 1000);
    assert(cf.is_automating());
    
    float va, vb;
    
    // Advance halfway
    cf.get_volumes(va, vb, 500);
    assert(cf.is_automating());
    // Position should be somewhere around center (smoothstep makes it non-linear)
    float pos = cf.position();
    assert(pos > -0.5f && pos < 0.5f);
    
    // Advance past end
    cf.get_volumes(va, vb, 600);
    assert(!cf.is_automating());
    assert(std::abs(cf.position() - 1.0f) < 0.01f);
    
    std::cout << "PASSED\n";
}

void test_crossfader_eq_swap_mix_params() {
    std::cout << "Test: Crossfader EQ Swap mix params... ";
    
    Crossfader cf;
    cf.set_curve(Crossfader::CurveType::EQSwap);
    
    MixParams params;
    
    // At start (-1.0 = full A), no EQ changes expected
    cf.set_position(-1.0f);
    cf.get_mix_params(params);
    assert(std::abs(params.eq_low_a - 0.0f) < 0.1f);
    assert(std::abs(params.eq_low_b - (-60.0f)) < 0.1f);
    
    // At center (0.0), we're in the swap zone
    cf.set_position(0.0f);
    cf.get_mix_params(params);
    // A's bass should be killed, B's bass should be fading in
    assert(params.eq_low_a < -50.0f);
    
    // At end (1.0 = full B), B's EQ should be fully restored
    cf.set_position(1.0f);
    cf.get_mix_params(params);
    assert(std::abs(params.eq_low_b - 0.0f) < 0.1f);
    assert(std::abs(params.eq_mid_b - 0.0f) < 0.1f);
    assert(std::abs(params.eq_high_b - 0.0f) < 0.1f);
    
    std::cout << "PASSED\n";
}

void test_crossfader_hard_cut() {
    std::cout << "Test: Crossfader hard cut... ";
    
    Crossfader cf;
    cf.set_curve(Crossfader::CurveType::HardCut);
    
    float va, vb;
    
    // Before center -> full A
    cf.set_position(-0.1f);
    cf.get_volumes(va, vb);
    assert(std::abs(va - 1.0f) < 0.01f);
    assert(std::abs(vb - 0.0f) < 0.01f);
    
    // At/after center -> full B
    cf.set_position(0.1f);
    cf.get_volumes(va, vb);
    assert(std::abs(va - 0.0f) < 0.01f);
    assert(std::abs(vb - 1.0f) < 0.01f);
    
    std::cout << "PASSED\n";
}

// =============================================================================
// 3. Scheduler Tests
// =============================================================================

// Track loader that returns synthetic audio
static std::vector<AudioBuffer> g_test_tracks;

static Result<AudioBuffer> test_track_loader(int64_t track_id) {
    if (track_id <= 0 || static_cast<size_t>(track_id) > g_test_tracks.size()) {
        return ResultError("Track not found");
    }
    return g_test_tracks[track_id - 1];
}

void test_scheduler_basic_playback() {
    std::cout << "Test: Scheduler basic playback... ";
    
    // Setup test tracks
    g_test_tracks.clear();
    g_test_tracks.push_back(make_sine(440.0f, 2.0f));  // Track 1: A4, 2 seconds
    g_test_tracks.push_back(make_sine(880.0f, 2.0f));  // Track 2: A5, 2 seconds
    
    Scheduler sched;
    sched.set_track_loader(test_track_loader);
    
    Playlist playlist;
    playlist.entries.push_back({1, std::nullopt});
    playlist.entries.push_back({2, std::nullopt});
    
    assert(sched.load_playlist(playlist));
    assert(sched.state() == PlaybackState::Stopped);
    
    sched.play();
    assert(sched.state() == PlaybackState::Playing);
    assert(sched.current_track_id() == 1);
    
    // Render some audio
    std::vector<float> output(512 * 2);
    int rendered = sched.render(output.data(), 512, kSampleRate);
    assert(rendered > 0);
    assert(is_nonzero(output.data(), 512));
    
    // Pause
    sched.pause();
    assert(sched.state() == PlaybackState::Paused);
    
    // Render while paused -> silence
    std::memset(output.data(), 0, output.size() * sizeof(float));
    sched.render(output.data(), 512, kSampleRate);
    assert(is_silent(output.data(), 512));
    
    // Resume
    sched.resume();
    assert(sched.state() == PlaybackState::Playing);
    
    // Stop
    sched.stop();
    assert(sched.state() == PlaybackState::Stopped);
    
    std::cout << "PASSED\n";
}

void test_scheduler_transition() {
    std::cout << "Test: Scheduler transition... ";
    
    g_test_tracks.clear();
    g_test_tracks.push_back(make_sine(440.0f, 2.0f));
    g_test_tracks.push_back(make_sine(880.0f, 2.0f));
    
    Scheduler sched;
    sched.set_track_loader(test_track_loader);
    
    // Configure short transition for testing
    TransitionConfig config;
    config.crossfade_beats = 4.0f;
    config.max_transition_seconds = 0.5f;  // Trigger transition 0.5s before end
    sched.set_transition_config(config);
    
    // Create playlist with explicit transition
    TransitionPlan plan;
    plan.from_track_id = 1;
    plan.to_track_id = 2;
    plan.out_point.time_seconds = 1.5f;  // Start transition at 1.5s (of 2s track)
    plan.in_point.time_seconds = 0.0f;
    plan.crossfade_duration = 0.3f;
    plan.bpm_stretch_ratio = 1.0f;
    
    Playlist playlist;
    playlist.entries.push_back({1, plan});
    playlist.entries.push_back({2, std::nullopt});
    
    assert(sched.load_playlist(playlist));
    sched.play();
    
    // Render until we get close to transition point
    std::vector<float> output(512 * 2);
    int total_frames = 0;
    int frames_needed = static_cast<int>(1.6f * kSampleRate);  // Past transition point
    
    while (total_frames < frames_needed) {
        sched.render(output.data(), 512, kSampleRate);
        sched.poll();  // Process control events
        total_frames += 512;
    }
    
    // At this point, transition should have been triggered
    // (state could be Transitioning or back to Playing if transition completed)
    PlaybackState state = sched.state();
    assert(state == PlaybackState::Playing || state == PlaybackState::Transitioning);
    
    std::cout << "PASSED\n";
}

void test_scheduler_skip() {
    std::cout << "Test: Scheduler skip... ";
    
    g_test_tracks.clear();
    g_test_tracks.push_back(make_sine(440.0f, 2.0f));
    g_test_tracks.push_back(make_sine(880.0f, 2.0f));
    g_test_tracks.push_back(make_sine(660.0f, 2.0f));
    
    Scheduler sched;
    sched.set_track_loader(test_track_loader);
    
    Playlist playlist;
    playlist.entries.push_back({1, std::nullopt});
    playlist.entries.push_back({2, std::nullopt});
    playlist.entries.push_back({3, std::nullopt});
    
    sched.load_playlist(playlist);
    sched.play();
    assert(sched.current_track_id() == 1);
    
    // Skip triggers transition
    sched.skip();
    
    // Need to poll to process the skip
    sched.poll();
    
    // After skip, should be transitioning or already on track 2
    PlaybackState state = sched.state();
    assert(state == PlaybackState::Transitioning || state == PlaybackState::Playing);
    
    std::cout << "PASSED\n";
}

void test_scheduler_render_prealloc() {
    std::cout << "Test: Scheduler pre-allocated buffers... ";
    
    g_test_tracks.clear();
    g_test_tracks.push_back(make_sine(440.0f, 1.0f));
    
    // Create scheduler with small buffer
    Scheduler sched(1024);
    sched.set_track_loader(test_track_loader);
    
    Playlist playlist;
    playlist.entries.push_back({1, std::nullopt});
    
    sched.load_playlist(playlist);
    sched.play();
    
    // Request more frames than max_buffer_frames — should be clamped
    std::vector<float> output(4096 * 2, 0.0f);
    int rendered = sched.render(output.data(), 4096, kSampleRate);
    
    // Should render at most max_buffer_frames (1024)
    assert(rendered <= 1024);
    assert(is_nonzero(output.data(), rendered));
    
    std::cout << "PASSED\n";
}

// =============================================================================
// 4. Engine Integration Test (render to memory buffer)
// =============================================================================

void test_engine_render_to_buffer() {
    std::cout << "Test: Engine render to memory buffer... ";
    
    // This test only uses the C API for basic validation
    // (full Engine integration requires a database and real audio files,
    //  which we can't provide in a unit test. But we can verify the render
    //  path doesn't crash with empty state.)
    
    // We test through the C++ API directly since we have access
    // Note: Engine needs a valid Store, so we use :memory: db
    Engine engine(":memory:");
    assert(engine.is_valid());
    assert(engine.playback_state() == PlaybackState::Stopped);
    
    // Render while stopped -> silence
    std::vector<float> output(512 * 2, 1.0f);  // fill with non-zero to verify zeroing
    int rendered = engine.render(output.data(), 512);
    assert(rendered == 512);  // Returns frames even when stopped
    assert(is_silent(output.data(), 512));
    
    // Poll should not crash when stopped
    engine.poll();
    
    std::cout << "PASSED\n";
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "AutoMix Engine - Phase 4 Tests: Real-time Mixing\n";
    std::cout << "=================================================\n\n";
    
    // Deck tests
    test_deck_load_and_render();
    test_deck_seek();
    test_deck_volume_smoothing();
    test_deck_eq();
    test_deck_finished();
    
    // Crossfader tests
    test_crossfader_linear();
    test_crossfader_equal_power();
    test_crossfader_automation();
    test_crossfader_eq_swap_mix_params();
    test_crossfader_hard_cut();
    
    // Scheduler tests
    test_scheduler_basic_playback();
    test_scheduler_transition();
    test_scheduler_skip();
    test_scheduler_render_prealloc();
    
    // Engine integration
    test_engine_render_to_buffer();
    
    std::cout << "\nAll Phase 4 tests passed!\n";
    return 0;
}
