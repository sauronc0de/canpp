cmake_minimum_required(VERSION 3.16)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

set(ProjectName "Canpp")
project(${ProjectName} VERSION 0.1.2 LANGUAGES C CXX)

# Use the highest C++ standard the toolchain reliably supports.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Global compile options ────────────────────────────────────────────────
# Diagnostic color and a strict warning policy. Per-target flags belong on
# the target. Third-party headers are pulled in via SYSTEM includes below
# so their warnings are ignored at this layer.
add_compile_options(
  $<$<COMPILE_LANG_AND_ID:C,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:C,Clang>:-fcolor-diagnostics>
  $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fcolor-diagnostics>
)

# Treat warnings as errors across the project. CMake's Ninja generator
# mangles semicolon-separated lists inside generator expressions, so
# the three flags are added as three separate options rather than as
# one list.
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wall>)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wextra>)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Werror>)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-empty-body>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wextra>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Werror>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-empty-body>)

# ── Project options ───────────────────────────────────────────────────────
option(ENABLE_LOG        "Enable logging"                 OFF)
option(ENABLE_RENDER     "Enable rendering"               ON)
option(ENABLE_TEST       "Enable tests"                   OFF)
option(ENABLE_DEB_PACKAGE "Build Debian .deb package"     OFF)

# ── Project-wide compile definitions ──────────────────────────────────────
if(NOT CANPP_ASSETS_PATH)
  if(USE_PORTABLE_PATHS)
    set(CANPP_ASSETS_PATH "assets")
  else()
    message(STATUS "Use absolute development paths")
    set(CANPP_ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets")
  endif()
endif()
if(NOT CANPP_DATA_PATH)
  if(USE_PORTABLE_PATHS)
    set(CANPP_DATA_PATH "data")
  else()
    set(CANPP_DATA_PATH "${CMAKE_SOURCE_DIR}/data")
  endif()
endif()

add_compile_definitions(
  ASSETS_PATH="${CANPP_ASSETS_PATH}"
  DATA_PATH="${CANPP_DATA_PATH}"
  IMGUI_USE_WCHAR32
  IMGUI_ENABLE_FREETYPE
  CANPP_VERSION="${PROJECT_VERSION}"
  $<$<CONFIG:Debug>:CANPP_DEBUG_UI>
  $<$<BOOL:${ENABLE_LOG}>:ENABLE_LOG=true>
  $<$<BOOL:${ENABLE_RENDER}>:ENABLE_RENDER=true>
  USE_PORTABLE_PATHS=$<BOOL:${USE_PORTABLE_PATHS}>
)

# ── Find dependencies at the top ──────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/modules")
find_package(SDL2       REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(OpenGL     REQUIRED)
find_package(GLEW       REQUIRED)
find_package(PkgConfig  REQUIRED)

# ── Scoped -Werror suppression for third-party headers ────────────────────
# ImGui/SQLite-style: nuke warnings on FetchContent sources to avoid
# breaking the build when we add `-Werror` later. We suppress in the
# third-party tree only.

# ── Test framework ────────────────────────────────────────────────────────
if(ENABLE_TEST)
  enable_testing()
endif()

# ── Subdirectory order mirrors the dependency direction ───────────────────
# externals  -> libraries  -> core  -> render  -> app
add_subdirectory(externals)
add_subdirectory(src/libraries)
add_subdirectory(src/core)
add_subdirectory(src/render)
add_subdirectory(src/app)
add_subdirectory(examples)

# ── Installer / packaging ─────────────────────────────────────────────────
configure_file(
  ${CMAKE_SOURCE_DIR}/tools/installer/canpp.iss.in
  ${CMAKE_BINARY_DIR}/canpp.iss
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/tools/installer/canpp.desktop.in
  ${CMAKE_BINARY_DIR}/canpp.desktop
  @ONLY
)

if(WIN32)
  find_program(ISCC_EXE
    NAMES ISCC.exe
    PATHS "C:/Program Files (x86)/Inno Setup 6"
    REQUIRED
  )

  add_custom_target(installer ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${CMAKE_SOURCE_DIR}/dist/Canpp"

    COMMAND ${CMAKE_COMMAND} -E copy
      "$<TARGET_FILE:${PROJECT_NAME}>"
      "${CMAKE_SOURCE_DIR}/dist/Canpp/$<TARGET_FILE_NAME:${PROJECT_NAME}>"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/assets"
      "${CMAKE_SOURCE_DIR}/dist/Canpp/assets"

    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES"
      "${CMAKE_SOURCE_DIR}/dist/Canpp/THIRD_PARTY_LICENSES"

    COMMAND C:/msys64/usr/bin/bash.exe --login
      "${CMAKE_SOURCE_DIR}/tools/tasks/copy_mingw_dlls.sh"
      "${CMAKE_SOURCE_DIR}"
      "${CMAKE_SOURCE_DIR}/dist/Canpp/$<TARGET_FILE_NAME:${PROJECT_NAME}>"

    COMMAND "${ISCC_EXE}" "${CMAKE_BINARY_DIR}/canpp.iss"
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Stage files and build Inno Setup installer"
    VERBATIM
  )
elseif(ENABLE_DEB_PACKAGE)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "canpp")

  install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    COMPONENT canpp
  )

  install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
    DESTINATION "share/canpp/assets"
    COMPONENT canpp
  )

  if(NOT USE_PORTABLE_PATHS)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/"
      DESTINATION "share/canpp/data"
      COMPONENT canpp
    )
  endif()

  install(FILES
    "${CMAKE_SOURCE_DIR}/readme.md"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES"
    DESTINATION "share/doc/canpp"
    COMPONENT canpp
  )

  install(FILES "${CMAKE_BINARY_DIR}/canpp.desktop"
    DESTINATION "share/applications"
    COMPONENT canpp
  )

  install(FILES "${CMAKE_SOURCE_DIR}/assets/icon/canpp.ico"
    DESTINATION "share/pixmaps"
    COMPONENT canpp
  )

  set(CPACK_GENERATOR "DEB")
  set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/dist/${PROJECT_VERSION}")
  set(CPACK_PACKAGE_NAME "canpp")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_CONTACT "sauroncode@gmail.com")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "sauroncode <sauroncode@gmail.com>")
  set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Canpp - Markdown note-taking application")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl2-2.0-0 (>= 2.0.0), libsdl2-image-2.0-0 (>= 2.0.0), libglew2.2 | libglew2.1, libgl1, libopengl0")
  set(CPACK_PACKAGE_FILE_NAME "canpp")
  set(CPACK_COMPONENTS_ALL "canpp")
  set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};canpp;/")

  include(CPack)

  add_custom_target(deb_package ALL
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Building Debian package"
    VERBATIM
  )
endif()
