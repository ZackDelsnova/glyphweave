#include "refine.hpp"
#include <iostream>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <unsupported/Eigen/CXX11/Tensor>

namespace {


    // char atlas
    // for chars ' '▁▂▃▄▅▆▇█
    static const std::array<float, 10> CHAR_INTENSITIES = {
        0.00f,  // space
        0.15f,  // ▁
        0.30f,  // ▂
        0.45f,  // ▃
        0.60f,  // ▄
        0.75f,  // ▅
        0.85f,  // ▆
        0.92f,  // ▇
        1.00f,  // █
    };

    // temp schedule
    // linear warmup, then cosine decay
    static float compute_temperature(int iterations, int max_iter, float warmup_ratio,
                                        float temp_max, float temp_min, float temp_init = 0.01f) {
        int warmup_steps = static_cast<int>(max_iter * warmup_ratio);
        if (iterations < warmup_steps) {
            // linear warmup frm 0 to max temp
            float progress = temp_max * static_cast<float>(iterations) / warmup_steps;
            return temp_init + (temp_max - temp_init) * progress;
        } else {
            // cosine decay frm max to min temp
            float progress = static_cast<float>(iterations - warmup_steps) / (max_iter - warmup_steps);
            float cosine = 0.5f * (1.0f + std::cos(static_cast<float>(M_PI) * progress));
            return std::max(temp_min, 0.01f) + (temp_max - std::max(temp_min, 0.01f)) * cosine;
        }
    }

    // softmax
    // 2d tensor (row x col) of logits, apply softmax along the last dim
    static Eigen::Tensor<float, 3> softmax(const Eigen::Tensor<float, 3>& logits, float temperature) {
        using namespace Eigen;
        int rows = logits.dimension(0);
        int cols = logits.dimension(1);
        int chars = logits.dimension(2);

        Tensor<float, 3> probs(rows, cols, chars);

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                // find max for stability
                float max_val = -std::numeric_limits<float>::infinity();
                for (int k = 0; k < chars; ++k) {
                    float val = logits(r, c, k) / temperature;
                    if (val > max_val) max_val = val;
                }
                float sum = 0.0f;
                for (int k = 0; k < chars; ++k) {
                    float val = std::exp(logits(r, c, k) / temperature - max_val);
                    sum += val;
                    probs(r, c, k) = val;
                }
                for (int k = 0; k < chars; ++k) {
                    probs(r, c, k) /= sum;
                }
            }
        }

        return probs;
    }

    // loss and gradient
    // return total loss and fill grad
    static float compute_loss_and_gradient(
        const Eigen::Tensor<float, 3>& probs,
        const Eigen::Tensor<float, 2>& target,
        const std::array<float, 10>& intensities,
        float diversity_weight,
        Eigen::Tensor<float, 3>& grad) {
        
        using namespace Eigen;
        int rows = probs.dimension(0);
        int cols = probs.dimension(1);
        int chars = probs.dimension(2);

        // compute rendered img, R = sum_c probs[r,c,k] * intensity[k]
        Tensor<float, 2> rendered(rows, cols);
        rendered.setZero();
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float val = 0.0f;
                for (int k = 0; k < chars; ++k) {
                    val += probs(r, c, k) * intensities[k];
                }
                rendered(r, c) = val / 255.0f;
            }
        }

        // reconstruction loss MSE
        float recon_loss = 0.0f;
        float scale = 1.0f / (rows * cols);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float diff = (rendered(r, c) - target(r, c) / 255.0f);
                recon_loss += diff * diff;
            }
        }
        recon_loss *= scale;
    
        // diversity loss, negative entropy
        // for uniform distribution
        float div_loss = 0.0f;
        const float eps = 1e-8f;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float entropy = 0.0f;
                for (int k = 0; k < chars; ++k) {
                    float p = probs(r, c, k);
                    if (p > eps) entropy += p * std::log(p);
                }
                div_loss -= entropy; // -ive entropy
            }
        }
        div_loss *= scale; // avg over cell

        float total_loss = recon_loss + diversity_weight * div_loss;

        // compute gradient
        // dL/dR = 2 * (R - target) / (rows * cols), since L = mean((R - target)^2)
        // dL/dprobs = dL/dR * intensity + diversity_weight * (-log(p) - 1)
        // dprobs/dlogits = probs * (delta - probs), softmax derivative
        // dL/dlogits = probs * (dL/dprobs - sum(probs * dL/dprobs))

        // dL_dprobs
        Tensor<float, 3> dL_dprobs(rows, cols, chars);
        dL_dprobs.setZero();

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                // dL_dR
                float dL_dR = 2.0f * (rendered(r, c) - target(r, c) / 255.0f) * scale;
                for (int k = 0; k < chars; ++k) {
                    float p = probs(r, c, k);
                    float dL_dp = dL_dR * intensities[k];
                    // diversity contribution: derivative of -entropy = -log(p) - 1
                    if (p > eps) {
                        dL_dp += diversity_weight * (-std::log(p) - 1.0f);
                    } else {
                        // if p = 0, gradient is undefined, we set it to 0 or big negative
                        // or also approx by treating p as eps
                        dL_dp += diversity_weight * (-std::log(eps) - 1.0f);
                    }
                    dL_dprobs(r, c, k) = dL_dp;
                }
            }
        }

        // dL_dlogits = probs * (dL_dprobs - sum(probs * dL_dprobs) over chars)
        grad.setZero();
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float sum_p_dp = 0.0f;
                for (int k = 0; k < chars; ++k) {
                    sum_p_dp += probs(r, c, k) * dL_dprobs(r, c, k);
                }
                for (int k = 0; k < chars; ++k) {
                    grad(r, c, k) = probs(r, c, k) * (dL_dprobs(r, c, k) - sum_p_dp);
                }
            }
        }

        // clipping to avoid explosions
        const float clip = 10.0f;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                for (int k = 0; k < chars; ++k) {
                    if (grad(r, c, k) > clip) grad(r, c, k) = clip;
                    if (grad(r, c, k) < -clip) grad(r, c, k) = -clip;
                }
            }
        }

        return total_loss;
    }
}

bool refine_image(ProcessedImage& img, const RefineOptions& opts,
                    const std::vector<float>& target_grays) {
    if (!opts.enabled) return false;

    int blocks_x = img.blocks_x;
    int blocks_y = img.blocks_y;

    if (opts.width_override > 0 && opts.width_override != blocks_x) {
        std::cerr << "warning --refine-width is not there, using current grid width " << blocks_x << "\n";
    }

    if (target_grays.size() != static_cast<size_t>(blocks_x * blocks_y)) {
        std::cerr << "error: target_grays size mismatch in refine_image\n";
        return false;
    }

    const std::string& block_chars = " ▁▂▃▄▅▆▇█"; // ig processing options also needs to be passed, will think abt it
    const int num_chars = 10;

    // convert target_grays to eigen matrix
    using namespace Eigen;
    Tensor<float, 2> target(blocks_y, blocks_x);
    for (int y = 0; y < blocks_y; ++y) {
        for (int x = 0; x < blocks_x; ++x) {
            target(y , x) = target_grays[y * blocks_x + x];
        }
    }

    // allocate logits and velocity tensors
    Tensor<float, 3> logits(blocks_y, blocks_x, num_chars);
    Tensor<float, 3> velocity(blocks_y, blocks_x, num_chars);
    velocity.setZero();

    // seed logits frm target grayscale
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> noise(-0.001f, 0.001f);

    const auto& cells = img.cells;
    for (int y = 0; y < blocks_y; ++y) {
        for (int x = 0; x < blocks_x; ++x) {
            float gray = target_grays[y * blocks_x + x];
            int idx = static_cast<int>((gray / 255.0f) * (num_chars - 1) + 0.5f);
            idx = std::clamp(idx, 0, num_chars - 1);
            for (int k = 0; k < num_chars; ++k) {
                logits(y, x, k) = (k == idx) ? 1.0f + noise(gen) : noise(gen);
            }
        }
    }

    // main loop
    int max_iter = opts.max_iterations;
    std::cout << "refining ASCII art with gradient descent ("
        << blocks_y << "x" << blocks_x << " grid, "<< max_iter << " iterations)...\n";
    
    float best_loss = std::numeric_limits<float>::infinity();
    Tensor<float, 3> best_logits = logits; // keep best

    for (int iter = 0; iter < max_iter; ++iter) {
        float temp = compute_temperature(iter, max_iter, opts.warmup_ratio,
                                        opts.temperature_max, opts.temperature_min);
        
        // forward, softmax
        auto probs = softmax(logits, temp);

        // compute loss and gradient
        Tensor<float, 3> grad(blocks_y, blocks_x, num_chars);
        float loss = compute_loss_and_gradient(probs, target, CHAR_INTENSITIES,
                                                opts.diversity_weight, grad);
                                    
        // update best
        if (loss < best_loss) {
            best_loss = loss;
            best_logits = logits;
        }

        // update logits with SGD + momentum
        // velocity = momentum * velocity - lr * grad
        // logits += velocity
        for (int y = 0; y < blocks_y; ++y) {
            for (int x = 0; x < blocks_x; ++x) {
                for (int k = 0; k < num_chars; ++k) {
                    velocity(y, x, k) = opts.momentum * velocity(y, x, k) - opts.learning_rate * grad(y, x, k);
                    logits(y, x, k) += velocity(y, x, k);
                }
            }
        }

        // print progress every 10% or 50 iter
        if (iter % 50 == 0 || iter == max_iter - 1) {
            std::cout << "iter " << iter << "/" << max_iter << " loss = " << loss
                    << " temp = " << temp << "\n";
        }
    }

    // use best logits founf
    logits = best_logits;

    // extract final chars (argmax of proibs at final temp)
    // use low temp for final selection
    auto final_probs = softmax(logits, 0.05f); // very low temp hopefully
    for (int y = 0; y < blocks_y; ++y) {
        for (int x = 0; x < blocks_x; ++x) {
            int best_idx = 0;
            float best_prob = final_probs(y, x, 0);
            for (int k = 1; k < num_chars; ++k) {
                if (final_probs(y, x, k) > best_prob) {
                    best_prob = final_probs(y, x, k);
                    best_idx = k;
                }
            }
            img.cells[y * blocks_x + x].ch = block_chars[best_idx];
        }
    }

    std::cout << "refinement done, final loss: " << best_loss << '\n';
    return true;
}
