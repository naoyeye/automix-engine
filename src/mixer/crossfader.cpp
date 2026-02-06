/**
 * AutoMix Engine - Crossfader Implementation
 */

#include "crossfader.h"
#include "../core/utils.h"
#include <cmath>

namespace automix {

Crossfader::Crossfader() = default;

void Crossfader::set_position(float position) {
    position_ = utils::clamp(position, -1.0f, 1.0f);
}

void Crossfader::start_automation(float from_position, float to_position, int duration_frames) {
    auto_start_pos_ = from_position;
    auto_end_pos_ = to_position;
    auto_total_frames_ = duration_frames;
    auto_current_frame_ = 0;
    position_ = from_position;
    automating_ = true;
}

void Crossfader::stop_automation() {
    automating_ = false;
}

// Shared automation advancement logic
float Crossfader::advance_automation(int frames) {
    float pos = position_;
    
    if (automating_ && frames > 0) {
        auto_current_frame_ += frames;
        
        if (auto_current_frame_ >= auto_total_frames_) {
            pos = auto_end_pos_;
            position_ = pos;
            automating_ = false;
        } else {
            float t = static_cast<float>(auto_current_frame_) / auto_total_frames_;
            // Smoothstep ease-in-out
            t = t * t * (3.0f - 2.0f * t);
            pos = auto_start_pos_ + t * (auto_end_pos_ - auto_start_pos_);
            position_ = pos;
        }
    }
    
    return pos;
}

void Crossfader::get_volumes(float& deck_a_vol, float& deck_b_vol, int frames) {
    float pos = advance_automation(frames);
    compute_volumes(pos, deck_a_vol, deck_b_vol);
}

void Crossfader::get_mix_params(MixParams& params, int frames) {
    float pos = advance_automation(frames);
    compute_mix_params(pos, params);
}

void Crossfader::compute_volumes(float pos, float& vol_a, float& vol_b) {
    float normalized = (pos + 1.0f) / 2.0f;
    normalized = utils::clamp(normalized, 0.0f, 1.0f);
    
    switch (curve_) {
        case CurveType::Linear:
            vol_a = 1.0f - normalized;
            vol_b = normalized;
            break;
            
        case CurveType::EqualPower:
            vol_a = std::cos(normalized * static_cast<float>(M_PI) / 2.0f);
            vol_b = std::sin(normalized * static_cast<float>(M_PI) / 2.0f);
            break;
            
        case CurveType::EQSwap:
            // Volume curve for EQ swap — both tracks stay loud during transition
            if (normalized < 0.5f) {
                vol_a = 1.0f;
                vol_b = normalized * 2.0f;
            } else {
                vol_a = (1.0f - normalized) * 2.0f;
                vol_b = 1.0f;
            }
            break;
            
        case CurveType::HardCut:
            // Instant cut at center
            vol_a = (normalized < 0.5f) ? 1.0f : 0.0f;
            vol_b = (normalized >= 0.5f) ? 1.0f : 0.0f;
            break;
    }
}

void Crossfader::compute_mix_params(float pos, MixParams& params) {
    float normalized = (pos + 1.0f) / 2.0f;
    normalized = utils::clamp(normalized, 0.0f, 1.0f);
    
    // Default: compute volumes, EQ at unity
    compute_volumes(pos, params.volume_a, params.volume_b);
    params.eq_low_a = 0.0f; params.eq_mid_a = 0.0f; params.eq_high_a = 0.0f;
    params.eq_low_b = 0.0f; params.eq_mid_b = 0.0f; params.eq_high_b = 0.0f;
    
    if (curve_ != CurveType::EQSwap) {
        return;
    }
    
    // =======================================================================
    // EQ Swap transition
    //
    // The classic DJ EQ transition:
    //   Phase 1 (0.0 – 0.4):  Cut outgoing (A) bass, bring in B's highs
    //   Phase 2 (0.4 – 0.6):  Swap zone — both tracks playing, B bass fades in
    //   Phase 3 (0.6 – 1.0):  A fades out, B fully restored
    //
    // EQ values in dB: 0 = unity, -60 = kill
    // =======================================================================
    
    const float kill_db = -60.0f;
    
    if (normalized < 0.4f) {
        // Phase 1: Gradually cut A's bass, bring in B (B highs only at first)
        float t = normalized / 0.4f;  // 0 -> 1
        params.eq_low_a  = kill_db * t;           // A bass: 0 dB -> -60 dB
        params.eq_mid_a  = 0.0f;
        params.eq_high_a = 0.0f;
        
        params.eq_low_b  = kill_db;               // B bass: killed
        params.eq_mid_b  = kill_db * (1.0f - t);  // B mid: -60 dB -> 0 dB
        params.eq_high_b = 0.0f;                  // B highs: full
    } else if (normalized < 0.6f) {
        // Phase 2: Swap zone — A bass killed, B bass fading in
        float t = (normalized - 0.4f) / 0.2f;  // 0 -> 1
        params.eq_low_a  = kill_db;
        params.eq_mid_a  = 0.0f;
        params.eq_high_a = 0.0f;
        
        params.eq_low_b  = kill_db * (1.0f - t);  // B bass: -60 dB -> 0 dB
        params.eq_mid_b  = 0.0f;
        params.eq_high_b = 0.0f;
    } else {
        // Phase 3: A fading out, B fully restored
        float t = (normalized - 0.6f) / 0.4f;  // 0 -> 1
        params.eq_low_a  = kill_db;
        params.eq_mid_a  = kill_db * t;            // A mid fades out
        params.eq_high_a = kill_db * t;            // A highs fade out
        
        params.eq_low_b  = 0.0f;
        params.eq_mid_b  = 0.0f;
        params.eq_high_b = 0.0f;
    }
}

} // namespace automix
