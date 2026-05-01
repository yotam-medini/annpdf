cmake_minimum_required(VERSION 3.10)
project(annpdf VERSION 1.0)

# For LSP
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find Boost (Program Options)
find_package(Boost REQUIRED COMPONENTS program_options)

# 2. Find PkgConfig (used to locate HarfBuzz and Cairo easily)
find_package(PkgConfig REQUIRED)

# 3. Find HarfBuzz
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)

# 4. Find Cairo
pkg_check_modules(CAIRO REQUIRED cairo)

# 5. Find FreeType (HarfBuzz often needs this for font loading)
find_package(Freetype REQUIRED)

pkg_check_modules(POPPLER REQUIRED poppler-glib)

# Define the executable
add_executable(annpdf annpdf.cpp)

# 6. Include Directories
target_include_directories(annpdf PRIVATE
    ${POPPLER_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${HARFBUZZ_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
)

# 7. Link Libraries
target_link_libraries(annpdf PRIVATE
    ${POPPLER_LIBRARIES}
    ${Boost_LIBRARIES}
    ${HARFBUZZ_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${FREETYPE_LIBRARIES}
)

# Optional: Add compiler warnings for easier debugging
target_compile_options(annpdf PRIVATE -Wall -Wextra)
