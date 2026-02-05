cmake_minimum_required(VERSION 3.20)
project(automix VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(AUTOMIX_BUILD_TESTS "Build tests" ON)
option(AUTOMIX_BUILD_EXAMPLES "Build examples" ON)
option(AUTOMIX_BUILD_CLI "Build CLI tools" ON)

# Platform detection
if(APPLE)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(AUTOMIX_PLATFORM "iOS")
    else()
        set(AUTOMIX_PLATFORM "macOS")
    endif()
elseif(WIN32)
    set(AUTOMIX_PLATFORM "Windows")
elseif(ANDROID)
    set(AUTOMIX_PLATFORM "Android")
else()
    set(AUTOMIX_PLATFORM "Linux")
endif()

message(STATUS "Building AutoMix Engine for ${AUTOMIX_PLATFORM}")

# Find dependencies
# FFmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)

# SQLite3
find_package(SQLite3 REQUIRED)

# Essentia (optional - will use simplified analysis if not found)
pkg_check_modules(ESSENTIA essentia)

# Rubber Band (optional - will skip time-stretch if not found)
pkg_check_modules(RUBBERBAND rubberband)

# Source files
set(AUTOMIX_SOURCES
    src/core/types.cpp
    src/core/store.cpp
    src/core/utils.cpp
    src/decoder/decoder.cpp
    src/analyzer/analyzer.cpp
    src/analyzer/bpm_detector.cpp
    src/analyzer/key_detector.cpp
    src/analyzer/energy_analyzer.cpp
    src/matcher/similarity.cpp
    src/matcher/transition_points.cpp
    src/matcher/playlist.cpp
    src/mixer/deck.cpp
    src/mixer/crossfader.cpp
    src/mixer/scheduler.cpp
    src/mixer/engine.cpp
    src/api/automix_api.cpp
)

# Create the library
add_library(automix STATIC ${AUTOMIX_SOURCES})

target_include_directories(automix
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${AVFORMAT_INCLUDE_DIRS}
        ${AVCODEC_INCLUDE_DIRS}
        ${AVUTIL_INCLUDE_DIRS}
        ${SWRESAMPLE_INCLUDE_DIRS}
        ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(automix
    PRIVATE
        ${AVFORMAT_LIBRARIES}
        ${AVCODEC_LIBRARIES}
        ${AVUTIL_LIBRARIES}
        ${SWRESAMPLE_LIBRARIES}
        SQLite::SQLite3
)

target_link_directories(automix
    PRIVATE
        ${AVFORMAT_LIBRARY_DIRS}
        ${AVCODEC_LIBRARY_DIRS}
        ${AVUTIL_LIBRARY_DIRS}
        ${SWRESAMPLE_LIBRARY_DIRS}
)

# Add Essentia if available
if(ESSENTIA_FOUND)
    target_compile_definitions(automix PRIVATE AUTOMIX_HAS_ESSENTIA)
    target_include_directories(automix PRIVATE ${ESSENTIA_INCLUDE_DIRS})
    target_link_libraries(automix PRIVATE ${ESSENTIA_LIBRARIES})
    target_link_directories(automix PRIVATE ${ESSENTIA_LIBRARY_DIRS})
    message(STATUS "Essentia found - full analysis enabled")
else()
    message(STATUS "Essentia not found - using simplified analysis")
endif()

# Add Rubber Band if available
if(RUBBERBAND_FOUND)
    target_compile_definitions(automix PRIVATE AUTOMIX_HAS_RUBBERBAND)
    target_include_directories(automix PRIVATE ${RUBBERBAND_INCLUDE_DIRS})
    target_link_libraries(automix PRIVATE ${RUBBERBAND_LIBRARIES})
    target_link_directories(automix PRIVATE ${RUBBERBAND_LIBRARY_DIRS})
    message(STATUS "Rubber Band found - time-stretch enabled")
else()
    message(STATUS "Rubber Band not found - time-stretch disabled")
endif()

# macOS/iOS specific
if(APPLE)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    
    target_link_libraries(automix
        PRIVATE
            ${COREAUDIO_FRAMEWORK}
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${COREFOUNDATION_FRAMEWORK}
    )
endif()

# CLI tools
if(AUTOMIX_BUILD_CLI)
    # automix-scan
    add_executable(automix-scan src/cli/scan_main.cpp)
    target_link_libraries(automix-scan PRIVATE automix)
    
    # automix-playlist
    add_executable(automix-playlist src/cli/playlist_main.cpp)
    target_link_libraries(automix-playlist PRIVATE automix)
    
    # automix-play
    add_executable(automix-play src/cli/play_main.cpp)
    target_link_libraries(automix-play PRIVATE automix)
endif()

# Examples
if(AUTOMIX_BUILD_EXAMPLES)
    add_executable(decode_test examples/decode_test.cpp)
    target_link_libraries(decode_test PRIVATE automix)
endif()

# Tests
if(AUTOMIX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
install(TARGETS automix
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/automix
    DESTINATION include
)
