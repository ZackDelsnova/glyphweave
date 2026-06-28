#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <optional>
#include <cstdlib>
#include <algorithm>

#include "types.hpp"
#include "utils.hpp"
#include "image_loader.hpp"
#include "processor.hpp"
#include "glitch.hpp"
#include "renderer.hpp"

// global exit flag
std::atomic<bool> g_exit_requested = false;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        g_exit_requested = true;
    }
}

// glyphweave <filename> (static image or gif)
// -c / --color : enable colour
// --glitch : enable glitch effect
// --interval ms : glitch interval (ms)
// -o / --output file : save to text file
// --png file : save as PNG
// --gif file : save as GIF
// --width n  : target width (0 = auto detect)
int main(int argc, char *argv[]) {
    atexit(restore_cursor);

    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <filename> [-c] [--glitch] [--interval ms] [-o output.txt] [--width]\n";
        return EXIT_FAILURE;
    }

    int target_width = 0; // 0 = auto detect console width
    bool use_color = false;
    bool use_glitch = false;
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
            if (auto parsed = parse_int(argv[++i]); parsed) {
                glitch_interval_ms = *parsed;
            } else {
                std::cerr << "error: --interval needs a number\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "-o" || arg == "--output") {
            if (++i < argc) output_file = argv[i];
            else { std::cerr << "error: -o needs a filename\n"; return EXIT_FAILURE; }
        } else if (arg == "--png") {
            if (++i < argc) png_file = argv[i];
            else { std::cerr << "error: --png needs a filename\n"; return EXIT_FAILURE; }
        } else if (arg == "--gif") {
            if (++i < argc) gif_file = argv[i];
            else { std::cerr << "error: --gif needs a filename\n"; return EXIT_FAILURE; }
        } else if (arg == "--width") {
            if (auto parsed = parse_int(argv[++i]); parsed) {
                target_width = *parsed;
            } else {
                std::cerr << "error: --width needs a number\n";
                return EXIT_FAILURE;
            }
        } else if (arg[0] == '-') {
            std::cerr << "unknown option: " << arg << '\n';
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

    if (is_gif_file(image_path)) {
        // gif input    
        std::vector<Image> raw_frames;
        std::vector<int> delays;
        if (!load_gif_file(image_path, raw_frames, delays)) {
            return EXIT_FAILURE;
        }

        // process each frame
        std::vector<ProcessedImage> frames;
        frames.reserve(raw_frames.size());

        for (const auto& raw : raw_frames) {
            int width_to_use;
            if (!output_file.empty() || !png_file.empty() || !gif_file.empty())
                width_to_use = (target_width > 0) ? target_width : 600;
            else
                width_to_use = (target_width > 0) ? target_width : get_terminal_width() - 2;
            width_to_use = std::max(80, width_to_use);

            frames.push_back(process_image(raw.data.data(), raw.w, raw.h, raw.c,
                                           use_color, width_to_use));
        }

        // output decision
        if (!gif_file.empty()) {
            render_to_gif(frames, delays, gif_file, use_color);
        } else if (!png_file.empty()) {
            std::cerr << "warning gif input, png output - saving only first frame.\n";
            render_to_png(frames[0], png_file, use_color);
        } else if (!output_file.empty()) {
            std::cerr << "warning gif input, text output - saving only first frame.\n";
            std::string text = render_to_console(frames[0], use_color);
            save_to_file(text, output_file);
        } else {
            animate_gif_in_console(frames, delays, use_color, use_glitch);
        }

    } else {
        // static input
        Image img = load_image_file(image_path);
        if (img.w == 0) return EXIT_FAILURE;

        int width_to_use;
        if (!output_file.empty())
            width_to_use = (target_width > 0) ? target_width : 600;
        else
            width_to_use = (target_width > 0) ? target_width : get_terminal_width() - 2;
        width_to_use = std::max(80, width_to_use);

        ProcessedImage processed = process_image(img.data.data(), img.w, img.h, img.c,
                                                 use_color, width_to_use);

        if (!png_file.empty()) render_to_png(processed, png_file, use_color);
        if (!gif_file.empty()) {
            std::vector<ProcessedImage> single = {processed};
            std::vector<int> single_delay = {100};
            render_to_gif(single, single_delay, gif_file, use_color);
        }
        if (!output_file.empty()) {
            std::string text = render_to_console(processed, use_color);
            save_to_file(text, output_file);
        }
        if (output_file.empty() && png_file.empty() && gif_file.empty()) {
            if (use_glitch)
                print_with_glitch(processed, use_color, glitch_interval_ms);
            else
                std::cout << render_to_console(processed, use_color);
        }
    }
    return 0;
}
