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
#include "refine.hpp"

// global exit flag
std::atomic<bool> g_exit_requested = false;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        g_exit_requested = true;
    }
}

// helper for parse a single token, eg "row_shift:8"
void parse_glitch_token(const std::string& token, GlitchOptions& opts) {
    // split by colon, optional
    size_t colon = token.find(':');
    std::string name = token.substr(0, colon);
    std::string param = (colon != std::string::npos) ? token.substr(colon + 1) : "";

    // helper to split param by comma and return a vec
    auto split_params = [](std::string str) -> std::vector<std::string> {
        std::vector<std::string> result;
        size_t pos = 0;
        while ((pos = str.find(',')) != std::string::npos) {
            result.push_back(str.substr(0, pos));
            str.erase(0, pos + 1);
        }
        result.push_back(str);
        return result;
    };

    if (name == "row_shift") {
        opts.enable_row_shift = true;
        if (!param.empty()) opts.shift_intensity = std::stoi(param);
    } else if (name == "dropout") {
        opts.enable_dropout = true;
        if (!param.empty()) opts.dropout_rate = std::stof(param);
    } else if (name == "sine_warp") {
        opts.enable_sine_warp = true;
        if (!param.empty()) {
            auto params = split_params(param);
            opts.warp_amplitude = (params.size() > 0) ? std::stof(params[0]) : 2.0f;
            opts.warp_frequency = (params.size() > 1) ? std::stof(params[1]) : 0.1f;
        }
    } else if (name == "rgb_shift") {
        opts.enable_rgb_shift = true;
        if (!param.empty()) opts.rgb_shift_amount = std::stoi(param);
    } else if (name == "jpeg_smash") {
        opts.enable_jpeg_smash = true;
        if (!param.empty()) {
            auto params = split_params(param);
            opts.smash_block_size = (params.size() > 0) ? std::stoi(params[0]) : 8;
            opts.smash_intensity = (params.size() > 1) ? std::stof(params[1]) : 0.3f;
        }
    } else if (name == "data_bend") {
        opts.enable_data_bend = true;
        if (!param.empty()) opts.data_bend_chance = std::stof(param);
    } else if (name == "mirror_slice") {
        opts.enable_mirror_slice = true;
        if (!param.empty()) {
            auto params = split_params(param);
            opts.mirror_slice_width = (params.size() > 0) ? std::stoi(params[0]) : 10;
            if (params.size() > 1) {
                std::string val = params[1];
                std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                opts.mirror_vertical = (val == "true" || val == "1" || val == "yes");
            }
        }
    } else {
        std::cerr << "unknown glitch effect: " << name << '\n';
    }
}

// helper to apply glitch pipeline to processed img
static void apply_glitch_if_enabled(ProcessedImage& img, const GlitchOptions& opts) {
    bool any_glitch = opts.enable_row_shift || opts.enable_dropout ||
                      opts.enable_sine_warp || opts.enable_jpeg_smash ||
                      opts.enable_data_bend || opts.enable_rgb_shift ||
                      opts.enable_mirror_slice;
    if (any_glitch) {
        apply_glitch_pipeline(img.cells, img.blocks_x, img.blocks_y, opts);
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
// --fast : use bayer matrix or default floyd-steinberg
int main(int argc, char *argv[]) {
    atexit(restore_cursor);

    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <filename> [--fast] [-c] [--glitch] [--interval ms] [-o output.txt] [--png] [--gif] [--width]\n";
        return EXIT_FAILURE;
    }

    ProcessingOptions proc_opts;
    proc_opts.use_color = false;
    proc_opts.target_width = 0; // 0 = auto detect console width
    proc_opts.fast_dither = false;

    GlitchOptions glitch_opts;
    int glitch_interval_ms = DEFAULT_GLITCH_INTERVAL_MS;
    std::string image_path;
    std::string output_file, png_file, gif_file;

    RefineOptions refine_opts;
    refine_opts.enabled = false;
    
    // argument parser
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--color") {
            proc_opts.use_color = true;
        } else if (arg == "--fast") {
            proc_opts.fast_dither = true;
        } else if (arg == "--glitch") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                std::string effects = argv[++i];

                // split by ','
                size_t pos = 0;
                while ((pos = effects.find(',')) != std::string::npos) {
                    std::string token = effects.substr(0, pos);
                    effects.erase(0, pos + 1);
                    parse_glitch_token(token, glitch_opts);
                }
                parse_glitch_token(effects, glitch_opts); // last token
            } else {
                std::cerr << "error: --glitch needs a list of effects\n";
                return EXIT_FAILURE;
            }
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
                proc_opts.target_width = *parsed;
            } else {
                std::cerr << "error: --width needs a number\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "--refine") {
            refine_opts.enabled = true;
        } else if (arg == "--refine-iter") {
            if (auto parsed = parse_int(argv[++i]); parsed) {
                refine_opts.max_iterations = *parsed;
            } else { 
                std::cerr << "error: --refine-iter needs a number\n";
                return EXIT_FAILURE; 
            }
        } else if (arg == "--refine-width") {
            if (auto parsed = parse_int(argv[++i]); parsed) {
                refine_opts.width_override = *parsed;
            } else {
                std::cerr << "error: --refine-width needs a number\n";
                return EXIT_FAILURE; 
            }
        } else if (arg == "--diversity") {
            if (auto parsed = parse_float(argv[++i]); parsed) {
                refine_opts.diversity_weight = *parsed;
            } else { 
                std::cerr << "error: --diversity needs a number\n";
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

        bool has_output = !output_file.empty() || !png_file.empty() || !gif_file.empty();
        for (const auto& raw : raw_frames) {
            int width_to_use;
            if (proc_opts.target_width > 0) {
                width_to_use = proc_opts.target_width;
            } else {
                width_to_use = has_output ? 600 : get_terminal_width() - 2;
            }
            width_to_use = std::max(80, width_to_use);

            proc_opts.target_width = width_to_use;
            frames.push_back(process_image(raw.data.data(), raw.w, raw.h, raw.c,
                                           proc_opts));
        }

        // refine frames if enabled and has output
        if (refine_opts.enabled && has_output) {
            std::cout << "refining all frames...........\n";
            for (auto& frame : frames) {
                refine_image(frame, refine_opts, frame.grayscale_data);
            }
        }

        // save as gif
        if (!gif_file.empty()) {
            // if glitch enabled, apply it per frame
            std::vector<ProcessedImage> glitched_frames;
            glitched_frames.reserve(frames.size());
            for (const auto& frame : frames) {
                ProcessedImage copy = frame;
                apply_glitch_if_enabled(copy, glitch_opts);
                glitched_frames.push_back(std::move(copy));
            }
            render_to_gif(glitched_frames, delays, gif_file, proc_opts.use_color);
        }

        // save as png, 1st frame alone
        if (!png_file.empty()) {
            std::cerr << "warning: gif input, png output - saving only first frame.\n";
            ProcessedImage first = frames[0];
            apply_glitch_if_enabled(first, glitch_opts);
            render_to_png(first, png_file, proc_opts.use_color);
        }

        // save as txt, 1st frame alone
        if (!output_file.empty()) {
            std::cerr << "warning: gif input, text output - saving only first frame.\n";
            ProcessedImage first = frames[0];
            apply_glitch_if_enabled(first, glitch_opts);
            std::string text;
            text.reserve(4096);
            render_to_console(first.cells, first.blocks_x, first.blocks_y, proc_opts.use_color, text);
            save_to_file(text, output_file);
        }

        // no output, run in console
        if (output_file.empty() && png_file.empty() && gif_file.empty()) {
            animate_gif_in_console(frames, delays, proc_opts.use_color, glitch_opts);
        }

    } else {
        // static input
        Image img = load_image_file(image_path);
        if (img.w == 0) return EXIT_FAILURE;

        bool has_output = !output_file.empty() || !png_file.empty() || !gif_file.empty();
        int width_to_use;
        if (proc_opts.target_width > 0) {
            width_to_use = proc_opts.target_width;
        } else {
            width_to_use = has_output ? 600 : get_terminal_width() - 2;
        }
        width_to_use = std::max(80, width_to_use);

        proc_opts.target_width = width_to_use;
        ProcessedImage processed = process_image(img.data.data(), img.w, img.h, img.c, proc_opts);

        if (refine_opts.enabled && has_output) {
            std::cout << "refining image.........\n";
            refine_image(processed, refine_opts, processed.grayscale_data);

            std::cout << "First 20 chars after refinement: ";
            for (int i = 0; i < 20 && i < processed.cells.size(); ++i)
                std::cout << processed.cells[i].ch;
            std::cout << "\n";
        }

        // save as png
        if (!png_file.empty()) {
            ProcessedImage copy = processed;
            apply_glitch_if_enabled(copy, glitch_opts);
            render_to_png(copy, png_file, proc_opts.use_color);
        }

        // save as txt
        if (!output_file.empty()) {
            ProcessedImage copy = processed;
            apply_glitch_if_enabled(copy, glitch_opts);
            std::string text;
            text.reserve(4096);
            render_to_console(copy.cells, copy.blocks_x, copy.blocks_y, proc_opts.use_color, text);
            save_to_file(text, output_file);
        }

        // save as gif, single frame anim
        if (!gif_file.empty()) {
            const int NUM_FRAMES = 30;
            std::vector<ProcessedImage> glitch_frames;
            glitch_frames.reserve(NUM_FRAMES);
            std::vector<int> frame_delays(NUM_FRAMES, DEFAULT_GLITCH_INTERVAL_MS);

            for (int i = 0; i < NUM_FRAMES; ++i) {
                ProcessedImage copy = processed;
                apply_glitch_if_enabled(copy, glitch_opts);
                glitch_frames.push_back(std::move(copy));
            }

            render_to_gif(glitch_frames, frame_delays, gif_file, proc_opts.use_color);
        }

        // no output, run in console
        if (output_file.empty() && png_file.empty() && gif_file.empty()) {
            bool any_glitch = glitch_opts.enable_row_shift || glitch_opts.enable_dropout ||
                              glitch_opts.enable_sine_warp || glitch_opts.enable_jpeg_smash ||
                              glitch_opts.enable_data_bend || glitch_opts.enable_rgb_shift ||
                              glitch_opts.enable_mirror_slice;

            if (any_glitch) {
                print_with_glitch(processed, proc_opts.use_color, glitch_interval_ms, glitch_opts);
            } else {
                // nothign just print clean img once
                std::string text;
                text.reserve(4096);
                render_to_console(processed.cells, processed.blocks_x, processed.blocks_y, proc_opts.use_color, text);
                std::cout << text;
            }
        }
    }
    return 0;
}
