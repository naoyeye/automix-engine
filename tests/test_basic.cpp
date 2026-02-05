/**
 * AutoMix Engine - Basic Tests
 */

#include "automix/automix.h"
#include <iostream>
#include <cassert>

void test_engine_creation() {
    std::cout << "Test: Engine creation... ";
    
    // Test with in-memory database
    AutoMixEngine* engine = automix_create(":memory:");
    assert(engine != nullptr);
    
    int count = automix_get_track_count(engine);
    assert(count == 0);
    
    automix_destroy(engine);
    
    std::cout << "PASSED\n";
}

void test_playback_state() {
    std::cout << "Test: Playback state... ";
    
    AutoMixEngine* engine = automix_create(":memory:");
    assert(engine != nullptr);
    
    // Initial state should be stopped
    assert(automix_get_state(engine) == AUTOMIX_STATE_STOPPED);
    
    // Position should be 0
    assert(automix_get_position(engine) == 0.0f);
    
    // Current track should be 0 (none)
    assert(automix_get_current_track(engine) == 0);
    
    automix_destroy(engine);
    
    std::cout << "PASSED\n";
}

void test_transition_config() {
    std::cout << "Test: Transition config... ";
    
    AutoMixEngine* engine = automix_create(":memory:");
    assert(engine != nullptr);
    
    AutoMixTransitionConfig config = {};
    config.crossfade_beats = 32.0f;
    config.use_eq_swap = 1;
    config.stretch_limit = 0.08f;
    
    automix_set_transition_config(engine, &config);
    
    // No easy way to verify config was set, but at least it shouldn't crash
    
    automix_destroy(engine);
    
    std::cout << "PASSED\n";
}

void test_sample_rate() {
    std::cout << "Test: Sample rate... ";
    
    AutoMixEngine* engine = automix_create(":memory:");
    assert(engine != nullptr);
    
    assert(automix_get_sample_rate(engine) == 44100);
    assert(automix_get_channels(engine) == 2);
    
    automix_destroy(engine);
    
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "AutoMix Engine - Basic Tests\n";
    std::cout << "============================\n\n";
    
    test_engine_creation();
    test_playback_state();
    test_transition_config();
    test_sample_rate();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
