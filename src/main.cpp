#include <iostream>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <execution>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <fstream>

#define _USE_MATH_DEFINES
#include <cmath>

#define NOMINMAX
#include <Windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "gif.h"

std::atomic<bool> g_exit_requested = false;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        g_exit_requested = true;
    }
}

int get_terminal_width() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
}

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

// area avg resize
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

// grayscaler
std::vector<float> prepare_grayscale(const Image& img, float gamma, bool normalize) {
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

    // gamma
    for (float& v : grays) {
        v = std::pow(v / 255.0f, gamma) * 255.0f;
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

// block-based dithering processing with error diffusion 
std::vector<Cell> process_blocks(
    const std::vector<float>& grays,
    const Image& rgb_img, // resized rgb img
    int img_w, int img_h,
    int block_w, int block_h,
    const std::string& block_chars,
    bool use_color
) {
    int blocks_x = (img_w + block_w - 1) / block_w;
    int blocks_y = (img_h + block_h - 1) / block_h;
    std::vector<Cell> cells(blocks_x * blocks_y);

    // error diffusion
    std::vector<float> errors(img_w * img_h, 0.0f);
    const int NUM_BLOCKS = block_chars.size();

    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            int x0 = bx * block_w;
            int y0 = by * block_h;

            // avg grayscale + error
            float sum = 0.0f;
            int count = 0;
            for (int dy = 0; dy < block_h; ++dy) {
                for (int dx = 0; dx < block_w; ++dx) {
                    int px = x0 + dx;
                    int py = y0 + dy;
                    if (px < img_w && py < img_h) {
                        int idx = py * img_w + px;
                        sum += grays[idx] + errors[idx];
                        ++count;
                    }
                }
            }

            if (count == 0) continue;
            float avg = sum / count;
            avg = std::clamp(avg, 0.0f, 255.0f);

            // quantize
            float norm = avg / 255.0f;
            int char_idx = static_cast<int>(norm * (NUM_BLOCKS - 1) + 0.5f);
            char_idx = std::clamp(char_idx, 0, NUM_BLOCKS - 1);
            char ch = block_chars[char_idx];

            float quantized = (char_idx / (float)(NUM_BLOCKS - 1)) * 255.0f;
            float error = avg - quantized;

            // distrubute error by floyd-steinberg
            float weight = 1.0f / 16.0f;
            if (x0 + block_w < img_w) {
                errors[y0 * img_w + (x0 + block_w)] += error * 7.0f * weight;
            }
            if (y0 + block_h < img_h) {
                if (x0 > 0) {
                    errors[(y0 + block_h) * img_w + (x0 - 1)] += error * 3.0f * weight;
                }
                errors[(y0 + block_h) * img_w + x0] += error * 5.0f * weight;
                if (x0 + block_w < img_w) {
                    errors[(y0 + block_h) * img_w + (x0 + block_w)] += error * 1.0f * weight;
                }
            }

            // avg rgb
            int sum_r = 0, sum_g = 0, sum_b = 0;
            int rgb_count = 0;
            for (int dy = 0; dy < block_h; ++dy) {
                for (int dx = 0; dx < block_w; ++dx) {
                    int px = x0 + dx, py = y0 + dy;
                    if (px < img_w && py < img_h) {
                        int idx = (py * img_w + px) * 3;
                        sum_r += rgb_img.data[idx];
                        sum_g += rgb_img.data[idx + 1];
                        sum_b += rgb_img.data[idx + 2];
                        ++rgb_count;
                    }
                }
            }

            Cell& cell = cells[by * blocks_x + bx];
            cell.ch = ch;
            if (rgb_count > 0) {
                cell.r = sum_r / rgb_count;
                cell.g = sum_g / rgb_count;
                cell.b = sum_b / rgb_count;
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

    return cells;
}

// main processing
ProcessedImage process_image(unsigned char *pixels, int w, int h, int channels, bool use_color, int target_width) {
    // main parameters
    const float CHAR_ASPECT = 2.0f;
    const float GAMMA = 1.0f;
    const bool USE_NORMALIZATION = true;
    const int TARGET_HEIGHT = std::max(1, static_cast<int>((target_width * h) / (w * CHAR_ASPECT)));

    // src image wrapper
    Image src{w, h, 3, std::vector<unsigned char>(pixels, pixels + w*h*3)};
    Image resized = resize_area_average(src, target_width, TARGET_HEIGHT);
    // gray scaled thing
    std::vector<float> grays = prepare_grayscale(resized, GAMMA, USE_NORMALIZATION);

    // block processing
    // order 0%, 25%, 50%, 75%, 100%
    // each block is 2x2
    const int block_w = 2, block_h = 2;
    const std::string BLOCKS = " ░▒▓█";   // space, light, medium, dark, full
    std::vector<Cell> cells = process_blocks(grays, resized, target_width, TARGET_HEIGHT,
                                            block_w, block_h, BLOCKS, use_color);

    ProcessedImage result;
    result.cells = std::move(cells);
    result.blocks_x = (target_width + block_w - 1) / block_w;
    result.blocks_y = (TARGET_HEIGHT + block_h - 1) / block_h;
    result.target_width = target_width;
    result.target_height = TARGET_HEIGHT;
    return result;
}

// render to console
std::string render_to_console(const ProcessedImage& img, bool use_color) {
    std::string output;
    output.reserve(img.blocks_x * img.blocks_y * 20);
    for (int by = 0; by < img.blocks_y; ++by) {
        for (int bx = 0; bx < img.blocks_x; ++bx) {
            const Cell& cell = img.cells[by * img.blocks_x + bx];
            if (use_color) {
                output += "\033[38;2;" + std::to_string(cell.r) + ";" + std::to_string(cell.g) + ";" + std::to_string(cell.b) + "m"; // fg
                output += "\033[48;2;" + std::to_string(cell.br) + ";" + std::to_string(cell.bg) + ";" + std::to_string(cell.bb) + "m"; //bg
                output += cell.ch;
                output += "\033[0m"; // clear
            } else {
                output += cell.ch;
            }
        }
        output += '\n';
    }
    return output;
}

// save to png
void render_to_png(const ProcessedImage& img, const std::string& filename, bool use_color, int cell_w = 8, int cell_h = 12) {
    int image_w = img.blocks_x * cell_w;
    int image_h = img.blocks_y * cell_h;
    std::vector<unsigned char> pixels(image_w * image_h * 3, 0);

    auto get_cell_color = [&](const Cell& cell) -> std::tuple<unsigned char, unsigned char, unsigned char> {
        
    };

    for (int by = 0; by < img.blocks_y; ++by) {
        for (int bx = 0; bx < img.blocks_x; ++bx) {
            const Cell& cell = img.cells[by * img.blocks_x + bx];
            unsigned char fg_r, fg_g, fg_b;
            if (use_color) {
                fg_r = cell.r; fg_g = cell.g; fg_b = cell.b;
            } else {
                // BT.709 luminance
                unsigned char gray = static_cast<unsigned char>(
                    0.2126f * cell.r + 0.7152f * cell.g + 0.0722f * cell.b
                );
                fg_r = fg_g = fg_b = gray;
            }
            // for block based thing, just fill with fg color
            // in advanced, u need to draw the charaqcter shape
            int x0 = bx * cell_w;
            int y0 = by * cell_h;
            for (int dy = 0; dy < cell_h; ++dy) {
                for (int dx = 0; dx < cell_w; ++dx) {
                    int px = (y0 + dy) * image_w + (x0 + dx);
                    // need to add 2tone fill color
                    pixels[px * 3] = fg_r;
                    pixels[px * 3 + 1] = fg_g;
                    pixels[px * 3 + 2] = fg_b;
                }
            }
        }
    }

    if (!stbi_write_png(filename.c_str(), image_w, image_h, 3, pixels.data(), image_w * 3)) {
        std::cerr << "failed to make png: " << filename << '\n';
    } else {
        std::cout << "png saved: " << filename << '\n';
    }
}

// row shifter glitch effect
void apply_row_shift_glitch(std::vector<Cell>& cells, int blocks_x, int blocks_y) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> row_dist(0, blocks_y - 1);
    static std::uniform_int_distribution<> shift_dist(-8, 8);

    int num_rows = std::max(1, static_cast<int>(blocks_y * 0.2f));
    for (int i = 0; i < num_rows; ++i) {
        int row = row_dist(gen);
        int shift = shift_dist(gen);
        if (shift == 0) continue;
        std::vector<Cell> original_row(blocks_x);
        int row_start = row * blocks_x;
        for (int x = 0; x < blocks_x; ++x)
            original_row[x] = cells[row_start + x];
        for (int x = 0; x < blocks_x; ++x) {
            int src_idx = (x - shift) % blocks_x;
            if (src_idx < 0) src_idx += blocks_x;
            cells[row_start + x] = original_row[src_idx];
        }
    }
}

// save as gif
void render_to_gif(const ProcessedImage& base_img, const std::string& filename,
                   bool use_color, bool use_glitch, int interval_ms = 100,
                   int duration_seconds = 5, int cell_w = 8, int cell_h = 12) {
    int image_w = base_img.blocks_x * cell_w;
    int image_h = base_img.blocks_y * cell_h;
    // frames = duration/interval
    int num_frames = (duration_seconds * 1000) / interval_ms;
    num_frames = std::clamp(num_frames, 1, 200);

    GifWriter writer;
    if (!GifBegin(&writer, filename.c_str(), image_w, image_h, interval_ms)) {
        std::cerr << "failed to gif: " << filename << '\n';
        return;
    }

    // frame buffer - gif.h needs RGBA
    std::vector<unsigned char> frame(image_w * image_h * 4, 0);

    // frame filling helper
    auto fill_frame = [&](const std::vector<Cell>& cells) {
        for (int by = 0; by < base_img.blocks_y; ++by) {
            for (int bx = 0; bx < base_img.blocks_x; ++bx) {
                const Cell& cell = cells[by * base_img.blocks_x + bx];
                unsigned char fg_r, fg_g, fg_b;
                if (use_color) {
                    fg_r = cell.r; fg_g = cell.g; fg_b = cell.b;
                } else {
                    // BT.709 luminance
                    unsigned char gray = static_cast<unsigned char>(
                        0.2126f * cell.r + 0.7152f * cell.g + 0.0722f * cell.b
                    );
                    fg_r = fg_g = fg_b = gray;
                }
                int x0 = bx * cell_w;
                int y0 = by * cell_h;
                for (int dy = 0; dy < cell_h; ++dy) {
                    for (int dx = 0; dx < cell_w; ++dx) {
                        int idx = ((y0 + dy) * image_w + (x0 + dx)) * 4;
                        frame[idx] = fg_r;
                        frame[idx+1] = fg_g;
                        frame[idx+2] = fg_b;
                        frame[idx+3] = 255; // fully opaque for alpha
                    }
                } 
            }
        }
    };

    // gen frame
    if (use_glitch) {
        for (int f = 0; f < num_frames; ++f) {
            std::vector<Cell> glitched = base_img.cells;
            apply_row_shift_glitch(glitched, base_img.blocks_x, base_img.blocks_y);
            fill_frame(glitched);
            GifWriteFrame(&writer, frame.data(), image_w, image_h, interval_ms);
        }
    } else {
        // 1 frame alone
        fill_frame(base_img.cells);
        GifWriteFrame(&writer, frame.data(), image_w, image_h, interval_ms);
    }

    GifEnd(&writer);
    std::cout << "gif saved: " << filename << '\n';
}

// output with glitch loop
void print_with_glitch(const ProcessedImage& base_img, bool use_color, int interval_ms) {
    std::signal(SIGINT, signal_handler);
    std::cout << "\033[?25l"; // hide cursor
    while (!g_exit_requested) {
        std::vector<Cell> glitched_cells = base_img.cells;
        apply_row_shift_glitch(glitched_cells, base_img.blocks_x, base_img.blocks_y);
        std::string output = render_to_console({glitched_cells, base_img.blocks_x, base_img.blocks_y,
                                                base_img.target_width, base_img.target_height}, use_color);
        std::cout << "\033[2J\033[H" << output << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    std::cout << "\033[?25h" << "\033[0m" << std::flush;
}

// file saving .txt
void save_to_file(const std::string& content, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "error writing to file: " << filename << '\n';
        return;
    }
    file << content;
    file.close();
    std::cout << "saved: " << filename << '\n';
}

// glyphweave <filename> -c[optional print in color] [--glitch] [--interval ms] (both optional)
// [-o/--output only for .txt file] [--png filename] [--gif filename] [--width for custom width or default terminal width]
int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <filename> [-c] [--glitch] [--interval ms] [-o output.txt] [--width]\n";
        return EXIT_FAILURE;
    }

    int target_width = 0; // 0 = auto detect console width
    bool use_color = false, use_glitch = false;
    int glitch_interval_ms = 100;
    std::string image_path;
    std::string output_file, png_file, gif_file;
    
    // argument parser
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--color") {
            use_color = true;
        } else if (arg == "--glitch") {
            use_glitch = true;
        } else if (arg == "--interval") {
            if (i + 1 < argc) {
                glitch_interval_ms = std::stoi(argv[++i]);
            } else {
                std::cerr << "error: --interval needs a value\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                std::cerr << "error: -o needs a filename\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "--png") {
            if (i + 1 < argc) {
                png_file = argv[++i];
            } else {
                std::cerr << "error: --png needs a filename\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "--gif") {
            if (i + 1 < argc) {
                gif_file = argv[++i];
            } else {
                std::cerr << "error: --gif needs a filename\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "--width") {
            if (i + 1 < argc) {
                target_width = std::stoi(argv[++i]);
            } else {
                std::cerr << "error: --width needs a number\n";
                return EXIT_FAILURE;
            }
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << '\n';
            return EXIT_FAILURE;
        } else {
            if (image_path.empty())
                image_path = arg;
            else
                std::cerr << "extra argument '" << arg << "' ignored\n";
        }
    }

    if (image_path.empty()) {
        std::cerr << "no image provided\n";
        return EXIT_FAILURE;
    }

    int width, height, channels;
    unsigned char *pixels = stbi_load(image_path.c_str(), &width, &height, &channels, 3);
    if (!pixels) {
        std::cerr << "failed to load image: " << image_path << '\n';
        return EXIT_FAILURE;
    }

    int width_to_use;
    if (!output_file.empty()) {
        width_to_use = (target_width > 0) ? target_width : 600;
    } else {
        if (target_width > 0) {
            width_to_use = target_width;
        } else {
            width_to_use = get_terminal_width() - 2; // margin
        }
    }
    width_to_use = std::max(80, width_to_use);

    ProcessedImage img = process_image(pixels, width, height, channels, use_color, width_to_use);

    // output decision
    if (!png_file.empty()) {
        render_to_png(img, png_file, use_color);
    }
    if (!gif_file.empty()) {
        render_to_gif(img, gif_file, use_color, use_glitch, glitch_interval_ms);
    }
    if (!output_file.empty()) {
        std::string text = render_to_console(img, use_color);
        save_to_file(text, output_file);
    }
    // no output - print to console
    if (output_file.empty() && png_file.empty() && gif_file.empty()) {
        if (use_glitch) print_with_glitch(img, use_color, glitch_interval_ms);
        else std::cout << render_to_console(img, use_color);
    }
    
    stbi_image_free(pixels);
    return 0;
}
