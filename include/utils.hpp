#pragma once

#include <optional>
#include <string>
#include <algorithm>
#include <cctype>
#include <iostream>
#define NOMINMAX
#include <Windows.h>

inline int get_terminal_width() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
}

inline bool is_gif_file(std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".gif";
}

inline void restore_cursor() {
    std::cout << "\033[?25h" << std::flush; // show cursor
}

inline std::optional<int> parse_int(const char* s) {
    try { return std::stoi(s); }
    catch (...) { return std::nullopt; }
}
