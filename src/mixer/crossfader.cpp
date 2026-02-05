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

void Crossfader::get_volumes(float& deck_a_vol, float& deck_b_vol, int frames) {
    float pos = position_;
    
    // Update automation if active
    if (automating_ && frames > 0) {
        auto_current_frame_ += frames;
        
        if (auto_current_frame_ >= auto_total_frames_) {
            // Automation complete
            pos = auto_end_pos_;
            position_ = pos;
            automating_ = false;
        } else {
            // Interpolate position
            float t = static_cast<float>(auto_current_frame_) / auto_total_frames_;
            // Use ease-in-out curve for smoother transition
            t = t * t * (3.0f - 2.0f * t);  // Smoothstep
            pos = auto_start_pos_ + t * (auto_end_pos_ - auto_start_pos_);
            position_ = pos;
        }
    }
    
    compute_volumes(pos, deck_a_vol, deck_b_vol);
}

void Crossfader::compute_volumes(float pos, float& vol_a, float& vol_b) {
    // Normalize position to 0-1 range (0 = full A, 1 = full B)
    float normalized = (pos + 1.0f) / 2.0f;
    normalized = utils::clamp(normalized, 0.0f, 1.0f);
    
    switch (curve_) {
        case CurveType::Linear:
            vol_a = 1.0f - normalized;
            vol_b = normalized;
            break;
            
        case CurveType::EqualPower:
            // Equal power crossfade maintains constant perceived loudness
            // Volume follows cos/sin curves
            vol_a = std::cos(normalized * M_PI / 2.0f);
            vol_b = std::sin(normalized * M_PI / 2.0f);
            break;
            
        case CurveType::EQSwap:
            // EQ swap: more aggressive curve, used with actual EQ in full implementation
            // For now, use a faster curve
            if (normalized < 0.5f) {
                vol_a = 1.0f;
                vol_b = normalized * 2.0f;
            } else {
                vol_a = (1.0f - normalized) * 2.0f;
                vol_b = 1.0f;
            }
            break;
    }
}

} // namespace automix
