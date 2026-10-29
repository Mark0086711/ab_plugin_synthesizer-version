cmake_minimum_required(VERSION 3.22)
project(Audio2Bass VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statikus MSVC runtime: a felhasznalo gepen nem kell VC++ Redistributable a plugin betoltesehez
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Audio2Bass
    COMPANY_NAME "AudioToBass"
    PLUGIN_MANUFACTURER_CODE Atob
    PLUGIN_CODE A2Bs
    FORMATS VST3
    PRODUCT_NAME "Audio2Bass"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx)

juce_generate_juce_header(Audio2Bass)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.cpp")
    set(A2B_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.cpp")
    set(A2B_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    message(FATAL_ERROR "PluginProcessor.cpp nem talalhato (se Source/, se a repo gyokerben)")
endif()

target_sources(Audio2Bass PRIVATE
    ${A2B_SRC_DIR}/PluginProcessor.cpp
    ${A2B_SRC_DIR}/PluginProcessor.h
    ${A2B_SRC_DIR}/PluginEditor.cpp
    ${A2B_SRC_DIR}/PluginEditor.h)
target_include_directories(Audio2Bass PRIVATE ${A2B_SRC_DIR})

target_compile_definitions(Audio2Bass PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Audio2Bass
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# ---- Audio2Bass Synth: hanggenerátor (MIDI-vezérelt) változat, ugyanabból a forráskódból ----
juce_add_plugin(Audio2Synth
    COMPANY_NAME "AudioToBass"
    PLUGIN_MANUFACTURER_CODE Atob
    PLUGIN_CODE A2sy
    FORMATS VST3
    PRODUCT_NAME "Audio2Synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

juce_generate_juce_header(Audio2Synth)

target_sources(Audio2Synth PRIVATE
    ${A2B_SRC_DIR}/PluginProcessor.cpp
    ${A2B_SRC_DIR}/PluginProcessor.h
    ${A2B_SRC_DIR}/PluginEditor.cpp
    ${A2B_SRC_DIR}/PluginEditor.h)
target_include_directories(Audio2Synth PRIVATE ${A2B_SRC_DIR})

target_compile_definitions(Audio2Synth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    A2B_SYNTH=1)

target_link_libraries(Audio2Synth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# ---- Opcionalis tesztek (CI: -DA2B_BUILD_TESTS=ON) ----
option(A2B_BUILD_TESTS "Build Audio2Bass test executables" OFF)
if(A2B_BUILD_TESTS)
    set(A2B_TEST_FILES tracker_test.cpp fx_test.cpp gen_test.cpp poly_test.cpp processor_test.cpp synth_test.cpp)
    set(A2B_TEST_DIR "")
    foreach(candidate "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${CMAKE_CURRENT_SOURCE_DIR}")
        set(all_found TRUE)
        foreach(f ${A2B_TEST_FILES})
            if(NOT EXISTS "${candidate}/${f}")
                set(all_found FALSE)
            endif()
        endforeach()
        if(all_found AND A2B_TEST_DIR STREQUAL "")
            set(A2B_TEST_DIR "${candidate}")
        endif()
    endforeach()
    if(A2B_TEST_DIR STREQUAL "")
        message(FATAL_ERROR "A tesztfajlok (tracker_test, fx_test, gen_test, poly_test, processor_test, synth_test .cpp) nem talalhatok a repoban. "
                            "Toltsd fel oket a tests/ mappaba vagy a repo gyokerebe. (A VST3 build tesztek nelkul is mukodik.)")
    endif()

    add_executable(a2b_tracker_test ${A2B_TEST_DIR}/tracker_test.cpp)
    target_include_directories(a2b_tracker_test PRIVATE ${A2B_SRC_DIR})

    add_executable(a2b_fx_test ${A2B_TEST_DIR}/fx_test.cpp)
    target_include_directories(a2b_fx_test PRIVATE ${A2B_SRC_DIR})

    add_executable(a2b_gen_test ${A2B_TEST_DIR}/gen_test.cpp)
    target_include_directories(a2b_gen_test PRIVATE ${A2B_SRC_DIR})

    add_executable(a2b_poly_test ${A2B_TEST_DIR}/poly_test.cpp)
    target_include_directories(a2b_poly_test PRIVATE ${A2B_SRC_DIR})

    juce_add_console_app(Audio2BassProcessorTest PRODUCT_NAME "a2b_processor_test")
    juce_generate_juce_header(Audio2BassProcessorTest)
    target_sources(Audio2BassProcessorTest PRIVATE
        ${A2B_TEST_DIR}/processor_test.cpp
        ${A2B_SRC_DIR}/PluginProcessor.cpp
        ${A2B_SRC_DIR}/PluginEditor.cpp)
    target_include_directories(Audio2BassProcessorTest PRIVATE ${A2B_SRC_DIR})
    target_compile_definitions(Audio2BassProcessorTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(Audio2BassProcessorTest
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)

    juce_add_console_app(Audio2SynthProcessorTest PRODUCT_NAME "a2b_synth_test")
    juce_generate_juce_header(Audio2SynthProcessorTest)
    target_sources(Audio2SynthProcessorTest PRIVATE
        ${A2B_TEST_DIR}/synth_test.cpp
        ${A2B_SRC_DIR}/PluginProcessor.cpp
        ${A2B_SRC_DIR}/PluginEditor.cpp)
    target_include_directories(Audio2SynthProcessorTest PRIVATE ${A2B_SRC_DIR})
    target_compile_definitions(Audio2SynthProcessorTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 A2B_SYNTH=1)
    target_link_libraries(Audio2SynthProcessorTest
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()
