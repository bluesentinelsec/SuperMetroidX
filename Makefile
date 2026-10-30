# POSIX make wrapper. On Windows, run it from Git Bash.

BUILD_DIR := build
DEBUG_DIR := $(BUILD_DIR)/debug
RELEASE_DIR := $(BUILD_DIR)/release
XCODE_DIR := $(BUILD_DIR)/xcode
IOS_DIR := $(BUILD_DIR)/ios

CMAKE ?= cmake
NINJA ?= ninja
CLANG_FORMAT ?= clang-format
DOXYGEN ?= doxygen
ROM ?= SuperMetroid.sfc
ARGS ?=
# Extra CMake cache settings, such as -DSMX_EMBED_ROM=OFF, and extra ctest flags, such as -LE rom.
CMAKE_ARGS ?=
CTEST_ARGS ?=
# The Apple developer team that signs the Xcode project's app, when not the keychain's; kept once given.
TEAM ?=

ifeq ($(OS),Windows_NT)
  BIN := SuperMetroidX.exe
else ifeq ($(shell uname -s),Darwin)
  BIN := SuperMetroidX.app/Contents/MacOS/SuperMetroidX
  DEBUGGER ?= lldb
else
  BIN := SuperMetroidX
  DEBUGGER ?= $(if $(shell command -v gdb),gdb,lldb)
endif
DEBUG_BIN := $(DEBUG_DIR)/$(BIN)
RELEASE_BIN := $(RELEASE_DIR)/$(BIN)

CMAKE_FLAGS := -G Ninja -DCMAKE_MAKE_PROGRAM=$(NINJA) $(CMAKE_ARGS)

.DEFAULT_GOAL := build

.PHONY: build release play run debug fmt test test-release docs clean package dist xcode ios configure-debug configure-release

build: configure-debug
	$(CMAKE) --build $(DEBUG_DIR)

release: configure-release
	$(CMAKE) --build $(RELEASE_DIR)

play: release
	$(RELEASE_BIN) $(ARGS) $(ROM)

run: build
	$(DEBUG_BIN) $(ARGS) $(ROM)

# Stops at a crash with every thread's backtrace and stays at the prompt.
debug: build
ifeq ($(DEBUGGER),)
	@echo "make debug needs lldb or gdb on macOS or Linux" >&2; exit 1
else ifeq ($(notdir $(DEBUGGER)),gdb)
	$(DEBUGGER) -q -ex 'set pagination off' -ex run -ex 'thread apply all bt' --args $(DEBUG_BIN) $(ARGS) $(ROM)
else
	$(DEBUGGER) --batch -o run -k 'thread backtrace all' -- $(DEBUG_BIN) $(ARGS) $(ROM)
endif

configure-debug:
	$(CMAKE) -S . -B $(DEBUG_DIR) $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Debug

configure-release:
	$(CMAKE) -S . -B $(RELEASE_DIR) $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release

fmt:
	$(CLANG_FORMAT) -i $(shell git ls-files '*.c' '*.h')

test: build
	$(CMAKE) --build $(DEBUG_DIR) --target smx_tests
	cd $(DEBUG_DIR) && ctest --output-on-failure --parallel 4 $(CTEST_ARGS)

test-release: release
	cd $(RELEASE_DIR) && ctest --output-on-failure --parallel 4 $(CTEST_ARGS)

docs:
	$(DOXYGEN) Doxyfile

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR) docs/html

# A disk image (macOS), zip (Windows), or tarball (Linux) in build/release/dist.
package: release
	cd $(RELEASE_DIR) && ctest --output-on-failure $(CTEST_ARGS)
	cd $(RELEASE_DIR) && cpack
	@echo "Package in $(RELEASE_DIR)/dist:"
	@ls $(RELEASE_DIR)/dist | grep -v '^_CPack_Packages$$'

dist: package

# An Xcode project in build/xcode, opened in Xcode, that builds, runs, debugs, signs, and archives the app.
xcode:
ifneq ($(shell uname -s),Darwin)
	@echo "make xcode needs macOS and Xcode" >&2; exit 1
else
	$(CMAKE) -S . -B $(XCODE_DIR) -G Xcode $(CMAKE_ARGS) $(if $(TEAM),-DSMX_APPLE_TEAM=$(TEAM))
	open $(XCODE_DIR)/SuperMetroidX.xcodeproj
endif

# An Xcode project for iPhone and iPad in build/ios, opened in Xcode. The translator and
# the icon tool run on this Mac, so the Release build makes them first.
ios:
ifneq ($(shell uname -s),Darwin)
	@echo "make ios needs macOS and Xcode" >&2; exit 1
else
	$(MAKE) configure-release
	$(CMAKE) --build $(RELEASE_DIR) --target smxrecomp smxembed smxicon
	$(CMAKE) -S . -B $(IOS_DIR) -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DSMX_HOST_TOOLS=$(abspath $(RELEASE_DIR)) $(CMAKE_ARGS) $(if $(TEAM),-DSMX_APPLE_TEAM=$(TEAM))
ifneq ($(OPEN),0)
	open $(IOS_DIR)/SuperMetroidX.xcodeproj
endif
endif
