#include "renderer.hpp"
#include "glitch.hpp"
#include "utils.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <charconv>
#include <array>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "gif.h"

// global exit flag is in main - extern to use it here
extern std::atomic<bool> g_exit_requested;
extern void signal_handler(int);

// helper for converstion, int to string
template <typename T>
static void append_int(std::string& out, T val) {
    std::array<char, 32> buf;
    auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), val);
    out.append(buf.data(), ptr - buf.data());
}


// console renderer

std::string render_to_console(const std::vector<Cell>& cells,
                            int blocks_x, int blocks_y, bool use_color) {
    
    // persistent buffer, grows to max size once
    static std::string output;
    static int last_blocks_x = -1;
    static int last_blocks_y = -1;

    // reserve enough space if dim changes
    if (blocks_x != last_blocks_x || blocks_y != last_blocks_y) {
        last_blocks_x = blocks_x;
        last_blocks_y = blocks_y;
        output.reserve(blocks_x * blocks_y * 60); //rough estimate around ~50 for ansi
    }

    output.clear();

    if (use_color) {
        for (int y = 0; y < blocks_y; ++y) {
            for (int x = 0; x < blocks_x; ++x) {
                const Cell& cell = cells[y * blocks_x + x];

                // fg
                output.append("\033[38;2;");
                append_int(output, cell.r);
                output.push_back(';');
                append_int(output, cell.g);
                output.push_back(';');
                append_int(output, cell.b);
                output.push_back('m');

                // bg
                output.append("\033[48;2;");
                append_int(output, cell.br);
                output.push_back(';');
                append_int(output, cell.bg);
                output.push_back(';');
                append_int(output, cell.bb);
                output.push_back('m');

                output.push_back(cell.ch);
                output.append("\033[0m");
            }
            output.push_back('\n');
        }
    } else {
        // monochrome
        for (int y = 0; y < blocks_y; ++y) {
            for (int x = 0; x < blocks_x; ++x) {
                output.push_back(cells[y * blocks_x + x].ch);
            }
            output.push_back('\n');
        }
    }

    return output;
}

// png renderer

void render_to_png(const ProcessedImage& img, const std::string& filename,
                   bool use_color, int cell_w, int cell_h) {
    int image_w = img.blocks_x * cell_w;
    int image_h = img.blocks_y * cell_h;
    std::vector<unsigned char> pixels(image_w * image_h * 3, 0);

    for (int by = 0; by < img.blocks_y; ++by) {
        for (int bx = 0; bx < img.blocks_x; ++bx) {
            const Cell& cell = img.cells[by * img.blocks_x + bx];
            unsigned char fg_r, fg_g, fg_b;
            if (use_color) {
                fg_r = cell.r; fg_g = cell.g; fg_b = cell.b;
            } else {
                unsigned char gray = static_cast<unsigned char>(
                    0.2126f * cell.r + 0.7152f * cell.g + 0.0722f * cell.b
                );
                fg_r = fg_g = fg_b = gray;
            }
            int x0 = bx * cell_w, y0 = by * cell_h;
            for (int dy = 0; dy < cell_h; ++dy) {
                for (int dx = 0; dx < cell_w; ++dx) {
                    int idx = ((y0 + dy) * image_w + (x0 + dx)) * 3;
                    pixels[idx]     = fg_r;
                    pixels[idx + 1] = fg_g;
                    pixels[idx + 2] = fg_b;
                }
            }
        }
    }

    if (!stbi_write_png(filename.c_str(), image_w, image_h, 3, pixels.data(), image_w * 3)) {
        std::cerr << "failed to write png: " << filename << '\n';
    } else {
        std::cout << "png saved: " << filename << '\n';
    }
}

// gif renderer

void render_to_gif(const std::vector<ProcessedImage>& frames,
                   const std::vector<int>& delays,
                   const std::string& filename,
                   bool use_color, int cell_w, int cell_h) {
    if (frames.empty()) return;

    const auto& first = frames[0];
    int image_w = first.blocks_x * cell_w;
    int image_h = first.blocks_y * cell_h;

    // delays are in ms, gif.h needs centiseconds
    int default_delay_cs = (delays.empty() || delays[0] <= 0) ? 10 : delays[0] / 10;
    if (default_delay_cs < 1) default_delay_cs = 1;

    GifWriter writer;
    if (!GifBegin(&writer, filename.c_str(), image_w, image_h, default_delay_cs)) {
        std::cerr << "failed to make gif: " << filename << '\n';
        return;
    }

    std::vector<unsigned char> frame_rgba(image_w * image_h * 4, 0);

    for (size_t f = 0; f < frames.size(); ++f) {
        const auto& img = frames[f];

        // fill frame buffer
        for (int by = 0; by < img.blocks_y; ++by) {
            for (int bx = 0; bx < img.blocks_x; ++bx) {
                const Cell& cell = img.cells[by * img.blocks_x + bx];
                unsigned char r, g, b;
                if (use_color) {
                    r = cell.r; g = cell.g; b = cell.b;
                } else {
                    unsigned char gray = static_cast<unsigned char>(
                        0.2126f * cell.r + 0.7152f * cell.g + 0.0722f * cell.b
                    );
                    r = g = b = gray;
                }
                int x0 = bx * cell_w, y0 = by * cell_h;
                for (int dy = 0; dy < cell_h; ++dy) {
                    for (int dx = 0; dx < cell_w; ++dx) {
                        int idx = ((y0 + dy) * image_w + (x0 + dx)) * 4;
                        frame_rgba[idx] = r;
                        frame_rgba[idx + 1] = g;
                        frame_rgba[idx + 2] = b;
                        frame_rgba[idx + 3] = 255;
                    }
                }
            }
        }

        int delay_cs = (f < delays.size() && delays[f] > 0) ? delays[f] / 10 : default_delay_cs;
        if (delay_cs < 1) delay_cs = 1;
        GifWriteFrame(&writer, frame_rgba.data(), image_w, image_h, delay_cs);
    }

    GifEnd(&writer);
    std::cout << "gif saved: " << filename << '\n';
}

// txt file saver

void save_to_file(const std::string& content, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "failed to write file: " << filename << '\n';
        return;
    }
    file << content;
    file.close();
    std::cout << "saved text: " << filename << '\n';
}

// console playing, static + glitch

void print_with_glitch(const ProcessedImage& base_img, bool use_color, int interval_ms,
                        const GlitchOptions& glitch_opts) {
    std::signal(SIGINT, signal_handler);
    std::cout << "\033[?25l";  // hide cursor

    // allocate working buffer
    std::vector<Cell> working_cells(base_img.cells.size());
    const std::vector<Cell>& base_cells = base_img.cells;

    while (!g_exit_requested) {
        // fast raw copy
        std::memcpy(working_cells.data(), base_cells.data(), base_cells.size() * sizeof(Cell));

        apply_glitch_pipeline(working_cells, base_img.blocks_x, base_img.blocks_y, glitch_opts);

        std::string full_output;
        full_output.reserve(4096);
        full_output.append("\033[2J\033[H");
        full_output.append(render_to_console(working_cells, base_img.blocks_x, base_img.blocks_y, use_color));

        write_console_output(full_output);

        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }

    std::cout << "\033[?25h" << "\033[0m" << std::flush;
}

// gif in console

void animate_gif_in_console(const std::vector<ProcessedImage>& frames,
                            const std::vector<int>& delays,
                            bool use_color, const GlitchOptions& glitch_opts) {
    std::signal(SIGINT, signal_handler);
    std::cout << "\033[?25l";

    // pre allocate buffer, use first frames size
    if (frames.empty()) return;
    std::vector<Cell> working_cells(frames[0].cells.size());

    size_t frame_count = frames.size();
    while (!g_exit_requested) {
        for (size_t i = 0; i < frame_count; ++i) {
            if (g_exit_requested) break;

            const auto& frame = frames[i];
            // copy frame into working buffer
            std::memcpy(working_cells.data(), frame.cells.data(), frame.cells.size() * sizeof(Cell));

            apply_glitch_pipeline(working_cells, frame.blocks_x, frame.blocks_y, glitch_opts);

            std::string full_output;
            full_output.reserve(8192);
            full_output.append("\033[2J\033[H");
            full_output.append(render_to_console(working_cells, frame.blocks_x, frame.blocks_y, use_color));

            write_console_output(full_output);

            int delay_ms = (i < delays.size() && delays[i] > 0) ? delays[i] : DEFAULT_GLITCH_INTERVAL_MS;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

    std::cout << "\033[?25h" << "\033[0m" << std::flush;
}
