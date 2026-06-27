#include <iostream>
#include <algorithm>
#include <vector>
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdint>
#include <execution>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// area avg resize
struct Image {
    int w, h, c;
    std::vector<unsigned char> data;
};

void get_avg(const Image& src, unsigned char *out, int x1, int x2, int y1, int y2) {
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

Image resize_area_average(const Image& src, int new_w, int new_h) {
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
            unsigned char *out = &dst.data[(x + y * new_w) * src.c];
            get_avg(src, out, x1, x2, y1, y2);
        }
    }
    return dst;
}

// bloack based printer
void print_ascii(unsigned char *pixels, int w, int h, int channels, bool use_color = false) {
    // tuneable
    const int TARGET_WIDTH = 500;
    const float CHAR_ASPECT = 2.0f;
    const float GAMMA = 1.0f;
    const bool USE_NORMALIZATION = true;

    const int TARGET_HEIGHT = std::max(1, static_cast<int>((TARGET_WIDTH * h) / (w * CHAR_ASPECT)));

    // src image wrapper
    Image src{w, h, 3, std::vector<unsigned char>(pixels, pixels + w*h*3)};
    Image resized = resize_area_average(src, TARGET_WIDTH, TARGET_HEIGHT);

    // grayscale BT.709
    std::vector<float> grays(TARGET_WIDTH * TARGET_HEIGHT);
    for (int i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
        int idx = i * 3;
        float r = resized.data[idx];
        float g = resized.data[idx + 1];
        float b = resized.data[idx + 2];
        grays[i] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    // gamma corrections
    std::for_each(std::execution::par, grays.begin(), grays.end(),
                  [GAMMA](float &v) { v = std::pow(v / 255.0f, GAMMA) * 255.0f; });

    // min-max normalization
    if (USE_NORMALIZATION) {
        float min_gray = *std::min_element(grays.begin(), grays.end());
        float max_gray = *std::max_element(grays.begin(), grays.end());
        float range = max_gray - min_gray;
        if (range < 1.0f) range = 1.0f;
        std::for_each(std::execution::par, grays.begin(), grays.end(),
                      [min_gray, range](float &v) {
                          v = ((v - min_gray) / range) * 255.0f;
                      });
    }

    // bloack based dithering
    // order 0%, 25%, 50%, 75%, 100%
    const std::string BLOCKS = " ░▒▓█";   // space, light, medium, dark, full
    const int NUM_BLOCKS = BLOCKS.size();

    // each block is 2x2
    const int block_w = 2;
    const int block_h = 2;

    const std::string RESET = "\033[0m";
    const std::string BG_BLACK = "\033[40m"; 

    for (int y = 0; y < TARGET_HEIGHT; ++y) {
        for (int x = 0; x < TARGET_WIDTH; ++x) {
            float sum_gray = 0.0f;
            int sum_r = 0, sum_g = 0, sum_b = 0;
            int count = 0;
            for (int dy = 0; dy < block_h; ++dy) {
                for (int dx = 0; dx < block_w; ++dx) {
                    int px = x + dx;
                    int py = y + dy;
                    if (px < TARGET_WIDTH && py < TARGET_HEIGHT) {
                        int idx = py * TARGET_WIDTH + px;
                        sum_gray += grays[idx];
                        int rgb_idx = idx * 3;
                        sum_r += resized.data[rgb_idx];
                        sum_g += resized.data[rgb_idx + 1];
                        sum_b += resized.data[rgb_idx + 2];
                        ++count;
                    }
                }
            }
            if (count == 0) continue;
            float avg_gray = sum_gray / count;
            int avg_r = sum_r / count;
            int avg_g = sum_g / count;
            int avg_b = sum_b / count;
            
            float norm = std::clamp(avg_gray / 255.0f, 0.0f, 1.0f);
            int char_idx = static_cast<int>(norm * (NUM_BLOCKS - 1) + 0.5f);
            char_idx = std::clamp(char_idx, 0, NUM_BLOCKS - 1);
            char block_char = BLOCKS[char_idx];

            if (use_color) {
                std::cout << "\033[38;2;" << avg_r << ";" << avg_g << ";" << avg_b << "m";
                std::cout << BG_BLACK;
                std::cout << block_char << RESET;
            } else {
                std::cout << block_char;
            }
        }
        std::cout << '\n';
    }
}

// glyphweave <filename> -c[optional print in color]
int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <filename> [-c]\n";
        return EXIT_FAILURE;
    }

    bool use_color = false;
    std::string image_path = argv[1];
    if (argc >= 3 && std::string(argv[2]) == "-c") {
        use_color = true;
    }

    int width, height, channels;
    unsigned char *pixels = stbi_load(image_path.c_str(), &width, &height, &channels, 3);
    if (!pixels) {
        std::cerr << "failed to load image: " << image_path << '\n';
        return EXIT_FAILURE;
    }
    
    print_ascii(pixels, width, height, channels, use_color);
    stbi_image_free(pixels);
    return 0;
}

