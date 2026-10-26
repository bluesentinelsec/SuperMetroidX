cmake_minimum_required(VERSION 3.25)

project(
  SuperMetroidX
  VERSION 0.1.0
  DESCRIPTION "Static recompilation of Super Metroid"
  LANGUAGES C
)

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
  add_library(smx_game STATIC ${SMX_GAME_SOURCES})
else()
  add_library(smx_game STATIC src/game_stub.c)
endif()
target_link_libraries(smx_game PUBLIC smx)

add_executable(SuperMetroidX src/main.c)
target_link_libraries(SuperMetroidX PRIVATE smx_game smx)
if(APPLE)
  set_target_properties(
    SuperMetroidX
    PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "SuperMetroidX"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.supermetroidx.app"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
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

add_executable(smx_test_apu tests/test_apu.c)
target_link_libraries(smx_test_apu PRIVATE smx)
add_test(NAME apu COMMAND smx_test_apu)

install(
  TARGETS SuperMetroidX
  BUNDLE DESTINATION .
  RUNTIME DESTINATION .
)

set(CPACK_PACKAGE_NAME "SuperMetroidX")
set(CPACK_PACKAGE_VENDOR "SuperMetroidX")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "SuperMetroidX-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
if(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
elseif(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
