#pragma once

#include <optional>
#include <string>
#include <algorithm>
#include <cctype>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

inline int get_terminal_width() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
#endif
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

inline std::optional<float> parse_float(const char* s) {
    try { return std::stof(s); }
    catch (...) { return std::nullopt; }
}

// single batch console output - cross platform, didnt try in linux yet
inline void write_console_output(const std::string& text) {
#ifdef _WIN32
    // windows console api - one syscall for whole frame
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    // chk if its console
    if (GetConsoleMode(h, &mode)) {
        DWORD written;
        WriteConsoleA(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    } else {
        DWORD written;
        WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    }
#else
    // posix fallback - buffered fwrite
    fwrite(text.data(), 1, text.size(), stdout);
#endif
}
