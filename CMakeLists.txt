cmake_minimum_required(VERSION 3.16)
project(glyphweave)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# external libs
set(STB_INCLUDE_DIR "C:/devlibs/stb")
set(GIFH_INCLUDE_DIR "C:/devlibs/gif-h")
set(EIGEN_INCLUDE_DIR "C:/devlibs/eigen")

set(PROJECT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(SOURCES
    src/main.cpp
    src/image_loader.cpp
    src/processor.cpp
    src/renderer.cpp
    src/glitch.cpp
    src/refine.cpp
)

set(HEADERS
    include/types.hpp
    include/utils.hpp
    include/image_loader.hpp
    include/processor.hpp
    include/renderer.hpp
    include/glitch.hpp
    include/refine.hpp
)

add_executable(glyphweave ${SOURCES} ${HEADERS})

target_include_directories(glyphweave PRIVATE
    ${PROJECT_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
    ${GIFH_INCLUDE_DIR}
    ${EIGEN_INCLUDE_DIR}
)

# msvc 
if (MSVC) 
    target_compile_options(glyphweave PRIVATE
        $<$<CONFIG:RELEASE>:/O2 /Ob2 /Oi /Ot /Oy /GT /GL /fp:fast>
    )
    target_link_options(glyphweave PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()
