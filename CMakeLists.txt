cmake_minimum_required(VERSION 3.25)
if(WIN32)
  project(coreoflight LANGUAGES C CXX RC)
else()
  project(coreoflight LANGUAGES C CXX)
endif()

# Skylanders: Core of Light, the launcher for the recompiled games (docs/roadmap.md, phase 1).
# A small program with Dear ImGui: on Windows Win32 / Direct3D 11, on Linux SDL3 / OpenGL 3. It
# starts each game's own program, so no game depends on it.

# The launcher's version: the problem report names it, tools/package_installer.ps1 builds the
# installer with it, and updates from GitHub Releases (#17) will compare against it.
set(COREOFLIGHT_VERSION "0.4.0-beta.3")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
if(NOT EXISTS ${IMGUI_DIR}/imgui.cpp)
  message(FATAL_ERROR "thirdparty/imgui is missing: git submodule update --init")
endif()

# The figure files (names, elements, stats) from skylanders-portal, without the games' SDK.
set(PORTAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/skylanders-portal)
if(NOT EXISTS ${PORTAL_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "thirdparty/skylanders-portal is missing: git submodule update --init")
endif()
# Its skylanders_alchemy (the games' archive readers, here the LZMA decoder for Imaginators'
# archives) takes stb_image's zlib decoder from thirdparty/stb.
set(SKYLANDERS_STB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb)
if(WIN32)
  # Its skylanders_portal_figures target, with Windows' own AES and MD5.
  add_subdirectory(${PORTAL_DIR} skylanders-portal EXCLUDE_FROM_ALL)
else()
  # skylanders-portal's CMakeLists wants an SDK tree off Windows (for AES and MD5): the same
  # sources here, AES and MD5 from OpenSSL (src/linux/aes.h, the SDK's interface).
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  add_library(skylanders_portal_figures STATIC
    ${PORTAL_DIR}/src/portal/data_folder.cpp
    ${PORTAL_DIR}/src/portal/figure_catalog.cpp
    ${PORTAL_DIR}/src/portal/figure_crypto.cpp
    ${PORTAL_DIR}/src/portal/figure_file.cpp
    ${PORTAL_DIR}/src/portal/figure_info.cpp
    ${PORTAL_DIR}/src/portal/figure_stats.cpp
    ${PORTAL_DIR}/src/portal/trap_villain.cpp
    src/linux/aes_md5_openssl.cpp)
  target_include_directories(skylanders_portal_figures PUBLIC ${PORTAL_DIR}/src)
  target_include_directories(skylanders_portal_figures PRIVATE src/linux)
  target_link_libraries(skylanders_portal_figures PRIVATE OpenSSL::Crypto)
  target_compile_features(skylanders_portal_figures PUBLIC cxx_std_23)
  file(GLOB ALCHEMY_SOURCES ${PORTAL_DIR}/src/alchemy/*.cpp)
  add_library(skylanders_alchemy STATIC ${ALCHEMY_SOURCES})
  target_include_directories(skylanders_alchemy PUBLIC ${PORTAL_DIR}/src)
  target_include_directories(skylanders_alchemy PRIVATE ${SKYLANDERS_STB_DIR})
  target_compile_features(skylanders_alchemy PUBLIC cxx_std_23)
endif()

set(COREOFLIGHT_SOURCES
  src/games.cpp
  src/game_settings.cpp
  src/hub_settings.cpp
  src/disc_import.cpp
  src/disc_search.cpp
  src/file_browser.cpp
  src/figure_import.cpp
  src/figures_ui.cpp
  src/xiso.cpp
  src/settings_ui.cpp
  src/setup_ui.cpp
  src/launcher_ui.cpp
  src/hints.cpp
  src/hub_log.cpp
  src/updates.cpp
  src/nxe_art.cpp
  src/profiles.cpp
  src/profiles_ui.cpp
  src/achievements.cpp
  src/achievements_ui.cpp
  src/problem_report.cpp
  src/saves.cpp
  src/saves_ui.cpp
  src/data_move.cpp
  src/data_transfer.cpp
  src/game_files.cpp
  src/handheld.cpp
  src/gpu_tier.cpp
  src/portable.cpp
  src/language_import.cpp
  src/platform.cpp
  src/desktop_entries.cpp
  src/lang_nl.cpp
  src/unlock_figures.cpp
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
)
if(WIN32)
  add_executable(coreoflight WIN32 ${COREOFLIGHT_SOURCES}
    src/main.cpp
    src/music.cpp
    src/textures.cpp
    src/coreoflight.rc
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
else()
  # Linux: SDL3 for the window, the pads and the music, OpenGL 3 to draw, libcurl for GitHub.
  # COREOFLIGHT_SELF_CONTAINED (the steamdeck preset, built in the Steam Runtime SDK by
  # tools/build_steamdeck.sh): SDL3 built in from its release, the C++ library linked in, and the
  # other libraries the system may lack looked for in lib/ next to the program (package_linux.sh
  # puts them there): one ZIP that runs on SteamOS and other distributions alike.
  option(COREOFLIGHT_SELF_CONTAINED "Linux: SDL3 and libstdc++ built in, other libraries from ./lib" OFF)
  if(COREOFLIGHT_SELF_CONTAINED)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.18
                         GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
  else()
    find_package(SDL3 REQUIRED CONFIG)
  endif()
  find_package(OpenGL REQUIRED)
  find_package(CURL REQUIRED)
  add_executable(coreoflight ${COREOFLIGHT_SOURCES}
    src/main_sdl.cpp
    src/music_sdl.cpp
    src/textures_gl.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
endif()
# thirdparty/stb: stb_image's zlib decoder for Imaginators' archives (language_import.cpp).
target_include_directories(coreoflight PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends thirdparty/stb)
# IMGUI_IMPL_WIN32_DISABLE_GAMEPAD: the launcher reads every XInput pad itself (pad_state.h).
target_compile_definitions(coreoflight PRIVATE COREOFLIGHT_VERSION="${COREOFLIGHT_VERSION}")
target_link_libraries(coreoflight PRIVATE skylanders_portal_figures skylanders_alchemy)
if(WIN32)
  target_compile_definitions(coreoflight PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                                                 IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
  target_link_libraries(coreoflight PRIVATE d3d11 dxgi d3dcompiler bcrypt shell32 ole32 windowscodecs winmm winhttp)
else()
  # The launcher reads every pad itself through SDL's gamepad API (pad_state.h), not ImGui's.
  target_link_libraries(coreoflight PRIVATE OpenGL::GL CURL::libcurl OpenSSL::Crypto)
  if(COREOFLIGHT_SELF_CONTAINED)
    target_link_libraries(coreoflight PRIVATE SDL3::SDL3-static -static-libstdc++ -static-libgcc)
    set_target_properties(coreoflight PROPERTIES BUILD_RPATH "\$ORIGIN/lib" INSTALL_RPATH "\$ORIGIN/lib")
  else()
    target_link_libraries(coreoflight PRIVATE SDL3::SDL3)
  endif()
endif()
# The fallback font next to the exe (main.cpp LoadFont: where Segoe UI isn't there, under Wine and
# on Linux).
add_custom_command(TARGET coreoflight POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/fonts $<TARGET_FILE_DIR:coreoflight>/fonts)
if(MSVC)
  target_compile_options(coreoflight PRIVATE /W3 /utf-8)
endif()
