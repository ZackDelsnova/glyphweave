#pragma once

#include "types.hpp"
#include <vector>
#include <string>

ProcessedImage process_image(const unsigned char* pixels, int w, int h, int channels,
                                const ProcessingOptions& opts);


