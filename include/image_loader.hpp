#pragma once

#include "types.hpp"
#include "glitch.hpp"
#include <vector>
#include <string>

// load a static image (like png and jpg, etc) into an image with 3 RGB channels
// returns empty Image (w=0, h=0, c=0) on failure
Image load_image_file(const std::string& path);

// load a gif file, returns true on success, filling out_frames with RGB Image frames
// and out_delays with per frame delays in ms
bool load_gif_file(const std::string& path,
                   std::vector<Image>& out_frames,
                   std::vector<int>& out_delays);
