# POSIX make wrapper. On Windows, run it from Git Bash.

BUILD_DIR := build
DEBUG_DIR := $(BUILD_DIR)/debug
RELEASE_DIR := $(BUILD_DIR)/release

CMAKE ?= cmake
NINJA ?= ninja
CLANG_FORMAT ?= clang-format
DOXYGEN ?= doxygen
ROM ?= SuperMetroid.sfc
ARGS ?=

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

CMAKE_FLAGS := -G Ninja -DCMAKE_MAKE_PROGRAM=$(NINJA)

.DEFAULT_GOAL := build

.PHONY: build release play run debug fmt test docs clean package configure-debug configure-release

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
	cd $(DEBUG_DIR) && ctest --output-on-failure

docs:
	$(DOXYGEN) Doxyfile

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR) docs/html

package: release
	cd $(RELEASE_DIR) && ctest --output-on-failure
	cd $(RELEASE_DIR) && $(CMAKE) --install . --prefix $(CURDIR)/$(RELEASE_DIR)/install
	cd $(RELEASE_DIR) && cpack
