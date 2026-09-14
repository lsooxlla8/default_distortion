cmake_minimum_required(VERSION 3.22)

project(default_distortion VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

set(DEFAULT_DISTORTION_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DEFAULT_DISTORTION_FORMATS AU)
elseif(UNIX)
    list(APPEND DEFAULT_DISTORTION_FORMATS LV2)
endif()

juce_add_plugin(DefaultDistortion
    COMPANY_NAME "icanseesounds"
    BUNDLE_ID "com.icanseesounds.defaultdistortion"
    PLUGIN_MANUFACTURER_CODE Icss
    PLUGIN_CODE Ddst
    FORMATS ${DEFAULT_DISTORTION_FORMATS}
    PRODUCT_NAME "default_distortion"
    DESCRIPTION "Thirty distinct distortion algorithms in a geometric monochrome interface"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    LV2URI "https://github.com/lsooxlla8/default_distortion"
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_add_binary_data(DefaultDistortionFontAssets
    NAMESPACE DefaultDistortionFonts
    HEADER_NAME DefaultDistortionFonts.h
    SOURCES
        Resources/Fonts/JetBrainsMono/JetBrainsMono-Medium.ttf
        Resources/Fonts/JetBrainsMono/JetBrainsMono-ExtraBold.ttf
)

target_sources(DefaultDistortion
    PRIVATE
        Source/DistortionEngine.cpp
        Source/DistortionEngine.h
        Source/MultibandProcessor.cpp
        Source/MultibandProcessor.h
        Source/TransientSplitter.h
        Source/SpectrumFIFO.h
        Source/GlobalBypass.h
        Source/AutoGainTable.h
        Source/SpectralAutoGainTable.h
        Source/SecondaryAutoGainTable.h
        Source/ChowTapeHysteresis.h
        Source/Parameters.h
        Source/UILayout.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        docs/architecture-0.9.md
        docs/compatibility-0.8.md
)

target_compile_definitions(DefaultDistortion
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DefaultDistortion
    PRIVATE
        DefaultDistortionFontAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(APPLE)
    foreach(format_target
            DefaultDistortion_AU
            DefaultDistortion_VST3
            DefaultDistortion_Standalone)
        add_custom_command(
            TARGET ${format_target}
            POST_BUILD
            COMMAND codesign --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:${format_target}>"
            COMMENT "Applying local ad-hoc signature to ${format_target}"
            VERBATIM
        )
    endforeach()
endif()

juce_add_console_app(DefaultDistortionTests
    PRODUCT_NAME "default_distortion DSP Tests"
)

target_sources(DefaultDistortionTests
    PRIVATE
        Source/DistortionEngine.cpp
        Source/DistortionEngine.h
        Source/MultibandProcessor.cpp
        Source/MultibandProcessor.h
        Source/TransientSplitter.h
        Source/SpectrumFIFO.h
        Source/GlobalBypass.h
        Source/AutoGainTable.h
        Source/SpectralAutoGainTable.h
        Source/SecondaryAutoGainTable.h
        Source/ChowTapeHysteresis.h
        Source/Parameters.h
        Source/UILayout.h
        Tests/DspTests.cpp
)

target_compile_definitions(DefaultDistortionTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(DefaultDistortionTests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

enable_testing()
add_test(NAME DefaultDistortionDSP COMMAND DefaultDistortionTests)

add_executable(DefaultDistortionContractTests Tests/PluginContractTests.cpp)

target_compile_definitions(DefaultDistortionContractTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(DefaultDistortionContractTests
    PRIVATE
        DefaultDistortion
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME DefaultDistortionContract COMMAND DefaultDistortionContractTests)

add_executable(DefaultDistortionScreenshot Tests/ScreenshotCapture.cpp)
set_target_properties(DefaultDistortionScreenshot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/DefaultDistortionScreenshot_artefacts/$<CONFIG>"
)

target_compile_definitions(DefaultDistortionScreenshot
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(DefaultDistortionScreenshot
    PRIVATE
        DefaultDistortion
        juce::juce_audio_utils
        juce::juce_graphics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
