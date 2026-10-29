cmake_minimum_required(VERSION 3.25)

# The oldest macOS the app runs on; set before project() so it reaches SDL too.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS the game supports" FORCE)
endif()

project(
  SuperMetroidX
  VERSION 1.1.0
  DESCRIPTION "Static recompilation of Super Metroid"
  HOMEPAGE_URL "https://github.com/bluesentinelsec/SuperMetroidX"
  LANGUAGES C
)
if(WIN32)
  enable_language(RC)
endif()

# What the packages say about the game. Change SMX_RELEASE_DATE with the version.
set(SMX_RELEASE_DATE "2026-10-28")
set(SMX_APP_ID "io.github.bluesentinelsec.SuperMetroidX")
set(SMX_VENDOR "bluesentinelsec")
set(SMX_COPYRIGHT "Copyright © 2026 bluesentinelsec. Licensed under the GNU GPL version 3.")
set(SMX_COPYRIGHT_ASCII "Copyright (C) 2026 bluesentinelsec. Licensed under the GNU GPL version 3.")
set(SMX_LICENSE "GPL-3.0-only")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug or Release" FORCE)
endif()

if(MSVC)
  set(CMAKE_C_FLAGS_DEBUG "/Od /Zi /DDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG")
else()
  set(CMAKE_C_FLAGS_DEBUG "-O0 -g")
  set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.18
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

set(SMX_SOURCES
  src/present.c
  src/cart.c
  src/bus.c
  src/cpu.c
  src/dma.c
  src/ppu.c
  src/apu.c
  src/dsp.c
  src/translate.c
  src/clock.c
  src/sram.c
  src/state.c
  src/save.c
  src/cheat.c
  src/overlay.c
  src/resample.c
  src/sound.c
  src/settings.c
  src/menu.c
  src/skip.c
  src/icon.c
)
add_library(smx STATIC ${SMX_SOURCES})
target_include_directories(
  smx PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(smx PUBLIC SDL3::SDL3)

# The translator runs the ROM in the interpreter to find entry points, so its
# copy of the runtime is optimized even in Debug builds.
add_library(smx_tool STATIC ${SMX_SOURCES})
target_include_directories(smx_tool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(smx_tool PUBLIC SDL3::SDL3)
if(MSVC)
  target_compile_options(smx_tool PRIVATE /O2)
else()
  target_compile_options(smx_tool PRIVATE -O2)
endif()

add_executable(smxrecomp src/recomp_main.c)
target_link_libraries(smxrecomp PRIVATE smx_tool)

# smxembed turns the ROM into C for builds with SMX_EMBED_ROM.
add_executable(smxembed src/embed_main.c)
target_link_libraries(smxembed PRIVATE SDL3::SDL3)

# smxicon draws the game's icon as the .icns, .ico, and PNG files of the packages.
add_executable(smxicon src/icon_main.c src/icon.c)
target_include_directories(smxicon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(smxicon PRIVATE SDL3::SDL3)

# smxsave plays the ROM headless to write play-test saves; see playtest.sh.
add_executable(smxsave src/save_main.c)
target_link_libraries(smxsave PRIVATE smx_tool)

# The C files smxrecomp writes into `dir`.
function(smx_game_sources dir out)
  set(sources "${dir}/smx_game.c")
  foreach(shard RANGE 0 15)
    string(LENGTH "${shard}" digits)
    if(digits EQUAL 1)
      set(shard "0${shard}")
    endif()
    list(APPEND sources "${dir}/smx_game_${shard}.c")
  endforeach()
  set(${out} ${sources} PARENT_SCOPE)
endfunction()

# The translation of the user's ROM is generated under the build tree and
# never committed. Without a ROM the shell links an empty translation.
set(SMX_ROM "${CMAKE_SOURCE_DIR}/SuperMetroid.sfc" CACHE FILEPATH "ROM translated into the shell")
option(SMX_RECOMP_TRACE "Trace the ROM in the interpreter to find more code" ON)
set(SMX_RECOMP_SEEDS "" CACHE FILEPATH "Coverage file from the shell with more entry points")
option(SMX_EMBED_ROM "Link the ROM into the game, so it runs without the ROM file" ON)
if(EXISTS "${SMX_ROM}")
  set(SMX_GAME_DIR "${CMAKE_BINARY_DIR}/game")
  set(SMX_RECOMP_FLAGS "")
  set(SMX_RECOMP_DEPENDS smxrecomp "${SMX_ROM}")
  if(NOT SMX_RECOMP_TRACE)
    list(APPEND SMX_RECOMP_FLAGS "--no-trace")
  endif()
  if(SMX_RECOMP_SEEDS)
    list(APPEND SMX_RECOMP_FLAGS "--seeds" "${SMX_RECOMP_SEEDS}")
    list(APPEND SMX_RECOMP_DEPENDS "${SMX_RECOMP_SEEDS}")
  endif()
  smx_game_sources("${SMX_GAME_DIR}" SMX_GAME_SOURCES)
  add_custom_command(
    OUTPUT ${SMX_GAME_SOURCES}
    COMMAND smxrecomp ${SMX_RECOMP_FLAGS} "${SMX_ROM}" "${SMX_GAME_DIR}"
    DEPENDS ${SMX_RECOMP_DEPENDS}
    COMMENT "Translating ${SMX_ROM}"
    VERBATIM
  )
  if(SMX_EMBED_ROM)
    set(SMX_ROM_SOURCE "${SMX_GAME_DIR}/smx_rom.c")
    add_custom_command(
      OUTPUT "${SMX_ROM_SOURCE}"
      COMMAND smxembed "${SMX_ROM}" "${SMX_ROM_SOURCE}"
      DEPENDS smxembed "${SMX_ROM}"
      COMMENT "Embedding ${SMX_ROM}"
      VERBATIM
    )
  else()
    set(SMX_ROM_SOURCE src/rom_stub.c)
  endif()
  add_library(smx_game STATIC ${SMX_GAME_SOURCES} ${SMX_ROM_SOURCE})
else()
  add_library(smx_game STATIC src/game_stub.c src/rom_stub.c)
endif()
target_link_libraries(smx_game PUBLIC smx)

add_executable(SuperMetroidX src/main.c)
target_link_libraries(SuperMetroidX PRIVATE smx_game smx)
target_compile_definitions(
  SuperMetroidX PRIVATE SMX_VERSION="${PROJECT_VERSION}" SMX_APP_ID="${SMX_APP_ID}"
                        SMX_VENDOR="${SMX_VENDOR}" SMX_HOMEPAGE="${PROJECT_HOMEPAGE_URL}"
)

# The icons and metadata of the packages, generated under the build tree.
set(SMX_PACKAGING_DIR "${CMAKE_BINARY_DIR}/packaging")
configure_file(packaging/README.txt.in "${SMX_PACKAGING_DIR}/README.txt" @ONLY)
if(APPLE)
  set(SMX_ICNS "${SMX_PACKAGING_DIR}/SuperMetroidX.icns")
  add_custom_command(
    OUTPUT "${SMX_ICNS}"
    COMMAND smxicon icns "${SMX_ICNS}"
    DEPENDS smxicon
    VERBATIM
  )
  target_sources(SuperMetroidX PRIVATE "${SMX_ICNS}")
  set_source_files_properties("${SMX_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(
    SuperMetroidX
    PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
      MACOSX_BUNDLE_BUNDLE_NAME "SuperMetroidX"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${SMX_APP_ID}"
      MACOSX_BUNDLE_ICON_FILE "SuperMetroidX.icns"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_COPYRIGHT "${SMX_COPYRIGHT}"
  )
elseif(WIN32)
  set(SMX_ICO "${SMX_PACKAGING_DIR}/SuperMetroidX.ico")
  set(SMX_MANIFEST "${CMAKE_SOURCE_DIR}/packaging/windows/SuperMetroidX.manifest")
  set(SMX_RC "${SMX_PACKAGING_DIR}/SuperMetroidX.rc")
  add_custom_command(
    OUTPUT "${SMX_ICO}"
    COMMAND smxicon ico "${SMX_ICO}"
    DEPENDS smxicon
    VERBATIM
  )
  configure_file(packaging/windows/SuperMetroidX.rc.in "${SMX_RC}" @ONLY)
  target_sources(SuperMetroidX PRIVATE "${SMX_RC}")
  set_source_files_properties("${SMX_RC}" PROPERTIES OBJECT_DEPENDS "${SMX_ICO};${SMX_MANIFEST}")
  # Release builds are windowed programs; Debug keeps a console for the log.
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(SuperMetroidX PROPERTIES WIN32_EXECUTABLE TRUE)
  endif()
  # One .exe with no MinGW runtime DLLs beside it.
  if(MINGW)
    target_link_options(SuperMetroidX PRIVATE -static)
  endif()
else()
  set(SMX_ICON_SIZES 16 24 32 48 64 128 256 512)
  set(SMX_ICON_PNGS "")
  foreach(size IN LISTS SMX_ICON_SIZES)
    set(png "${SMX_PACKAGING_DIR}/icons/${size}x${size}/${SMX_APP_ID}.png")
    add_custom_command(
      OUTPUT "${png}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${SMX_PACKAGING_DIR}/icons/${size}x${size}"
      COMMAND smxicon png ${size} "${png}"
      DEPENDS smxicon
      VERBATIM
    )
    list(APPEND SMX_ICON_PNGS "${png}")
  endforeach()
  add_custom_target(smx_icons ALL DEPENDS ${SMX_ICON_PNGS})
  configure_file(
    packaging/linux/${SMX_APP_ID}.metainfo.xml.in "${SMX_PACKAGING_DIR}/${SMX_APP_ID}.metainfo.xml"
    @ONLY
  )
endif()

enable_testing()
add_executable(smx_tests tests/test_present.c)
target_link_libraries(smx_tests PRIVATE smx)
add_test(NAME present COMMAND smx_tests)

add_executable(smx_test_cart tests/test_cart.c)
target_link_libraries(smx_test_cart PRIVATE smx)
add_test(NAME cart COMMAND smx_test_cart)
set_tests_properties(cart PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(smx_test_bus tests/test_bus.c)
target_link_libraries(smx_test_bus PRIVATE smx)
add_test(NAME bus COMMAND smx_test_bus)
set_tests_properties(bus PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(smx_test_cpu tests/test_cpu.c)
target_link_libraries(smx_test_cpu PRIVATE smx)
add_test(NAME cpu COMMAND smx_test_cpu)
set_tests_properties(cpu PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(smx_test_dma tests/test_dma.c)
target_link_libraries(smx_test_dma PRIVATE smx)
add_test(NAME dma COMMAND smx_test_dma)

add_executable(smx_test_ppu tests/test_ppu.c)
target_link_libraries(smx_test_ppu PRIVATE smx)
add_test(NAME ppu COMMAND smx_test_ppu)

add_executable(smx_test_clock tests/test_clock.c)
target_link_libraries(smx_test_clock PRIVATE smx)
add_test(NAME clock COMMAND smx_test_clock)
set_tests_properties(clock PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(smx_test_dsp tests/test_dsp.c)
target_link_libraries(smx_test_dsp PRIVATE smx)
add_test(NAME dsp COMMAND smx_test_dsp)

# The translator test translates a synthetic ROM, then checks every opcode
# against the interpreter.
set(SMX_TROM_DIR "${CMAKE_BINARY_DIR}/translate_test")
add_executable(smx_test_translate_rom tests/translate_romgen.c)
target_link_libraries(smx_test_translate_rom PRIVATE smx)
target_include_directories(smx_test_translate_rom PRIVATE tests)
add_custom_command(
  OUTPUT "${SMX_TROM_DIR}/test.sfc" "${SMX_TROM_DIR}/seeds.txt"
  COMMAND smx_test_translate_rom "${SMX_TROM_DIR}"
  DEPENDS smx_test_translate_rom
  VERBATIM
)
smx_game_sources("${SMX_TROM_DIR}/game" SMX_TROM_SOURCES)
add_custom_command(
  OUTPUT ${SMX_TROM_SOURCES}
  COMMAND smxrecomp --no-trace --seeds "${SMX_TROM_DIR}/seeds.txt" "${SMX_TROM_DIR}/test.sfc"
          "${SMX_TROM_DIR}/game"
  DEPENDS smxrecomp "${SMX_TROM_DIR}/test.sfc" "${SMX_TROM_DIR}/seeds.txt"
  VERBATIM
)
add_library(smx_test_game STATIC ${SMX_TROM_SOURCES})
target_link_libraries(smx_test_game PUBLIC smx)
add_executable(smx_test_translate tests/test_translate.c)
target_link_libraries(smx_test_translate PRIVATE smx_test_game smx)
target_include_directories(smx_test_translate PRIVATE tests)
add_test(NAME translate COMMAND smx_test_translate)

add_executable(smx_test_save tests/test_save.c)
target_link_libraries(smx_test_save PRIVATE smx)
add_test(NAME save COMMAND smx_test_save)

add_executable(smx_test_state tests/test_state.c)
target_link_libraries(smx_test_state PRIVATE smx)
add_test(NAME state COMMAND smx_test_state)

add_executable(smx_test_sram tests/test_sram.c)
target_link_libraries(smx_test_sram PRIVATE smx)
add_test(NAME sram COMMAND smx_test_sram)

# Tests that need the ROM. They run where one is given, such as on the
# self-hosted runner, and never print or store its bytes.
if(EXISTS "${SMX_ROM}")
  add_executable(smx_test_game_run tests/test_game.c)
  target_link_libraries(smx_test_game_run PRIVATE smx_game smx)
  target_include_directories(smx_test_game_run PRIVATE tests)
  foreach(run idle new-game interpreted)
    add_test(NAME "game_${run}" COMMAND smx_test_game_run ${run} "${SMX_ROM}")
    set_tests_properties("game_${run}" PROPERTIES LABELS rom TIMEOUT 1800)
  endforeach()

  # smxsave reaches play from each section's save station.
  foreach(section ceres landing crateria brinstar norfair wrecked-ship maridia tourian)
    add_test(
      NAME "section_${section}"
      COMMAND smxsave --section ${section} "${SMX_ROM}" "${CMAKE_BINARY_DIR}/test_sections/${section}"
    )
    set_tests_properties("section_${section}" PROPERTIES LABELS rom TIMEOUT 600)
  endforeach()

  # The shell starts on its own ROM, or on the file when the ROM is not built in.
  if(SMX_EMBED_ROM)
    set(SMX_SMOKE_ROM "")
    set(SMX_SMOKE_LOADED "loaded Super Metroid *\\(built in\\)")
  else()
    set(SMX_SMOKE_ROM "${SMX_ROM}")
    set(SMX_SMOKE_LOADED "loaded Super Metroid")
  endif()
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test_smoke")
  add_test(NAME smoke_rom COMMAND SuperMetroidX --smoke ${SMX_SMOKE_ROM})
  set_tests_properties(
    smoke_rom
    PROPERTIES
      LABELS rom
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test_smoke"
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SMX_DATA_DIR=${CMAKE_BINARY_DIR}/test_smoke"
      PASS_REGULAR_EXPRESSION "${SMX_SMOKE_LOADED}"
      FAIL_REGULAR_EXPRESSION "translated, [1-9][0-9]* interpreted"
  )
endif()

add_executable(smx_test_cheat tests/test_cheat.c)
target_link_libraries(smx_test_cheat PRIVATE smx_game smx)
add_test(NAME cheat COMMAND smx_test_cheat)
if(EXISTS "${SMX_ROM}")
  add_test(NAME cheat_rom COMMAND smx_test_cheat "${SMX_ROM}")
  set_tests_properties(cheat_rom PROPERTIES LABELS rom TIMEOUT 600)
endif()

add_executable(smx_test_resample tests/test_resample.c)
target_link_libraries(smx_test_resample PRIVATE smx)
add_test(NAME resample COMMAND smx_test_resample)

add_executable(smx_test_settings tests/test_settings.c)
target_link_libraries(smx_test_settings PRIVATE smx)
add_test(NAME settings COMMAND smx_test_settings)

add_executable(smx_test_menu tests/test_menu.c)
target_link_libraries(smx_test_menu PRIVATE smx)
add_test(NAME menu COMMAND smx_test_menu)

add_executable(smx_test_skip tests/test_skip.c)
target_link_libraries(smx_test_skip PRIVATE smx)
add_test(NAME skip COMMAND smx_test_skip)

add_executable(smx_test_apu tests/test_apu.c)
target_link_libraries(smx_test_apu PRIVATE smx)
add_test(NAME apu COMMAND smx_test_apu)

add_executable(smx_test_icon tests/test_icon.c)
target_link_libraries(smx_test_icon PRIVATE smx)
add_test(NAME icon COMMAND smx_test_icon)

# `make package` installs into CPack's staging folder and archives it: a
# disk image on macOS, a zip on Windows, and a tarball in the usual Linux
# layout (bin/ and share/) on Linux. Each holds the GPL beside the game.
if(APPLE)
  install(TARGETS SuperMetroidX BUNDLE DESTINATION .)
  install(FILES "${SMX_PACKAGING_DIR}/README.txt" DESTINATION .)
  install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
  # Sign the finished bundle ad hoc, so its resources are sealed with the
  # (possibly stripped) executable. This is not a Developer ID signature.
  install(
    CODE [[execute_process(
      COMMAND codesign --force --sign - "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/SuperMetroidX.app"
      COMMAND_ERROR_IS_FATAL ANY
    )]]
  )
elseif(WIN32)
  install(TARGETS SuperMetroidX RUNTIME DESTINATION .)
  install(FILES "${SMX_PACKAGING_DIR}/README.txt" DESTINATION .)
  install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
else()
  include(GNUInstallDirs)
  install(TARGETS SuperMetroidX RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/linux/${SMX_APP_ID}.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
  )
  install(FILES "${SMX_PACKAGING_DIR}/${SMX_APP_ID}.metainfo.xml"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
  )
  foreach(size IN LISTS SMX_ICON_SIZES)
    install(FILES "${SMX_PACKAGING_DIR}/icons/${size}x${size}/${SMX_APP_ID}.png"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
    )
  endforeach()
  install(FILES "${SMX_PACKAGING_DIR}/README.txt" DESTINATION ${CMAKE_INSTALL_DOCDIR})
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/SuperMetroidX)
endif()

if(APPLE)
  set(SMX_PACKAGE_SYSTEM "macOS")
  if(CMAKE_OSX_ARCHITECTURES)
    string(REPLACE ";" "-" SMX_PACKAGE_ARCH "${CMAKE_OSX_ARCHITECTURES}")
  else()
    set(SMX_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
  endif()
else()
  set(SMX_PACKAGE_SYSTEM "${CMAKE_SYSTEM_NAME}")
  set(SMX_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()
string(TOLOWER "${SMX_PACKAGE_ARCH}" SMX_PACKAGE_ARCH)
if(SMX_PACKAGE_ARCH STREQUAL "amd64")
  set(SMX_PACKAGE_ARCH "x86_64")
elseif(SMX_PACKAGE_ARCH STREQUAL "aarch64")
  set(SMX_PACKAGE_ARCH "arm64")
endif()

set(CPACK_PACKAGE_NAME "SuperMetroidX")
set(CPACK_PACKAGE_VENDOR "${SMX_VENDOR}")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME
    "SuperMetroidX-${PROJECT_VERSION}-${SMX_PACKAGE_SYSTEM}-${SMX_PACKAGE_ARCH}"
)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CPACK_STRIP_FILES ON)
if(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_DMG_VOLUME_NAME "SuperMetroidX ${PROJECT_VERSION}")
  set(CPACK_PACKAGE_ICON "${SMX_ICNS}")
elseif(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
