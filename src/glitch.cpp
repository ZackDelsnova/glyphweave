#include "glitch.hpp"
#include <random>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

    // randomly shift whole rows left/right
    // makes it look like CRT
    void apply_row_shift_glitch(std::vector<Cell>& cells, int blocks_x, int blocks_y, int intensity) {
        if (intensity <= 0 || blocks_y <= 0) return;
        
        static std::random_device rd;
        static std::mt19937 gen(rd());

        static std::uniform_int_distribution<> row_dist(0, blocks_y - 1);
        static std::uniform_int_distribution<> shift_dist(-intensity, intensity);

        static std::vector<Cell> original_row;
        if (static_cast<int>(original_row.size()) != blocks_x) 
            original_row.resize(blocks_x);

        // no of rows to glitch - 20%
        int num_rows = std::max(1, static_cast<int>(blocks_y * 0.2f));
        for (int i = 0; i < num_rows; ++i) {
            int row = row_dist(gen);
            int shift = shift_dist(gen);
            if (shift == 0) continue;

            int row_start = row * blocks_x;
            // copy whole row into buffer
            std::memcpy(original_row.data(), &cells[row_start], blocks_x * sizeof(Cell));

            // wrap / shift cyclically
            for (int x = 0; x < blocks_x; ++x) {
                int src_idx = (x - shift) % blocks_x;
                if (src_idx < 0) src_idx += blocks_x;
                cells[row_start + x] = original_row[src_idx];
            }
        }
    }

    // randomly replace a cell with space
    // making a sparkle or missing effect
    void apply_dropout(std::vector<Cell>& cells, int /*blocks_x*/, int /*blocks_y*/, float rate) {
        if (rate <= 0.0f || rate >= 1.0f) return;
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        for (auto& cell : cells) {
            if (dist(gen) < rate) {
                cell.ch = ' '; // space
                cell.br = cell.bg = cell.bb = 0;
            }
        }
    }

    // shifts each row's content horizontally (sleeping line) based on sine wave of row number
    // makes a wavy, fluid distortion
    void apply_sine_warp(std::vector<Cell>& cells, int blocks_x, int blocks_y,
                         float amplitude, float frequency) {
        
        if (amplitude <= 0.0f || blocks_x <= 0) return;

        static std::vector<Cell> row_buffer;
        if (static_cast<int>(row_buffer.size()) != blocks_x)
            row_buffer.resize(blocks_x);
        
        for (int y = 0; y < blocks_y; ++y) {
            // calc shift: amplitude * sin(frequency * y)
            float shift_float = amplitude * std::sin(frequency * static_cast<float>(y));
            int shift = static_cast<int>(std::round(shift_float));
            if (shift == 0) continue;

            int row_start = y * blocks_x;
            // copy whole row into buffer
            std::memcpy(row_buffer.data(), &cells[row_start], blocks_x * sizeof(Cell));

            for (int x = 0; x < blocks_x; ++x) {
                int src_x = (x - shift) % blocks_x;
                if (src_x < 0) src_x += blocks_x;
                cells[row_start + x] = row_buffer[src_x];
            }
        }
    }

    // separates rgb channels and shifts them slightly apart horizontally
    // makes a stiking "ghost" or 3d-glasses effect
    void apply_rgb_shift(std::vector<Cell>& cells, int blocks_x, int blocks_y, int amount) {
        if (amount <= 0) return;

        static std::vector<Cell> row_buffer;
        if (static_cast<int>(row_buffer.size()) != blocks_x)
            row_buffer.resize(blocks_x);

        for (int y = 0; y < blocks_y; ++y) {
            int row_start = y * blocks_x;
            // copy whole row into buffer
            std::memcpy(row_buffer.data(), &cells[row_start], blocks_x * sizeof(Cell));            

            for (int x = 0; x < blocks_x; ++x) {
                int idx = row_start + x;
                int src_r = (x - amount + blocks_x) % blocks_x; // red shift to left
                int src_b = (x + amount) % blocks_x; // blue shifts to right

                cells[idx].r = row_buffer[src_r].r;
                cells[idx].g = row_buffer[x].g; // green same
                cells[idx].b = row_buffer[src_b].b;

                // shift bg color for consistency
                cells[idx].br = row_buffer[src_r].br;
                cells[idx].bg = row_buffer[x].bg;
                cells[idx].bb = row_buffer[src_b].bb;
            }
        }
    }

    // splits img vertical or horizontal strips,
    // every second strip is flipped horizontally or vertically
    // makes a cool mirrored distortion
    void apply_mirror_slice(std::vector<Cell>& cells, int blocks_x, int blocks_y,
                            int slice_width, bool vertical) {
        if (slice_width <= 0 || blocks_x <= 0 || blocks_y <= 0) return;
        
        if (vertical) {
            // vertical - flip every second vertical strip horizontally
            for (int start_x = 0; start_x < blocks_x; start_x += slice_width) {
                int end_x = std::min(start_x + slice_width, blocks_x);
                int slice_idx = start_x / slice_width;

                // flip odd slices, 1st, 3rd, 5th
                if (slice_idx % 2 == 1) {
                    for (int y = 0; y < blocks_y; ++y) {
                        for (int x1 = start_x, x2 = end_x - 1; x1 < x2; ++x1, --x2) {
                            std::swap(cells[y * blocks_x + x1], cells[y * blocks_x + x2]);
                        }
                    }
                }
            }
        } else {
            // horizontal - flip every horizontal strip vertically
            for (int start_y = 0; start_y < blocks_y; start_y += slice_width) {
                int end_y = std::min(start_y + slice_width, blocks_y);
                int slice_idx = start_y / slice_width;

                // flip odd slices, 1st, 3rd, 5th
                if (slice_idx % 2 == 1) {
                    for (int x = 0; x < blocks_x; ++x) {
                        for (int y1 = start_y, y2 = end_y - 1; y1 < y2; ++y1, --y2) {
                            std::swap(cells[y1 * blocks_x + x], cells[y2 * blocks_x + x]);
                        }
                    }
                }
            }
        }
    }

    // corrupts small blocks by replacing them with random blocks
    void apply_jpeg_smash(std::vector<Cell>& cells, int blocks_x, int blocks_y,
                          int block_size, float intensity) {
        if (block_size <= 0 || intensity <= 0.0f || blocks_x <= 0 || blocks_y <= 0) return;

        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> prob(0.0f, 1.0f);

        // num of blocks in each dim
        int num_blocks_x = (blocks_x + block_size - 1) / block_size;
        int num_blocks_y = (blocks_y + block_size - 1) / block_size;

        // persistent block buffer, hopefully enough for the largest block
        static std::vector<Cell> block_buffer;
        if (static_cast<int>(block_buffer.size()) < block_size * block_size)
            block_buffer.resize(block_size * block_size);

        for (int by = 0; by < blocks_y; by += block_size) {
            for (int bx = 0; bx < blocks_x; bx += block_size) {
                if (prob(gen) >= intensity) continue;

                // pick random src block, not this one
                int src_bx, src_by;
                do {
                    src_bx = (std::uniform_int_distribution<>(0, num_blocks_x - 1))(gen) * block_size;
                    src_by = (std::uniform_int_distribution<>(0, num_blocks_y - 1))(gen) * block_size;
                } while (src_bx == bx && src_by == by);

                // copy frm src block to dest block, edges handled probably
                int dest_end_x = std::min(bx + block_size, blocks_x);
                int dest_end_y = std::min(by + block_size, blocks_y);
                int src_end_x = std::min(src_bx + block_size, blocks_x);
                int src_end_y = std::min(src_by + block_size, blocks_y);

                int dest_w = dest_end_x - bx;
                int dest_h = dest_end_y - by;
                int src_w  = src_end_x - src_bx;
                int src_h  = src_end_y - src_by;

                // copy src blocks row by row into buffer
                for (int dy = 0; dy < src_h; ++dy) {
                    int src_idx = (src_by + dy) * blocks_x + src_bx;
                    std::memcpy(&block_buffer[dy * src_w], &cells[src_idx], src_w * sizeof(Cell));
                }

                // write buffer to dest
                // clip if src smaller than dest
                for (int dy = 0; dy < dest_h; ++dy) {
                    int dest_idx = (by + dy) * blocks_x + bx;
                    int src_row = dy % src_h; // wrap if src is smaller
                    std::memcpy(&cells[dest_idx], &block_buffer[src_row * src_w], dest_w * sizeof(Cell));
                }
            }
        }
    }

    // bitwise corruption and random byte swaps for chaotic digital chaos
    void apply_data_bend(std::vector<Cell>& cells, int /*blocks_x*/, int /*blocks_y*/, float chance) {
        if (chance <= 0.0f || chance >= 1.0f) return;

        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> prob(0.0f, 1.0f);
        std::uniform_int_distribution<int> op_dist(0, 5);
        std::uniform_int_distribution<int> mask_dist(0, 255);
        std::uniform_int_distribution<size_t> cell_idx_dist(0, cells.size() - 1);

        for (size_t i = 0; i < cells.size(); ++i) {
            if (prob(gen) >= chance) continue;

            // decide bitwise op or swap
            if (prob(gen) < 0.5f) {
                // random bitwise op to all bytes of this cell
                int op = op_dist(gen);
                unsigned char mask = static_cast<unsigned char>(mask_dist(gen));
                auto apply = [&](unsigned char& byte) {
                    switch (op) {
                        case 0: byte ^= mask; break;
                        case 1: byte |= mask; break;
                        case 2: byte &= mask; break;
                        case 3: byte = ~mask; break;
                        case 4: // rot left by 1
                            byte = (byte << 1) | (byte >> 7);
                            break;
                        case 5: // swap nibbles
                            byte = ((byte & 0x0F) << 4) | ((byte & 0xF0) >> 4);
                            break;
                    }
                };

                apply(cells[i].r);
                apply(cells[i].g);
                apply(cells[i].b);
                apply(cells[i].br);
                apply(cells[i].bg);
                apply(cells[i].bb);
            } else {
                // swap cell with random cell
                size_t j = cell_idx_dist(gen);
                if (j != i) {
                    std::swap(cells[i], cells[j]);
                }
            }
        }
    }
}

void apply_glitch_pipeline(std::vector<Cell>& cells, int blocks_x, int blocks_y,
                           const GlitchOptions& opts) {
    // order matters: structure -> corruption -> final visuals

    // structures - 2
    if (opts.enable_mirror_slice)
        apply_mirror_slice(cells, blocks_x, blocks_y, opts.mirror_slice_width, opts.mirror_vertical);

    if (opts.enable_sine_warp)
        apply_sine_warp(cells, blocks_x, blocks_y, opts.warp_amplitude, opts.warp_frequency);

    // corruption - 3
    if (opts.enable_jpeg_smash)
        apply_jpeg_smash(cells, blocks_x, blocks_y, opts.smash_block_size, opts.smash_intensity);

    if (opts.enable_data_bend)
        apply_data_bend(cells, blocks_x, blocks_y, opts.data_bend_chance);

    if (opts.enable_dropout)
        apply_dropout(cells, blocks_x, blocks_y, opts.dropout_rate);

    // final visuals
    if (opts.enable_row_shift)
        apply_row_shift_glitch(cells, blocks_x, blocks_y, opts.shift_intensity);

    if (opts.enable_rgb_shift)
        apply_rgb_shift(cells, blocks_x, blocks_y, opts.rgb_shift_amount);
}
