/**
 * AutoMix Engine - Phase 2 Module Tests
 * Tests for Store, Decoder, and Analyzer modules
 */

#include "automix/types.h"
#include "../src/core/store.h"
#include "../src/core/utils.h"
#include "../src/decoder/decoder.h"
#include "../src/analyzer/analyzer.h"
#include "../src/analyzer/bpm_detector.h"
#include "../src/analyzer/key_detector.h"
#include "../src/analyzer/energy_analyzer.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <filesystem>

using namespace automix;

/* ============================================================================
 * Test Utilities
 * ============================================================================ */

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Test: " #name "... "; \
    try { \
        test_##name(); \
        std::cout << "PASSED\n"; \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << "\n"; \
        failed_tests++; \
    } \
} while(0)

static int failed_tests = 0;

void assert_near(float actual, float expected, float tolerance, const char* msg = "") {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(std::string(msg) + 
            " Expected: " + std::to_string(expected) + 
            ", Actual: " + std::to_string(actual));
    }
}

/* ============================================================================
 * Store Module Tests
 * ============================================================================ */

TEST(store_create_and_open) {
    Store store(":memory:");
    assert(store.is_open());
    assert(store.error().empty());
}

TEST(store_insert_and_get) {
    Store store(":memory:");
    assert(store.is_open());
    
    // Create a track
    TrackInfo track;
    track.path = "/test/audio.mp3";
    track.bpm = 128.0f;
    track.key = "8A";
    track.duration = 180.0f;
    track.beats = {0.0f, 0.5f, 1.0f, 1.5f};
    track.mfcc = {1.0f, 2.0f, 3.0f};
    track.chroma = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 
                    0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f};
    track.energy_curve = {0.5f, 0.7f, 0.9f, 0.6f};
    track.analyzed_at = 1234567890;
    track.file_modified_at = 1234567800;
    
    // Insert
    auto result = store.upsert_track(track);
    assert(result.ok());
    int64_t track_id = result.value();
    assert(track_id > 0);
    
    // Get by ID
    auto retrieved = store.get_track(track_id);
    assert(retrieved.has_value());
    assert(retrieved->path == track.path);
    assert_near(retrieved->bpm, track.bpm, 0.01f, "BPM mismatch");
    assert(retrieved->key == track.key);
    assert_near(retrieved->duration, track.duration, 0.01f, "Duration mismatch");
    assert(retrieved->beats.size() == track.beats.size());
    assert(retrieved->mfcc.size() == track.mfcc.size());
    assert(retrieved->chroma.size() == track.chroma.size());
    
    // Get by path
    auto by_path = store.get_track_by_path(track.path);
    assert(by_path.has_value());
    assert(by_path->id == track_id);
}

TEST(store_update_existing) {
    Store store(":memory:");
    
    TrackInfo track;
    track.path = "/test/audio.mp3";
    track.bpm = 120.0f;
    
    auto result1 = store.upsert_track(track);
    assert(result1.ok());
    
    // Update with new BPM
    track.bpm = 140.0f;
    auto result2 = store.upsert_track(track);
    assert(result2.ok());
    
    // Verify update
    auto retrieved = store.get_track_by_path(track.path);
    assert(retrieved.has_value());
    assert_near(retrieved->bpm, 140.0f, 0.01f, "BPM not updated");
    
    // Should still be only 1 track
    assert(store.get_track_count() == 1);
}

TEST(store_delete_track) {
    Store store(":memory:");
    
    TrackInfo track;
    track.path = "/test/audio.mp3";
    track.bpm = 120.0f;
    
    auto result = store.upsert_track(track);
    assert(result.ok());
    int64_t track_id = result.value();
    
    assert(store.get_track_count() == 1);
    
    // Delete by ID
    bool deleted = store.delete_track(track_id);
    assert(deleted);
    assert(store.get_track_count() == 0);
    
    // Verify deleted
    auto retrieved = store.get_track(track_id);
    assert(!retrieved.has_value());
}

TEST(store_get_all_tracks) {
    Store store(":memory:");
    
    for (int i = 0; i < 5; ++i) {
        TrackInfo track;
        track.path = "/test/audio" + std::to_string(i) + ".mp3";
        track.bpm = 100.0f + i * 10;
        store.upsert_track(track);
    }
    
    auto tracks = store.get_all_tracks();
    assert(tracks.size() == 5);
    
    // Verify order and content
    for (int i = 0; i < 5; ++i) {
        assert_near(tracks[i].bpm, 100.0f + i * 10, 0.01f, "BPM mismatch in list");
    }
}

TEST(store_search_tracks) {
    Store store(":memory:");
    
    TrackInfo track1;
    track1.path = "/music/electronic/track1.mp3";
    store.upsert_track(track1);
    
    TrackInfo track2;
    track2.path = "/music/jazz/track2.mp3";
    store.upsert_track(track2);
    
    TrackInfo track3;
    track3.path = "/music/electronic/track3.mp3";
    store.upsert_track(track3);
    
    // Search for electronic
    auto results = store.search_tracks("%electronic%");
    assert(results.size() == 2);
}

TEST(store_needs_analysis) {
    Store store(":memory:");
    
    TrackInfo track;
    track.path = "/test/audio.mp3";
    track.file_modified_at = 1000;
    store.upsert_track(track);
    
    // Same modification time - no analysis needed
    assert(!store.needs_analysis(track.path, 1000));
    
    // Older modification time - no analysis needed
    assert(!store.needs_analysis(track.path, 999));
    
    // Newer modification time - needs analysis
    assert(store.needs_analysis(track.path, 1001));
    
    // Non-existent file - needs analysis
    assert(store.needs_analysis("/nonexistent.mp3", 1000));
}

/* ============================================================================
 * Utils Module Tests
 * ============================================================================ */

TEST(utils_math) {
    // clamp
    assert_near(utils::clamp(5.0f, 0.0f, 10.0f), 5.0f, 0.001f, "clamp");
    assert_near(utils::clamp(-5.0f, 0.0f, 10.0f), 0.0f, 0.001f, "clamp min");
    assert_near(utils::clamp(15.0f, 0.0f, 10.0f), 10.0f, 0.001f, "clamp max");
    
    // lerp
    assert_near(utils::lerp(0.0f, 10.0f, 0.5f), 5.0f, 0.001f, "lerp");
    assert_near(utils::lerp(0.0f, 10.0f, 0.0f), 0.0f, 0.001f, "lerp 0");
    assert_near(utils::lerp(0.0f, 10.0f, 1.0f), 10.0f, 0.001f, "lerp 1");
    
    // normalize
    assert_near(utils::normalize(5.0f, 0.0f, 10.0f), 0.5f, 0.001f, "normalize");
}

TEST(utils_cosine_distance) {
    std::vector<float> a = {1.0f, 0.0f, 0.0f};
    std::vector<float> b = {1.0f, 0.0f, 0.0f};
    std::vector<float> c = {0.0f, 1.0f, 0.0f};
    
    // Same vector - distance 0
    assert_near(utils::cosine_distance(a, b), 0.0f, 0.001f, "same vector");
    
    // Orthogonal vectors - distance 1
    assert_near(utils::cosine_distance(a, c), 1.0f, 0.001f, "orthogonal");
}

TEST(utils_bpm_distance) {
    // Same BPM
    assert_near(utils::bpm_distance(120.0f, 120.0f), 0.0f, 0.001f, "same BPM");
    
    // Double time
    assert_near(utils::bpm_distance(120.0f, 60.0f), 0.0f, 0.01f, "double time");
    
    // Half time
    assert_near(utils::bpm_distance(60.0f, 120.0f), 0.0f, 0.01f, "half time");
}

TEST(utils_camelot_distance) {
    // Same key
    assert(utils::camelot_distance("8A", "8A") == 0);
    
    // Adjacent keys
    assert(utils::camelot_distance("8A", "7A") == 1);
    assert(utils::camelot_distance("8A", "9A") == 1);
    
    // Relative major/minor (same number)
    assert(utils::camelot_distance("8A", "8B") == 0);
    
    // Opposite keys
    assert(utils::camelot_distance("1A", "7A") == 6);
}

TEST(utils_keys_compatible) {
    assert(utils::keys_compatible("8A", "8A"));  // Same key
    assert(utils::keys_compatible("8A", "8B"));  // Relative major
    assert(utils::keys_compatible("8A", "7A"));  // Adjacent
    assert(utils::keys_compatible("8A", "9A"));  // Adjacent
    assert(!utils::keys_compatible("8A", "2A")); // Far apart
}

TEST(utils_audio_file_detection) {
    assert(utils::is_audio_file("song.mp3"));
    assert(utils::is_audio_file("song.MP3"));
    assert(utils::is_audio_file("song.flac"));
    assert(utils::is_audio_file("song.wav"));
    assert(utils::is_audio_file("song.m4a"));
    assert(utils::is_audio_file("song.ogg"));
    assert(!utils::is_audio_file("document.txt"));
    assert(!utils::is_audio_file("image.png"));
}

/* ============================================================================
 * Decoder Module Tests
 * ============================================================================ */

TEST(decoder_is_supported) {
    Decoder decoder;
    
    assert(Decoder::is_supported("test.mp3"));
    assert(Decoder::is_supported("test.flac"));
    assert(Decoder::is_supported("test.wav"));
    assert(Decoder::is_supported("test.m4a"));
    assert(!Decoder::is_supported("test.txt"));
}

/* ============================================================================
 * Analyzer Module Tests (with synthetic data)
 * ============================================================================ */

AudioBuffer generate_sine_wave(float frequency, float duration, int sample_rate = 44100) {
    AudioBuffer buffer;
    buffer.sample_rate = sample_rate;
    buffer.channels = 2;
    
    int num_samples = static_cast<int>(duration * sample_rate);
    buffer.samples.resize(num_samples * 2);
    
    for (int i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        float sample = std::sin(2.0f * M_PI * frequency * t) * 0.5f;
        buffer.samples[i * 2] = sample;      // Left
        buffer.samples[i * 2 + 1] = sample;  // Right
    }
    
    return buffer;
}

AudioBuffer generate_click_track(float bpm, float duration, int sample_rate = 44100) {
    AudioBuffer buffer;
    buffer.sample_rate = sample_rate;
    buffer.channels = 2;
    
    int num_samples = static_cast<int>(duration * sample_rate);
    buffer.samples.resize(num_samples * 2, 0.0f);
    
    float beat_interval = 60.0f / bpm;
    int samples_per_beat = static_cast<int>(beat_interval * sample_rate);
    int click_duration = sample_rate / 100;  // 10ms click
    
    for (int beat = 0; beat * samples_per_beat < num_samples; ++beat) {
        int start = beat * samples_per_beat;
        for (int i = 0; i < click_duration && start + i < num_samples; ++i) {
            float envelope = 1.0f - static_cast<float>(i) / click_duration;
            float sample = envelope * 0.8f;
            buffer.samples[(start + i) * 2] = sample;
            buffer.samples[(start + i) * 2 + 1] = sample;
        }
    }
    
    return buffer;
}

TEST(analyzer_bpm_detection) {
    BPMDetector detector;
    
    // Generate a click track at 120 BPM
    auto audio = generate_click_track(120.0f, 10.0f);
    
    auto result = detector.detect(audio);
    assert(result.ok());
    
    float detected_bpm = result.value();
    // Allow 10% tolerance
    assert_near(detected_bpm, 120.0f, 15.0f, "BPM detection");
}

TEST(analyzer_beat_detection) {
    BPMDetector detector;
    
    // Generate a click track at 120 BPM for 5 seconds
    auto audio = generate_click_track(120.0f, 5.0f);
    
    auto result = detector.detect_beats(audio);
    assert(result.ok());
    
    auto beats = result.value();
    // At 120 BPM, we should have about 10 beats in 5 seconds
    assert(beats.size() >= 5);
    assert(beats.size() <= 15);
}

TEST(analyzer_energy_curve) {
    EnergyAnalyzer analyzer;
    
    // Generate audio with varying amplitude
    AudioBuffer buffer;
    buffer.sample_rate = 44100;
    buffer.channels = 2;
    
    // 4 seconds of audio
    int total_samples = 4 * 44100;
    buffer.samples.resize(total_samples * 2);
    
    for (int i = 0; i < total_samples; ++i) {
        float t = static_cast<float>(i) / 44100;
        // Amplitude increases over time
        float amplitude = t / 4.0f;
        float sample = std::sin(2.0f * M_PI * 440.0f * t) * amplitude;
        buffer.samples[i * 2] = sample;
        buffer.samples[i * 2 + 1] = sample;
    }
    
    auto result = analyzer.compute_curve(buffer, 0.5f);
    assert(result.ok());
    
    auto curve = result.value();
    assert(!curve.empty());
    
    // Energy should generally increase (verify first vs last quarter)
    float first_quarter_avg = 0.0f;
    float last_quarter_avg = 0.0f;
    size_t quarter = curve.size() / 4;
    
    for (size_t i = 0; i < quarter; ++i) {
        first_quarter_avg += curve[i];
    }
    for (size_t i = curve.size() - quarter; i < curve.size(); ++i) {
        last_quarter_avg += curve[i];
    }
    
    first_quarter_avg /= quarter;
    last_quarter_avg /= quarter;
    
    assert(last_quarter_avg > first_quarter_avg);
}

TEST(analyzer_key_detection) {
    KeyDetector detector;
    
    // Generate a simple sine wave at A4 (440 Hz) - should detect A major/minor
    auto audio = generate_sine_wave(440.0f, 3.0f);
    
    auto result = detector.detect(audio);
    assert(result.ok());
    
    // Should return a valid Camelot key
    std::string key = result.value();
    assert(!key.empty());
    assert(key.length() >= 2);
    assert(key.back() == 'A' || key.back() == 'B');
}

TEST(analyzer_chroma) {
    KeyDetector detector;
    
    auto audio = generate_sine_wave(440.0f, 2.0f);
    
    auto result = detector.compute_chroma(audio);
    assert(result.ok());
    
    auto chroma = result.value();
    assert(chroma.size() == 12);
    
    // Sum should be close to 1 (normalized)
    float sum = 0.0f;
    for (float v : chroma) {
        sum += v;
        assert(v >= 0.0f);  // All values should be non-negative
    }
    assert_near(sum, 1.0f, 0.01f, "chroma normalization");
}

TEST(analyzer_full_analysis) {
    Analyzer analyzer;
    
    // Generate test audio
    auto audio = generate_click_track(128.0f, 5.0f);
    
    auto result = analyzer.analyze(audio);
    assert(result.ok());
    
    auto features = result.value();
    
    // Verify all features are populated
    assert(features.bpm > 0);
    assert(!features.beats.empty());
    assert(!features.key.empty());
    assert(features.energy_curve.size() > 0);
    assert_near(features.duration, 5.0f, 0.1f, "duration");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main() {
    std::cout << "AutoMix Engine - Phase 2 Module Tests\n";
    std::cout << "======================================\n\n";
    
    std::cout << "--- Store Module ---\n";
    RUN_TEST(store_create_and_open);
    RUN_TEST(store_insert_and_get);
    RUN_TEST(store_update_existing);
    RUN_TEST(store_delete_track);
    RUN_TEST(store_get_all_tracks);
    RUN_TEST(store_search_tracks);
    RUN_TEST(store_needs_analysis);
    
    std::cout << "\n--- Utils Module ---\n";
    RUN_TEST(utils_math);
    RUN_TEST(utils_cosine_distance);
    RUN_TEST(utils_bpm_distance);
    RUN_TEST(utils_camelot_distance);
    RUN_TEST(utils_keys_compatible);
    RUN_TEST(utils_audio_file_detection);
    
    std::cout << "\n--- Decoder Module ---\n";
    RUN_TEST(decoder_is_supported);
    
    std::cout << "\n--- Analyzer Module ---\n";
    RUN_TEST(analyzer_bpm_detection);
    RUN_TEST(analyzer_beat_detection);
    RUN_TEST(analyzer_energy_curve);
    RUN_TEST(analyzer_key_detection);
    RUN_TEST(analyzer_chroma);
    RUN_TEST(analyzer_full_analysis);
    
    std::cout << "\n======================================\n";
    if (failed_tests == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    } else {
        std::cout << failed_tests << " test(s) failed.\n";
        return 1;
    }
}
