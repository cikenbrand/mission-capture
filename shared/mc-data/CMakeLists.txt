cmake_minimum_required(VERSION 3.28...3.30)

# Mission Capture: the headless data core. Deliberately depends on nothing but
# libobs' portable platform and threading helpers, so it can be unit-tested
# without a window, a device or a render loop -- which is the whole reason
# Phase 3 is specified as headless.
add_library(mc-data STATIC)
add_library(OBS::mc-data ALIAS mc-data)

target_sources(mc-data PRIVATE mc-channel.c include/mc-data/mc-channel.h)

target_include_directories(mc-data PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

target_link_libraries(mc-data PUBLIC OBS::libobs)

set_target_properties(mc-data PROPERTIES FOLDER shared POSITION_INDEPENDENT_CODE ON)
