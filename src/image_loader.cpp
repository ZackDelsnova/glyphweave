#include "image_loader.hpp"
#include <fstream>
#include <vector>
#include <iostream>
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Image load_image_file(const std::string& path) {
    int w, h, channels;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 3);
    if (!pixels) {
        std::cerr << "failed to load image: " << path << '\n';
        return Image{0, 0, 0, {}};
    }

    Image img;
    img.w = w;
    img.h = h;
    img.c = 3;
    img.data.assign(pixels, pixels + w * h * 3);
    stbi_image_free(pixels);
    return img;
}

bool load_gif_file(const std::string& path,
                   std::vector<Image>& out_frames,
                   std::vector<int>& out_delays) {
    // read entire file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "cant open gif: " << path << '\n';
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "failed to read gif file: " << path << '\n';
        return false;
    }

    int width, height, channels;
    int* delay_ptr = nullptr;
    int frame_count = 0;
    unsigned char* pixels = stbi_load_gif_from_memory(
        buffer.data(), buffer.size(), &delay_ptr,
        &width, &height, &frame_count, &channels, 3
    );
    if (!pixels) {
        std::cerr << "failed to decode gif: " << path << '\n';
        return false;
    }

    // frames
    int frame_bytes = width * height * 3;
    out_frames.clear();
    out_frames.reserve(frame_count);
    for (int f = 0; f < frame_count; ++f) {
        Image frame;
        frame.w = width;
        frame.h = height;
        frame.c = 3;
        frame.data.assign(pixels + f * frame_bytes,
                          pixels + (f + 1) * frame_bytes);
        out_frames.push_back(std::move(frame));
    }

    // get delays
    if (delay_ptr) {
        out_delays.assign(delay_ptr, delay_ptr + frame_count);
        free(delay_ptr);
    } else {
        out_delays.assign(frame_count, 100); // default 100ms
    }

    stbi_image_free(pixels);
    return true;
}
