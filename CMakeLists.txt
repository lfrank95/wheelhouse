cmake_minimum_required(VERSION 3.22)
project(Wheelhouse VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: either as a subdirectory (JUCE/) or via FetchContent
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ASIO SDK (Windows only): path to the "common" folder of the Steinberg ASIO SDK, e.g. -DASIO_SDK_DIR=C:/asiosdk/common
set(ASIO_SDK_DIR "" CACHE PATH "Folder 'common' of the Steinberg ASIO SDK")

# The build workflow downloads the fonts (Barlow Condensed, Yellowtail) into Resources/fonts; without them fallback fonts
# are used. The small icon is shown in the header of the UI.
file(GLOB WHEELHOUSE_FONTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/*.ttf")
juce_add_binary_data(WheelhouseResources SOURCES Resources/icon/icon-small-256.png ${WHEELHOUSE_FONTS})
set_target_properties(WheelhouseResources PROPERTIES POSITION_INDEPENDENT_CODE ON)   # also linked into the plugin DLL

juce_add_gui_app(Wheelhouse
    PRODUCT_NAME "Wheelhouse"
    COMPANY_NAME "Lorenzo Frank"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon-1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon-small-256.png")

target_sources(Wheelhouse PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Engine.cpp)

target_include_directories(Wheelhouse PRIVATE Source)

target_compile_definitions(Wheelhouse PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Wheelhouse,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Wheelhouse,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1)

if(WIN32 AND ASIO_SDK_DIR)
    message(STATUS "ASIO support enabled: ${ASIO_SDK_DIR}")
    target_compile_definitions(Wheelhouse PRIVATE JUCE_ASIO=1)
    target_include_directories(Wheelhouse PRIVATE "${ASIO_SDK_DIR}")
else()
    message(STATUS "No ASIO SDK given - building without ASIO (WASAPI/DirectSound).")
endif()

target_link_libraries(Wheelhouse PRIVATE
    WheelhouseResources
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(Wheelhouse PRIVATE /O2 /fp:fast /utf-8)   # sources are UTF-8 (UI texts)
endif()

# ---------------- VST3 instrument (same engine and UI as the app) ----------------
juce_add_plugin(WheelhousePlugin
    PRODUCT_NAME "Wheelhouse"
    COMPANY_NAME "Lorenzo Frank"
    PLUGIN_MANUFACTURER_CODE Lfrk
    PLUGIN_CODE Whls
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon-1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon-small-256.png")

target_sources(WheelhousePlugin PRIVATE
    Source/PluginProcessor.cpp
    Source/Engine.cpp)

target_include_directories(WheelhousePlugin PRIVATE Source)

target_compile_definitions(WheelhousePlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(WheelhousePlugin PRIVATE
    WheelhouseResources
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(WheelhousePlugin PRIVATE /O2 /fp:fast /utf-8)
endif()
