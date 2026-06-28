#pragma once

#include <vector>
#include <string>

struct Cell {
    char ch;
    unsigned char r, g, b;   // fg
    unsigned char br, bg, bb; // bg
};

struct Image {
    int w, h, c;
    std::vector<unsigned char> data;
};

struct ProcessedImage {
    std::vector<Cell> cells;
    int blocks_x;
    int blocks_y;
    int target_width;
    int target_height;
};

struct ProcessingOptions {
    int target_width = 0; // 0 = auto‑detect terminal
    int block_w = 1;
    int block_h = 1;
    float gamma = 1.0f;
    bool normalize = true;
    bool use_color = false;
    std::string block_chars = " ▁▂▃▄▅▆▇█";
};
