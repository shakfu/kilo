.DEFAULT_GOAL := all
.PHONY: all build configure library editor repl all clean test show-config

BUILD_DIR ?= build
CMAKE ?= cmake

all: build

configure:
	@mkdir -p build && $(CMAKE) -S . -B $(BUILD_DIR)

build: configure
	@$(CMAKE) --build $(BUILD_DIR) --config Release

library: configure
	@$(CMAKE) --build $(BUILD_DIR) --target libloki --config Release

loki: editor

editor: configure
	@$(CMAKE) --build $(BUILD_DIR) --target loki-editor --config Release

repl: configure
	@$(CMAKE) --build $(BUILD_DIR) --target loki-repl --config Release

show-config: configure
	@$(CMAKE) --build $(BUILD_DIR) --target show-config --config Release

test: editor repl
	@$(CMAKE) -E chdir $(BUILD_DIR) ctest --output-on-failure

clean:
	@$(CMAKE) -E rm -rf $(BUILD_DIR)
