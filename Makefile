# Convenience wrapper around CMake.
#   make            -> configure (if needed) + build
#   make clean      -> remove build/
#   make publish    -> package caper binary (CMake target)

BUILD_DIR ?= build
BUILD_TYPE ?= Release
CMAKE ?= cmake

.PHONY: all configure build clean publish

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE)

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)

publish: build
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE) --target publish
