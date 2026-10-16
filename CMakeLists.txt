# ==============================================================================
#
#  Rackbox Plus
#
#  Rackbox with more to turn. The same four-slot multi-effect - Input ->
#  LPF/HPF -> block 1 -> 2 -> 3 -> 4 -> dry/wet -> output, every block any of
#  fourteen effects in four categories - but each effect gets up to eight
#  knobs instead of two, and the blocks are laid out as four rows. A separate
#  plugin with its own identity, so Rackbox stays as lean as it is.
#
#  The engine started as a copy of Rackbox 0.5.0's Source/Engine; its Effect
#  carries eight knobs, and the effects grow theirs category by category.
#
#  Three build products: AU (aufx/Rbxp/Tzor, the deliverable for Logic), VST3
#  and Standalone (the development build). No AUv3, as in Rackbox.
#
#  Two console targets do the verifying. RackboxPlusCheck measures the engine
#  against signals whose answer is known by arithmetic; EditorShot goes through
#  the whole processor (state round trip, the null test, dry/wet) and renders
#  the panel to a PNG.
#
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

# 1.0.2: a Dark panel, as on Kitbox and Rackbox, switched under the wordmark and kept per user.
project(RackboxPlus VERSION 1.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple Silicon only, matching the other plugin bundles on this machine.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "26.5" CACHE STRING "" FORCE)

# JUCE lives outside the project tree because it is shared with every other
# plugin on this machine.
set(JUCE_PATH "$ENV{HOME}/JUCE" CACHE PATH "Path to the JUCE framework")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Pass -DJUCE_PATH=/path/to/JUCE.")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

# ------------------------------------------------------------------------------
# The plugin
# ------------------------------------------------------------------------------

juce_add_plugin(RackboxPlus
    # The vendor a host lists this under - display only. The identity is the
    # pair of four-character codes below: the AU is aufx/Rbxp/Tzor and the VST3
    # class id is a hash of the same two codes. Changing either after a session
    # has been saved makes a different plugin, and that session loses it.
    COMPANY_NAME                "T'Zorr"
    BUNDLE_ID                   TZorr.RackboxPlus
    PLUGIN_MANUFACTURER_CODE    Tzor
    PLUGIN_CODE                 Rbxp
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "Rackbox Plus"
    DESCRIPTION                 "4-slot multi-effect, extended"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE    # Scripts/install.sh does this, cache flush and all
    VST3_CATEGORIES             Fx
    AU_MAIN_TYPE                kAudioUnitType_Effect
    HARDENED_RUNTIME_ENABLED    TRUE
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

# The engine is listed separately so the verification target links exactly the
# objects the plugin runs. Testing a reimplementation tests nothing.
set(RACKBOX_CORE_SOURCES
    Source/Engine/PlateReverb.cpp
    Source/Engine/EffectCatalog.cpp
    Source/Engine/EffectBlock.cpp)

set(RACKBOX_UI_SOURCES
    Source/UI/Knob.cpp
    Source/UI/BlockPanel.cpp
    Source/UI/ChoiceButtons.cpp
    Source/UI/ChainDisplay.cpp
    Source/UI/PresetButton.cpp)

# The preset file names the plugin by its JucePlugin_* codes, so it builds
# only where juce_add_plugin's definitions are: here, not in RackboxPlusCheck.
target_sources(RackboxPlus PRIVATE
    Source/Engine/PresetFile.cpp
    ${RACKBOX_CORE_SOURCES}
    ${RACKBOX_UI_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(RackboxPlus PRIVATE Source)

target_compile_definitions(RackboxPlus PUBLIC
    TZORR_PRESET_EXTENSION=\".rackboxplus\"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(RackboxPlus PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# RackboxPlusCheck - the engine harness
# ------------------------------------------------------------------------------

juce_add_console_app(RackboxPlusCheck PRODUCT_NAME "RackboxPlusCheck")

target_sources(RackboxPlusCheck PRIVATE
    Verification/RackboxPlusCheck.cpp
    ${RACKBOX_CORE_SOURCES})

target_include_directories(RackboxPlusCheck PRIVATE Source)

target_compile_definitions(RackboxPlusCheck PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(RackboxPlusCheck PRIVATE
    juce::juce_audio_basics
    juce::juce_events
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# EditorShot - the whole processor, and a picture of the panel
# ------------------------------------------------------------------------------

juce_add_console_app(EditorShot PRODUCT_NAME "EditorShot")

target_sources(EditorShot PRIVATE Verification/EditorShot.cpp)

target_compile_definitions(EditorShot PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1)

# Linking the plugin's own shared-code library rather than recompiling its
# sources: that library already carries the JucePlugin_* definitions that
# juce_add_plugin generates.
target_link_libraries(EditorShot PRIVATE
    RackboxPlus
    juce::juce_gui_basics
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_include_directories(EditorShot PRIVATE Source)

# Both sides define JUCE_STANDALONE_APPLICATION and the console app's is the
# one that should win.
target_compile_options(EditorShot PRIVATE -Wno-macro-redefined)
