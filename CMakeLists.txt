cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# Fallbacks if not provided by buildspec/bootstrap
if(NOT DEFINED _name)
  set(_name "audio-wave")
endif()
if(NOT DEFINED _version)
  set(_version "0.1.0")
endif()

project(${_name} VERSION ${_version} LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options (simple)
# ---------------------------------------------------------------------------
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT           "Use Qt functionality"                      OFF)

include(compilerconfig)
include(defaults)
include(helpers)

# If you want: keep your warning tweaks
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  # Don't treat warnings as errors for this plugin
  add_compile_options(/WX- /utf-8)
endif()

if(APPLE)
  add_compile_options(
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=newline-eof>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=deprecated-this-capture>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=deprecated-declarations>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=range-loop-construct>
  )
endif()

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(AW_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/includes")
set(AW_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(AW_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_library(${CMAKE_PROJECT_NAME} MODULE)

# libobs
find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

# Optional: frontend API
if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API=1)
endif()

# Optional: Qt
if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Core Widgets QUIET)
  if(Qt6_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  else()
    find_package(Qt5 COMPONENTS Core Widgets REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt5::Core Qt5::Widgets)
  endif()

  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
  )
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_QT=1)
endif()

# ---------------------------------------------------------------------------
# config.hpp (PLUGIN_NAME / PLUGIN_VERSION)
# ---------------------------------------------------------------------------
file(MAKE_DIRECTORY "${AW_GEN_DIR}")

configure_file(
  ${AW_INC_DIR}/config.hpp.in
  ${AW_GEN_DIR}/config.hpp
  @ONLY
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ${AW_INC_DIR}
  ${AW_SRC_DIR}
  ${AW_GEN_DIR}
)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(OBS_AUDIO_WAVE_SRC
  ${AW_SRC_DIR}/plugin-main.cpp
  ${AW_SRC_DIR}/audio-wave-render.cpp
  ${AW_SRC_DIR}/audio-wave.cpp
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${OBS_AUDIO_WAVE_SRC})

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  PLUGIN_NAME_STR="${_name}"
  PLUGIN_VERSION_STR="${_version}"
)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED YES
)

# *** IMPORTANT ***
# Let the obs-plugintemplate helpers handle OUTPUT_NAME, bundles,
# and install rules (including macOS .plugin layout).
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES
  OUTPUT_NAME ${_name}
)
