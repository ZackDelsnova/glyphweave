#include "glitch.hpp"
#include <random>
#include <vector>
#include <algorithm>

void apply_row_shift_glitch(std::vector<Cell>& cells, int blocks_x, int blocks_y) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> row_dist(0, blocks_y - 1);
    static std::uniform_int_distribution<> shift_dist(-8, 8);

    // no of rows to glitch - 20%
    int num_rows = std::max(1, static_cast<int>(blocks_y * 0.2f));
    for (int i = 0; i < num_rows; ++i) {
        int row = row_dist(gen);
        int shift = shift_dist(gen);
        if (shift == 0) continue;

        // org copy
        std::vector<Cell> original_row(blocks_x);
        int row_start = row * blocks_x;
        for (int x = 0; x < blocks_x; ++x)
            original_row[x] = cells[row_start + x];

        // wrap / shift cyclically
        for (int x = 0; x < blocks_x; ++x) {
            int src_idx = (x - shift) % blocks_x;
            if (src_idx < 0) src_idx += blocks_x;
            cells[row_start + x] = original_row[src_idx];
        }
    }
}