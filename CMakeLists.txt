cmake_minimum_required(VERSION 3.22)

# Must be set before project() so the toolchain picks it up.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary" FORCE)
endif()

project(OP1FX VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Set OP1FX_AAX_SDK to the AAX SDK folder to also build AAX. Signing is a separate step (see docs/AAX.md).
set(OP1FX_AAX_SDK "" CACHE PATH "AAX SDK folder; leave empty to skip AAX")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE)
FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG main
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(JUCE clap-juce-extensions)

set(OP1FX_AAX OFF)
if(OP1FX_AAX_SDK AND EXISTS "${OP1FX_AAX_SDK}/Interfaces/AAX.h")
    juce_set_aax_sdk_path("${OP1FX_AAX_SDK}")
    set(OP1FX_AAX ON)
endif()

include(cmake/op1fx.cmake)

# Every folder under plugins/ with a CMakeLists.txt is one plugin. Set OP1FX_ONLY=<folder name> to build just one,
# which keeps a half-written plugin from breaking the others.
set(OP1FX_ONLY "" CACHE STRING "Build only this folder under plugins/")
file(GLOB plugin_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plugins/*)
foreach(d ${plugin_dirs})
    get_filename_component(plugin_folder ${d} NAME)
    if(EXISTS ${d}/CMakeLists.txt AND (NOT OP1FX_ONLY OR OP1FX_ONLY STREQUAL plugin_folder))
        add_subdirectory(${d})
    endif()
endforeach()
