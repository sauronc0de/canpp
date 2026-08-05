cmake_minimum_required(VERSION 3.16)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

project(
  Canpp
  VERSION 0.1.2
  DESCRIPTION "Modular communication trace analyzer"
  LANGUAGES C CXX
)

# ── Language configuration ─────────────────────────────────────────────────

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prevent targets from being silently placed in unexpected directories.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ── Project options ────────────────────────────────────────────────────────

option(ENABLE_LOG
  "Enable application logging"
  OFF
)

option(ENABLE_RENDER
  "Build the SDL/OpenGL graphical application"
  ON
)

option(ENABLE_TEST
  "Build automated tests"
  OFF
)

option(ENABLE_DEB_PACKAGE
  "Build the Debian package"
  OFF
)

option(CANPP_BUILD_TRACE_CLI
  "Build the communication trace command-line application"
  ON
)

option(USE_PORTABLE_PATHS
  "Use paths relative to the executable"
  OFF
)

# ── Compiler configuration ─────────────────────────────────────────────────

# Diagnostic coloring is safe to apply globally because it does not alter
# warning policy or third-party compilation behavior.
add_compile_options(
  $<$<COMPILE_LANG_AND_ID:C,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:C,Clang>:-fcolor-diagnostics>
  $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fcolor-diagnostics>
)

# Project targets use this interface library to obtain the common warning
# policy. Third-party targets should not link against it.
add_library(canpp_project_options INTERFACE)
add_library(canpp::project_options ALIAS canpp_project_options)

target_compile_features(
  canpp_project_options
  INTERFACE
    cxx_std_23
)

target_compile_options(
  canpp_project_options
  INTERFACE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:
      -Wall
      -Wextra
      -Werror
      -Wno-empty-body
    >

    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:
      -Wall
      -Wextra
      -Werror
      -Wpedantic
      -Wno-empty-body
    >

    $<$<COMPILE_LANG_AND_ID:C,MSVC>:
      /W4
      /WX
    >

    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:
      /W4
      /WX
      /permissive-
    >
)

# ── Paths ──────────────────────────────────────────────────────────────────

if(NOT DEFINED CANPP_ASSETS_PATH)
  if(USE_PORTABLE_PATHS)
    set(CANPP_ASSETS_PATH "assets")
  else()
    message(STATUS "Canpp: using absolute development asset paths")
    set(CANPP_ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets")
  endif()
endif()

if(NOT DEFINED CANPP_DATA_PATH)
  if(USE_PORTABLE_PATHS)
    set(CANPP_DATA_PATH "data")
  else()
    set(CANPP_DATA_PATH "${CMAKE_SOURCE_DIR}/data")
  endif()
endif()

message(STATUS "Canpp assets path: ${CANPP_ASSETS_PATH}")
message(STATUS "Canpp data path:   ${CANPP_DATA_PATH}")
message(STATUS "Canpp rendering:   ${ENABLE_RENDER}")
message(STATUS "Canpp trace CLI:   ${CANPP_BUILD_TRACE_CLI}")
message(STATUS "Canpp tests:       ${ENABLE_TEST}")

# ── Shared project definitions ─────────────────────────────────────────────

target_compile_definitions(
  canpp_project_options
  INTERFACE
    CANPP_VERSION="${PROJECT_VERSION}"
    ASSETS_PATH="${CANPP_ASSETS_PATH}"
    DATA_PATH="${CANPP_DATA_PATH}"
    USE_PORTABLE_PATHS=$<BOOL:${USE_PORTABLE_PATHS}>

    $<$<CONFIG:Debug>:CANPP_DEBUG>
    $<$<BOOL:${ENABLE_LOG}>:CANPP_ENABLE_LOG>
    $<$<BOOL:${ENABLE_RENDER}>:CANPP_ENABLE_RENDER>
)

# ImGui-specific definitions should ideally be linked only by render targets.
# They remain available through this separate interface target.
add_library(canpp_render_options INTERFACE)
add_library(canpp::render_options ALIAS canpp_render_options)

target_compile_definitions(
  canpp_render_options
  INTERFACE
    IMGUI_USE_WCHAR32
    IMGUI_ENABLE_FREETYPE
)

# ── Dependencies ───────────────────────────────────────────────────────────

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/modules")

# PkgConfig may also be useful to libraries that do not depend on rendering.
find_package(PkgConfig REQUIRED)

# Rendering dependencies must only be required when the render application
# is enabled. This allows the trace CLI and core libraries to build on
# headless systems.
if(ENABLE_RENDER)
  find_package(SDL2 REQUIRED)
  find_package(SDL2_image REQUIRED)
  find_package(OpenGL REQUIRED)
  find_package(GLEW REQUIRED)
endif()

# ── Tests ──────────────────────────────────────────────────────────────────

if(ENABLE_TEST)
  include(CTest)
  enable_testing()
endif()

# ── Project modules ────────────────────────────────────────────────────────
#
# Dependency direction:
#
#   externals
#       ↓
#   libraries
#       ↓
#   core
#       ↓
#   render
#       ↓
#   app
#
# The generic trace library must not depend on CAN, rendering or application
# targets. Protocol implementations depend on the trace library. Core depends
# on libraries. Applications depend on core.

if(ENABLE_RENDER)
  add_subdirectory(externals)
endif()
add_subdirectory(src/libraries)
add_subdirectory(src/core)

if(ENABLE_RENDER)
  add_subdirectory(src/render)
endif()

add_subdirectory(src/app)

if(ENABLE_TEST)
  add_subdirectory(tests)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/examples/CMakeLists.txt")
  add_subdirectory(examples)
endif()

# ── Installation and packaging ─────────────────────────────────────────────

include(GNUInstallDirs)

# The trace CLI is the main application; the graph executable is an addon.
if(TARGET Canpp)
  install(
    TARGETS Canpp
    RUNTIME
      DESTINATION "${CMAKE_INSTALL_BINDIR}"
      COMPONENT canpp
  )
endif()

if(TARGET CanppGraph)
  install(
    TARGETS CanppGraph
    RUNTIME
      DESTINATION "${CMAKE_INSTALL_BINDIR}"
      COMPONENT canpp
  )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
  install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/canpp/assets"
    COMPONENT canpp
  )
endif()

if(
  NOT USE_PORTABLE_PATHS
  AND EXISTS "${CMAKE_SOURCE_DIR}/data"
)
  install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/data/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/canpp/data"
    COMPONENT canpp
  )
endif()

set(CANPP_DOCUMENTATION_FILES)

foreach(documentation_file
  "${CMAKE_SOURCE_DIR}/README.md"
  "${CMAKE_SOURCE_DIR}/readme.md"
  "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES"
)
  if(EXISTS "${documentation_file}")
    list(APPEND CANPP_DOCUMENTATION_FILES "${documentation_file}")
  endif()
endforeach()

if(CANPP_DOCUMENTATION_FILES)
  install(
    FILES ${CANPP_DOCUMENTATION_FILES}
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    COMPONENT canpp
  )
endif()

# ── Desktop integration ────────────────────────────────────────────────────

set(CANPP_DESKTOP_TEMPLATE
  "${CMAKE_SOURCE_DIR}/tools/installer/canpp.desktop.in"
)

if(EXISTS "${CANPP_DESKTOP_TEMPLATE}")
  configure_file(
    "${CANPP_DESKTOP_TEMPLATE}"
    "${CMAKE_BINARY_DIR}/canpp.desktop"
    @ONLY
  )

  if(ENABLE_RENDER)
    install(
      FILES "${CMAKE_BINARY_DIR}/canpp.desktop"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
      COMPONENT canpp
    )
  endif()
endif()

set(CANPP_ICON_FILE
  "${CMAKE_SOURCE_DIR}/assets/icon/canpp.ico"
)

if(
  ENABLE_RENDER
  AND EXISTS "${CANPP_ICON_FILE}"
)
  install(
    FILES "${CANPP_ICON_FILE}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/pixmaps"
    COMPONENT canpp
  )
endif()

# ── Windows installer ──────────────────────────────────────────────────────

if(WIN32 AND ENABLE_RENDER)
  set(CANPP_INNO_TEMPLATE
    "${CMAKE_SOURCE_DIR}/tools/installer/canpp.iss.in"
  )

  if(EXISTS "${CANPP_INNO_TEMPLATE}")
    configure_file(
      "${CANPP_INNO_TEMPLATE}"
      "${CMAKE_BINARY_DIR}/canpp.iss"
      @ONLY
    )
  endif()

  if(TARGET Canpp AND TARGET CanppGraph)
    find_program(
      ISCC_EXE
      NAMES ISCC.exe
      PATHS
        "C:/Program Files (x86)/Inno Setup 6"
    )

    if(ISCC_EXE AND EXISTS "${CANPP_INNO_TEMPLATE}")
      set(CANPP_WINDOWS_STAGE_DIR
        "${CMAKE_SOURCE_DIR}/dist/Canpp"
      )

      set(CANPP_COPY_DLL_SCRIPT
        "${CMAKE_SOURCE_DIR}/tools/tasks/copy_mingw_dlls.sh"
      )

      add_custom_target(
        installer
        COMMAND
          ${CMAKE_COMMAND} -E rm -rf
          "${CANPP_WINDOWS_STAGE_DIR}"

        COMMAND
          ${CMAKE_COMMAND} -E make_directory
          "${CANPP_WINDOWS_STAGE_DIR}"

        COMMAND
          ${CMAKE_COMMAND} -E copy
          "$<TARGET_FILE:Canpp>"
          "${CANPP_WINDOWS_STAGE_DIR}/$<TARGET_FILE_NAME:Canpp>"

        COMMAND
          ${CMAKE_COMMAND} -E copy
          "$<TARGET_FILE:CanppGraph>"
          "${CANPP_WINDOWS_STAGE_DIR}/$<TARGET_FILE_NAME:CanppGraph>"

        COMMAND
          ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets"
          "${CANPP_WINDOWS_STAGE_DIR}/assets"

        COMMAND
          ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES"
          "${CANPP_WINDOWS_STAGE_DIR}/THIRD_PARTY_LICENSES"

        COMMAND
          C:/msys64/usr/bin/bash.exe
          --login
          "${CANPP_COPY_DLL_SCRIPT}"
          "${CMAKE_SOURCE_DIR}"
          "${CANPP_WINDOWS_STAGE_DIR}/$<TARGET_FILE_NAME:Canpp>"

        COMMAND
          C:/msys64/usr/bin/bash.exe
          --login
          "${CANPP_COPY_DLL_SCRIPT}"
          "${CMAKE_SOURCE_DIR}"
          "${CANPP_WINDOWS_STAGE_DIR}/$<TARGET_FILE_NAME:CanppGraph>"

        COMMAND
          "${ISCC_EXE}"
          "${CMAKE_BINARY_DIR}/canpp.iss"

        DEPENDS
          Canpp
          CanppGraph

        WORKING_DIRECTORY
          "${CMAKE_SOURCE_DIR}"

        COMMENT
          "Staging Canpp and building the Windows installer"

        VERBATIM
      )
    else()
      message(
        WARNING
        "Inno Setup was not found. The Canpp executable will be built, "
        "but the installer target will not be available."
      )
    endif()
  endif()
endif()

# ── Debian package ─────────────────────────────────────────────────────────

if(ENABLE_DEB_PACKAGE)
  set(CPACK_GENERATOR "DEB")

  set(CPACK_PACKAGE_NAME "canpp")
  set(CPACK_PACKAGE_VENDOR "sauroncode")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_CONTACT "sauroncode@gmail.com")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Canpp modular communication trace analyzer"
  )

  set(CPACK_OUTPUT_FILE_PREFIX
    "${CMAKE_SOURCE_DIR}/dist/${PROJECT_VERSION}"
  )

  set(CPACK_PACKAGE_FILE_NAME
    "canpp-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}"
  )

  set(CPACK_DEBIAN_PACKAGE_MAINTAINER
    "sauroncode <sauroncode@gmail.com>"
  )

  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")

  if(ENABLE_RENDER)
    set(
      CPACK_DEBIAN_PACKAGE_DEPENDS
      "libsdl2-2.0-0 (>= 2.0.0), "
      "libsdl2-image-2.0-0 (>= 2.0.0), "
      "libglew2.2 | libglew2.1, "
      "libgl1, "
      "libopengl0"
    )
  endif()

  set(CPACK_COMPONENTS_ALL canpp)

  include(CPack)

  add_custom_target(
    deb_package
    COMMAND
      "${CMAKE_CPACK_COMMAND}"
      --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"

    DEPENDS
      $<$<TARGET_EXISTS:Canpp>:Canpp>
      $<$<TARGET_EXISTS:CanppGraph>:CanppGraph>

    WORKING_DIRECTORY
      "${CMAKE_BINARY_DIR}"

    COMMENT
      "Building Canpp Debian package"

    VERBATIM
  )
endif()
