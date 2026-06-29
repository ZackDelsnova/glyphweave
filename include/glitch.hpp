#pragma once

#include "types.hpp"

constexpr int DEFAULT_GLITCH_INTERVAL_MS = 100; // or 150 or 200, need to check

struct GlitchOptions {
    // enable flags
    bool enable_row_shift = false;
    bool enable_dropout = false;
    bool enable_sine_warp = false;
    bool enable_jpeg_smash = false;
    bool enable_data_bend = false;
    bool enable_rgb_shift = false;
    bool enable_mirror_slice = false;

    // parameter
    int shift_intensity = 8;
    float dropout_rate = 0.05f;
    float warp_amplitude = 2.0f; // 15 looks solid
    float warp_frequency = 0.1f; // 0.2 is better
    int smash_block_size = 8;
    float smash_intensity = 0.3f;
    float data_bend_chance = 0.1f;
    int rgb_shift_amount = 2;
    int mirror_slice_width = 10;
    bool mirror_vertical = true;
};

// main glitch effect pipeline
void apply_glitch_pipeline(std::vector<Cell>& cells, int blocks_x, int blocks_y,
                           const GlitchOptions& opts);

