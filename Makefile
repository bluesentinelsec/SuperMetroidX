# POSIX make wrapper. On Windows, run it from Git Bash.

BUILD_DIR := build
DEBUG_DIR := $(BUILD_DIR)/debug
RELEASE_DIR := $(BUILD_DIR)/release

CMAKE ?= cmake
NINJA ?= ninja
CLANG_FORMAT ?= clang-format
DOXYGEN ?= doxygen

CMAKE_FLAGS := -G Ninja -DCMAKE_MAKE_PROGRAM=$(NINJA)

.DEFAULT_GOAL := build

.PHONY: build release fmt test docs clean package configure-debug configure-release

build: configure-debug
	$(CMAKE) --build $(DEBUG_DIR)

release: configure-release
	$(CMAKE) --build $(RELEASE_DIR)

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
