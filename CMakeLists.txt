cmake_minimum_required(VERSION 3.16)
project(glyphweave)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# include part, stb_image.h is in C:\devlibs\stb
set(STB_INCLUDE_DIR "C:/devlibs/stb")

add_executable(glyphweave 
    src/main.cpp
)

target_include_directories(glyphweave PRIVATE ${STB_INCLUDE_DIR})
