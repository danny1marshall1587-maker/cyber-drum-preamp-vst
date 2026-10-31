cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

if(APPLE)
    project(CyberDrumPreamp VERSION 1.2.0 LANGUAGES CXX C OBJC OBJCXX)
else()
    project(CyberDrumPreamp VERSION 1.2.0 LANGUAGES CXX C)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Fetch JUCE Framework automatically
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

set(PLUGIN_FORMATS VST3 Standalone LV2)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

# 2. Add Plugin Target
juce_add_plugin(CyberDrumPreamp
    COMPANY_NAME "Danny Marshall"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    BUNDLE_ID com.dannymarshall.cyberdrumpreamp
    PLUGIN_MANUFACTURER_CODE DANM
    PLUGIN_CODE CyDr
    FORMATS ${PLUGIN_FORMATS}
    LV2URI "http://cyber-audio.co.uk/plugins/cyber-drum-preamp"
    PRODUCT_NAME "CyberDrumPreamp"
    VERSION "1.2.0"
    AU_DESCRIPTION "Fluid Space Drum Shell & Percussion Preamp"
)

target_compile_definitions(CyberDrumPreamp PRIVATE 
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
    $<$<PLATFORM_ID:Windows>:JUCE_DIRECTSOUND=1>
    $<$<PLATFORM_ID:Windows>:JUCE_WASAPI=1>
    $<$<PLATFORM_ID:Windows>:JUCE_WASAPI_EXCLUSIVE=1>
)

if(UNIX AND NOT APPLE)
    include(FindPkgConfig)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(CyberDrumPreamp PUBLIC ${GTK3_INCLUDE_DIRS})
    target_link_libraries(CyberDrumPreamp PUBLIC ${GTK3_LIBRARIES} dl pthread)
endif()

# 3. Add Source Files
target_sources(CyberDrumPreamp PRIVATE
    Source/CyberDrumEngine.hpp
    Source/CustomDrumLookAndFeel.h
    Source/DrumShellVisualizer.h
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

target_include_directories(CyberDrumPreamp PUBLIC
    Source
)

# 4. Link JUCE Modules
target_link_libraries(CyberDrumPreamp PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_graphics
    juce::juce_core
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# 5. macOS Universal Binary & Performance Optimizations
if(APPLE)
    target_compile_options(CyberDrumPreamp PRIVATE -O3)
    target_link_libraries(CyberDrumPreamp PRIVATE "-framework Accelerate")
    message(STATUS "CyberDrumPreamp configured as Universal Binary (arm64 + x86_64) with Accelerate")
endif()
