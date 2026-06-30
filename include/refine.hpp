#pragma once

#include "types.hpp"
#include <vector>

struct RefineOptions {
    bool enabled = false;
    int max_iterations = 500;
    int width_override = 0;
    float diversity_weight = 0.01f;
    float learning_rate = 0.1f; // SGD learning rate
    float momentum = 0.9f; // SGD momentum
    float temperature_max = 1.0f; // Starting temperature
    float temperature_min = 0.1f; // Final temperature
    float warmup_ratio = 0.1f; // 10% of iterations for warmup
};

// DEPRECATED: does nothin, only prints a warning
// The Floyd‑Steinberg dithering is superior, so refinement is disabled

// refine char selection in img, using gradient descent
// target_grays must be vec of length blocks_x * blocks_y
// contains target grayscale val 0 to 255 for each cell
// return trye if refine was success else false
bool refine_image(ProcessedImage& img, const RefineOptions& opts,
                    const std::vector<float>& target_grays);
