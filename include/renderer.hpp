#pragma once

#include "types.hpp"
#include "glitch.hpp"

// renderer for various outputs

// string with ansi code to print to console
void render_to_console(const std::vector<Cell>& cells, int blocks_x, int blocks_y,
                            bool use_color, std::string& out);

// save as png
void render_to_png(const ProcessedImage& img, const std::string& filename,
                   bool use_color, int cell_w = 4, int cell_h = 6);

// save as animated gif
void render_to_gif(const std::vector<ProcessedImage>& frames,
                   const std::vector<int>& delays,
                   const std::string& filename,
                   bool use_color, int cell_w = 4, int cell_h = 6);

// save as txt (ascii art)
void save_to_file(const std::string& content, const std::string& filename);

// console playing

// display img with glitch effect, looping, need ctrl+c to exit
void print_with_glitch(const ProcessedImage& base_img, bool use_color,
                        int interval_ms, const GlitchOptions& glitch_opts);

// display gif animation, optional glitch effect
void animate_gif_in_console(const std::vector<ProcessedImage>& frames,
                            const std::vector<int>& delays,
                            bool use_color,
                            const GlitchOptions& glitch_opts);
