cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel), runs on macOS 10.15+. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

project(Neuroforge VERSION 0.3.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(NF_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
set(NF_TEST_FILTER "" CACHE STRING "Only compile src/tests/test_<filter>*.cpp (empty = all)")

# ---- Pure C++ DSP library (no JUCE) -------------------------------------------------------
# NF_DSP_GLOBS lets a developer compile only part of the DSP tree (e.g. "src/dsp/osc/*.cpp").
set(NF_DSP_GLOBS "src/dsp/*.cpp;src/engine/*.cpp" CACHE STRING "Globs (recursive) of DSP sources to compile")
file(GLOB_RECURSE NF_DSP_SOURCES CONFIGURE_DEPENDS ${NF_DSP_GLOBS})
add_library(nf_dsp STATIC src/dsp/DspStub.cpp ${NF_DSP_SOURCES})
target_include_directories(nf_dsp PUBLIC src)
set_target_properties(nf_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON) # linked into shared plugin modules on Linux
if (MSVC)
    target_compile_options(nf_dsp PRIVATE /W4 /fp:fast /wd4100 /wd4244 /wd4305)
    target_compile_definitions(nf_dsp PUBLIC _USE_MATH_DEFINES NOMINMAX)
endif()

# ---- Unit tests ---------------------------------------------------------------------------
file(GLOB NF_TEST_SOURCES CONFIGURE_DEPENDS src/tests/test_${NF_TEST_FILTER}*.cpp)
add_executable(nf_tests src/tests/TestMain.cpp ${NF_TEST_SOURCES})
target_link_libraries(nf_tests PRIVATE nf_dsp)

# ---- Offline renderer (deterministic preset -> WAV) -------------------------------------------
file(GLOB NF_RENDER_SOURCES CONFIGURE_DEPENDS tools/render/*.cpp)
if (NF_RENDER_SOURCES)
    add_executable(nf_render ${NF_RENDER_SOURCES})
    target_link_libraries(nf_render PRIVATE nf_dsp)
endif()

# ---- Tutorial demo renderer (engine only; see tools/demos/README.md) ----
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/demos/demo.cpp")
    add_executable(nf_demo tools/demos/demo.cpp)
    target_link_libraries(nf_demo PRIVATE nf_dsp)
endif()

# ---- Plugin ---------------------------------------------------------------------------------
if (NF_BUILD_PLUGIN)
    add_subdirectory(libs/JUCE)
    # CLAP support (https://github.com/free-audio/clap-juce-extensions), optional
    set(NF_HAS_CLAP OFF)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/clap-juce-extensions/CMakeLists.txt")
        add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)
        set(NF_HAS_CLAP ON)
    endif()
    set(NF_FORMATS VST3 Standalone) # Windows, macOS and Linux (CLAP is added by clap-juce-extensions)
    if (APPLE)
        list(APPEND NF_FORMATS AU) # Logic Pro / GarageBand
    endif()

    juce_add_plugin(Neuroforge
        COMPANY_NAME "Neuroforge"
        PLUGIN_MANUFACTURER_CODE Nfrg
        PLUGIN_CODE Nfg1
        FORMATS ${NF_FORMATS}
        PRODUCT_NAME "Neuroforge"
        BUNDLE_ID com.huuba91.neuroforge
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Synth)

    file(GLOB_RECURSE NF_APP_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/Plugin*.cpp)
    target_sources(Neuroforge PRIVATE ${NF_APP_SOURCES})
    target_compile_definitions(Neuroforge PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    juce_add_binary_data(NfAssets SOURCES assets/logo.png assets/metal_tile.png
        assets/fonts/RobotoCondensed-Regular.ttf assets/fonts/RobotoCondensed-Medium.ttf
        assets/fonts/RobotoCondensed-SemiBold.ttf assets/fonts/RobotoCondensed-Bold.ttf)
    target_link_libraries(Neuroforge PRIVATE nf_dsp NfAssets
        juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    if (NF_HAS_CLAP)
        clap_juce_extensions_plugin(TARGET Neuroforge
            CLAP_ID "com.huuba91.neuroforge"
            CLAP_FEATURES instrument synthesizer mono "drum and bass" bass)
    endif()
endif()
