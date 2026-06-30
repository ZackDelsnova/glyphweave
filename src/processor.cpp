#include "processor.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>

// bayer matrix 4x4 - values 0 to 15, scazled to 0 to 255 later
static const int BAYER_4x4[4][4] = {
    { 0, 8, 2, 10 },
    { 12,  4, 14,  6 },
    { 3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

// centered bayer matrix 4x4, already scaled and centered
static const int BAYER_CENTERED[4][4] = {
    { -120,   -88,   -88,   -56 },
    {  -56,  -120,   -24,   -88 },
    {  -88,   -56,  -120,   -88 },
    {  -24,   -88,   -56,  -120 }
}; // actual values - bayer_4x4 * 16 - 120

// helper funcs
static void get_avg(const Image& src, unsigned char* out, int x1, int x2, int y1, int y2) {
    float r = 0, g = 0, b = 0;
    int count = 0;
    for (int y = y1; y < y2; ++y) {
        for (int x = x1; x < x2; ++x) {
            int idx = (y * src.w + x) * src.c;
            r += src.data[idx];
            g += src.data[idx + 1];
            b += src.data[idx + 2];
            ++count;
        }
    }
    if (count > 0) {
        out[0] = static_cast<unsigned char>(r / count);
        out[1] = static_cast<unsigned char>(g / count);
        out[2] = static_cast<unsigned char>(b / count);
    }
}

static Image resize_area_average(const Image& src, int new_w, int new_h) {
    Image dst;
    dst.w = new_w;
    dst.h = new_h;
    dst.c = src.c;
    dst.data.resize(new_w * new_h * src.c);

    for (int y = 0; y < new_h; ++y) {
        int y1 = (y * src.h) / new_h;
        int y2 = ((y + 1) * src.h) / new_h;
        for (int x = 0; x < new_w; ++x) {
            int x1 = (x * src.w) / new_w;
            int x2 = ((x + 1) * src.w) / new_w;
            unsigned char* out = &dst.data[(x + y * new_w) * src.c];
            get_avg(src, out, x1, x2, y1, y2);
        }
    }
    return dst;
}

static std::vector<float> prepare_grayscale(const Image& img, float gamma, bool normalize) {
    int total = img.w * img.h;
    std::vector<float> grays(total);
    // grayscale BT.709
    for (int i = 0; i < total; ++i) {
        int idx = i * 3;
        float r = img.data[idx];
        float g = img.data[idx + 1];
        float b = img.data[idx + 2];
        grays[i] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    // gamma (skip if gamma == 1.0f)
    if (gamma != 1.0f) {
        for (float& v : grays) {
            v = std::pow(v / 255.0f, gamma) * 255.0f;
        }
    }

    // normalize
    if (normalize) {
        float min_v = *std::min_element(grays.begin(), grays.end());
        float max_v = *std::max_element(grays.begin(), grays.end());
        float range = max_v - min_v;
        if (range < 1.0f) range = 1.0f;
        for (float& v : grays) {
            v = ((v - min_v) / range) * 255.0f;
        }
    }

    return grays;
}

// block-based dithering processing with error diffusion (Floyd‑Steinberg)
static std::vector<Cell> process_blocks(
    const std::vector<float>& grays,
    const Image& rgb_img, // resized rgb img
    int img_w, int img_h,
    int block_w, int block_h,
    const std::string& block_chars,
    bool use_color, bool fast_dither
) {
    int blocks_x = (img_w + block_w - 1) / block_w;
    int blocks_y = (img_h + block_h - 1) / block_h;
    std::vector<Cell> cells(blocks_x * blocks_y);
    const int NUM_BLOCKS = static_cast<int>(block_chars.size());

    if (fast_dither) {
        for (int by = 0; by < blocks_y; ++by) {
            for (int bx = 0; bx < blocks_x; ++bx) {
                int idx = by * blocks_x + bx;
                int x0 = bx * block_w, y0 = by * block_h;

                // avg grayscale and rgb
                float sum_gray = 0.0f;
                int sum_r = 0, sum_g = 0, sum_b = 0;
                int count = 0;
                for (int dy = 0; dy < block_h; ++dy) {
                    for (int dx = 0; dx < block_w; ++dx) {
                        int px = x0 + dx, py = y0 + dy;
                        if (px < img_w && py < img_h) {
                            sum_gray += grays[py * img_w + px];
                            int pidx = (py * img_w + px) * 3;
                            sum_r += rgb_img.data[pidx];
                            sum_g += rgb_img.data[pidx + 1];
                            sum_b += rgb_img.data[pidx + 2];
                            ++count;
                        }
                    }
                }
                if (count == 0) continue;

                float avg_gray = sum_gray / count;
                // use pre centered bayer threshold
                float dithered = avg_gray + BAYER_CENTERED[x0 % 4][y0 % 4];
                dithered = std::clamp(dithered, 0.0f, 255.0f);

                int char_idx = static_cast<int>((dithered / 255.0f) * (NUM_BLOCKS - 1) + 0.5f);
                char_idx = std::clamp(char_idx, 0, NUM_BLOCKS - 1);

                Cell& cell = cells[idx];
                cell.ch = block_chars[char_idx];
                cell.r = static_cast<unsigned char>(sum_r / count);
                cell.g = static_cast<unsigned char>(sum_g / count);
                cell.b = static_cast<unsigned char>(sum_b / count);
                if (use_color) {
                    cell.br = cell.r / 2;
                    cell.bg = cell.g / 2;
                    cell.bb = cell.b / 2;
                } else {
                    cell.br = cell.bg = cell.bb = 0;
                }
            }
        }
    } else {
        // per-block error accumulator
        std::vector<float> block_errors(blocks_x * blocks_y, 0.0f);
        
        // precompute block sums and counts
        std::vector<float> block_sums(blocks_x * blocks_y, 0.0f);
        std::vector<int> block_counts(blocks_x * blocks_y, 0);

        for (int by = 0; by < blocks_y; ++by) {
            for (int bx = 0; bx < blocks_x; ++bx) {
                int x0 = bx * block_w, y0 = by * block_h;
                float sum = 0.0f;
                int count = 0;
                for (int dy = 0; dy < block_h; ++dy) {
                    for (int dx = 0; dx < block_w; ++dx) {
                        int px = x0 + dx, py = y0 + dy;
                        if (px < img_w && py < img_h) {
                            sum += grays[py * img_w + px];
                            ++count;
                        }
                    }
                }
                block_sums[by * blocks_x + bx] = sum;
                block_counts[by * blocks_x + bx] = count;
            }
        }

        // process blocks in raster order
        for (int by = 0; by < blocks_y; ++by) {
            for (int bx = 0; bx < blocks_x; ++bx) {
                int idx = by * blocks_x + bx;
                float sum = block_sums[idx] + block_errors[idx]; // add accumulated error
                int count = block_counts[idx];
                if (count == 0) continue;

                float avg = sum / count;
                avg = std::clamp(avg, 0.0f, 255.0f);

                // quantise to character index
                float norm = avg / 255.0f;
                int char_idx = static_cast<int>(norm * (NUM_BLOCKS - 1) + 0.5f);
                char_idx = std::clamp(char_idx, 0, NUM_BLOCKS - 1);
                char ch = block_chars[char_idx];

                // quantised value and error
                float quantized = (char_idx / static_cast<float>(NUM_BLOCKS - 1)) * 255.0f;
                float error = avg - quantized;

                // Floyd‑Steinberg error distribution to neighbouring blocks
                // 7/16 (right), 3/16 (bottom‑left), 5/16 (bottom), 1/16 (bottom‑right)
                const float weight = 1.0f / 16.0f;
                if (bx + 1 < blocks_x)
                    block_errors[by * blocks_x + (bx + 1)] += error * 7.0f * weight;
                if (by + 1 < blocks_y) {
                    if (bx > 0)
                        block_errors[(by + 1) * blocks_x + (bx - 1)] += error * 3.0f * weight;
                    block_errors[(by + 1) * blocks_x + bx] += error * 5.0f * weight;
                    if (bx + 1 < blocks_x)
                        block_errors[(by + 1) * blocks_x + (bx + 1)] += error * 1.0f * weight;
                }

                // average RGB for the block
                int sum_r = 0, sum_g = 0, sum_b = 0;
                int rgb_count = 0;
                int x0 = bx * block_w, y0 = by * block_h;
                for (int dy = 0; dy < block_h; ++dy) {
                    for (int dx = 0; dx < block_w; ++dx) {
                        int px = x0 + dx, py = y0 + dy;
                        if (px < img_w && py < img_h) {
                            int pidx = (py * img_w + px) * 3;
                            sum_r += rgb_img.data[pidx];
                            sum_g += rgb_img.data[pidx + 1];
                            sum_b += rgb_img.data[pidx + 2];
                            ++rgb_count;
                        }
                    }
                }

                Cell& cell = cells[idx];
                cell.ch = ch;
                if (rgb_count > 0) {
                    cell.r = static_cast<unsigned char>(sum_r / rgb_count);
                    cell.g = static_cast<unsigned char>(sum_g / rgb_count);
                    cell.b = static_cast<unsigned char>(sum_b / rgb_count);
                    if (use_color) {
                        cell.br = cell.r / 2;
                        cell.bg = cell.g / 2;
                        cell.bb = cell.b / 2;
                    } else {
                        cell.br = cell.bg = cell.bb = 0;
                    }
                }
            }
        }
    }

    return cells;
}

// main processing pipline
ProcessedImage process_image(const unsigned char* pixels, int w, int h, int channels,
                                const ProcessingOptions& opts) {
    // main parameters
    const float CHAR_ASPECT = 2.0f;
    const int TARGET_HEIGHT = std::max(1, static_cast<int>((opts.target_width * h) / (w * CHAR_ASPECT)));

    // src img wrapper
    Image src{w, h, 3, std::vector<unsigned char>(pixels, pixels + w * h * 3)};
    Image resized = resize_area_average(src, opts.target_width, TARGET_HEIGHT);

    // grayscale
    std::vector<float> grays = prepare_grayscale(resized, opts.gamma, opts.normalize);

    // block processing
    std::vector<Cell> cells = process_blocks(grays, resized, opts.target_width, TARGET_HEIGHT,
                                                opts.block_w, opts.block_h, opts.block_chars,
                                                opts.use_color, opts.fast_dither);

    ProcessedImage result;
    result.cells = std::move(cells);
    result.blocks_x = (opts.target_width + opts.block_w - 1) / opts.block_w;
    result.blocks_y = (TARGET_HEIGHT + opts.block_h - 1) / opts.block_h;
    result.target_width = opts.target_width;
    result.target_height = TARGET_HEIGHT;
    result.grayscale_data = std::move(grays);
    return result;
}
