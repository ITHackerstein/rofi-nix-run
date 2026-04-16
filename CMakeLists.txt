cmake_minimum_required(VERSION 4.0)
project(rofi-nix-run C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig)

pkg_search_module(GLIB2 REQUIRED glib-2.0)
pkg_search_module(CAIRO REQUIRED cairo)
pkg_search_module(ROFI REQUIRED rofi)
pkg_search_module(YYJSON REQUIRED yyjson)

add_library(nix-run SHARED
    src/nix-run.c
    src/indexer.c
)

target_include_directories(nix-run PRIVATE
    ${GLIB2_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${ROFI_INCLUDE_DIRS}
    ${YYJSON_INCLUDE_DIRS}
)

target_link_libraries(nix-run PRIVATE
    ${GLIB2_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${ROFI_LIBRARIES}
    ${YYJSON_LIBRARIES}
)

target_compile_options(nix-run PRIVATE -Wall -Wextra -Wformat=2 -Werror)
target_compile_features(nix-run PRIVATE c_std_11)

install(TARGETS nix-run LIBRARY DESTINATION "lib/rofi")
