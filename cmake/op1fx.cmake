# Shared build recipe for every OP-1 effect plugin in this repo.
#
#   op1fx_add_plugin(NAME Punch ID punch CODE Pnch CLAP_FEATURES audio-effect transient-shaper stereo)
#   op1fx_add_plugin(NAME Fm ID fm CODE OpFm SYNTH CLAP_FEATURES instrument synthesizer stereo)
#
# A plugin folder holds core/*.c (portable DSP), plugin/*.cpp (JUCE processor and editor) and
# design/art/faceplate-bg-1280x630.png. plugin/Snapshot.cpp, if present, becomes a <NAME>Snapshot dev tool.
function(op1fx_add_plugin)
    cmake_parse_arguments(P "SYNTH" "NAME;ID;CODE;VST3_CATEGORIES" "CLAP_FEATURES" ${ARGN})
    set(dir ${CMAKE_CURRENT_SOURCE_DIR})

    set(formats VST3 Standalone)
    if(APPLE)
        list(APPEND formats AU)
    endif()
    if(OP1FX_AAX)
        list(APPEND formats AAX)
    endif()
    if(P_SYNTH)
        # Instruments: MIDI in, audio out, no audio input bus.
        set(is_synth TRUE)
        set(au_type kAudioUnitType_MusicDevice)
        set(aax_category AAX_ePlugInCategory_SWGenerators)
        if(NOT P_VST3_CATEGORIES)
            set(P_VST3_CATEGORIES Instrument Synth)
        endif()
    else()
        set(is_synth FALSE)
        set(au_type kAudioUnitType_Effect)
        set(aax_category AAX_ePlugInCategory_Effect)
        if(NOT P_VST3_CATEGORIES)
            set(P_VST3_CATEGORIES Fx)
        endif()
    endif()

    juce_add_plugin(${P_NAME}
        COMPANY_NAME "Bombest"
        PLUGIN_MANUFACTURER_CODE Bomb
        PLUGIN_CODE ${P_CODE}
        FORMATS ${formats}
        PRODUCT_NAME "${P_NAME}"
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT ${is_synth}
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES ${P_VST3_CATEGORIES}
        AU_MAIN_TYPE ${au_type}
        AAX_CATEGORY ${aax_category}
        COPY_PLUGIN_AFTER_BUILD FALSE
        BUNDLE_ID audio.bombest.${P_ID}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "${P_NAME} needs microphone access to process audio input in the standalone app.")

    clap_juce_extensions_plugin(TARGET ${P_NAME}
        CLAP_ID "audio.bombest.${P_ID}"
        CLAP_FEATURES ${P_CLAP_FEATURES})

    juce_add_binary_data(${P_NAME}Assets SOURCES ${dir}/design/art/faceplate-bg-1280x630.png)

    file(GLOB core_sources CONFIGURE_DEPENDS ${dir}/core/*.c)
    file(GLOB plugin_sources CONFIGURE_DEPENDS ${dir}/plugin/*.cpp)
    list(FILTER plugin_sources EXCLUDE REGEX "Snapshot\\.cpp$")

    target_sources(${P_NAME} PRIVATE ${core_sources} ${plugin_sources})
    target_include_directories(${P_NAME} PRIVATE ${dir}/core ${dir}/plugin)
    target_compile_definitions(${P_NAME} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${P_NAME}
        PRIVATE ${P_NAME}Assets juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    if(EXISTS ${dir}/plugin/Snapshot.cpp)
        juce_add_console_app(${P_NAME}Snapshot PRODUCT_NAME "${P_NAME}Snapshot")
        set_target_properties(${P_NAME}Snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
        target_sources(${P_NAME}Snapshot PRIVATE ${core_sources} ${plugin_sources} ${dir}/plugin/Snapshot.cpp)
        target_include_directories(${P_NAME}Snapshot PRIVATE ${dir}/core ${dir}/plugin)
        target_compile_definitions(${P_NAME}Snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
        target_link_libraries(${P_NAME}Snapshot PRIVATE ${P_NAME}Assets juce::juce_audio_utils juce::juce_recommended_config_flags)
    endif()
endfunction()
