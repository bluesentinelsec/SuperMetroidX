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

add_library(smx STATIC src/present.c src/cart.c src/bus.c src/cpu.c)
target_include_directories(
  smx PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(smx PUBLIC SDL3::SDL3)

add_executable(SuperMetroidX src/main.c)
target_link_libraries(SuperMetroidX PRIVATE smx)
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
