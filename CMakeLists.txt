cmake_minimum_required(VERSION 3.22)
project(SwellBass VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Monocypher (BSD-2 / CC0) : vérification Ed25519 des fichiers de licence.
FetchContent_Declare(monocypher GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git GIT_TAG 4.0.3 GIT_SHALLOW TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(monocypher)
add_library(monocypher STATIC "${monocypher_SOURCE_DIR}/src/monocypher.c" "${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c")
target_include_directories(monocypher PUBLIC "${monocypher_SOURCE_DIR}/src" "${monocypher_SOURCE_DIR}/src/optional")
set_target_properties(monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(BASS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BASS_FORMATS AU)
endif()

juce_add_plugin(SwellBass
    COMPANY_NAME "Swell-audio"
    BUNDLE_ID "fr.swell-audio.essential-bass"
    PLUGIN_MANUFACTURER_CODE Swel
    PLUGIN_CODE Sbas
    FORMATS ${BASS_FORMATS}
    PRODUCT_NAME "Swell Essential Bass"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler)

# Banque d'usine : générée par Tools/make-bank.sh à partir de Assets/samples.
file(GLOB BASS_BANK CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/bank/*.flac")
list(FILTER BASS_BANK EXCLUDE REGEX "/\\._")
if(NOT BASS_BANK)
    message(FATAL_ERROR "Banque vide : lance Tools/make-bank.sh")
endif()
juce_add_binary_data(SwellBassAssets SOURCES Assets/swell-mark.png Assets/BebasNeue-Regular.ttf ${BASS_BANK})

target_sources(SwellBass PRIVATE
    Source/Bank.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Licence/SwellLicence.cpp)

target_compile_definitions(SwellBass PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(SwellBass PRIVATE /utf-8)
endif()

target_link_libraries(SwellBass
    PRIVATE
        monocypher
        SwellBassAssets
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

# Outil hors ligne : captures de l'interface et rendus audio (cmake -DBASS_BUILD_TOOLS=ON)
option(BASS_BUILD_TOOLS "Outil hors ligne : captures et rendus audio" OFF)
if(BASS_BUILD_TOOLS)
    juce_add_console_app(BassTool PRODUCT_NAME "BassTool")
    target_sources(BassTool PRIVATE Tools/BassTool.cpp Source/Bank.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Licence/SwellLicence.cpp)
    target_compile_definitions(BassTool PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_VersionString="0.1.2" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(BassTool PRIVATE monocypher SwellBassAssets juce::juce_audio_utils juce::juce_cryptography juce::juce_dsp juce::juce_recommended_config_flags)
endif()
