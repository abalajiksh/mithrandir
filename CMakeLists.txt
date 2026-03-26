cmake_minimum_required(VERSION 3.16)
project(mithrandir VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)

# --- Main executable ---
add_executable(mithrandir
    src/main.cpp
    src/types.cpp
    src/cue_parser.cpp
    src/splitter.cpp
    src/tagger.cpp
    src/discid.cpp
)

target_include_directories(mithrandir PRIVATE src)
target_link_libraries(mithrandir PRIVATE
    PkgConfig::TAGLIB
    PkgConfig::OPENSSL
)

install(TARGETS mithrandir DESTINATION bin)
