# Orus Playground Makefile
# Cross-platform build and packaging for the playground

# Detect platform
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
endif
ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
endif
ifeq ($(findstring NT,$(UNAME_S)),NT)
    PLATFORM := windows
endif

# Directories
PLAYGROUND_DIR := $(shell pwd)
PROJECT_ROOT := $(PLAYGROUND_DIR)/..
BUILD_DIR := $(PLAYGROUND_DIR)/build
DIST_DIR := $(PLAYGROUND_DIR)/dist

# Default target
.PHONY: all
all: build

# Build the main Orus binary if it doesn't exist
.PHONY: build-orus
build-orus:
	@echo "🔨 Building Orus binary..."
	@if [ ! -f "$(PROJECT_ROOT)/orus" ]; then \
		cd "$(PROJECT_ROOT)" && $(MAKE); \
	else \
		echo "✅ Orus binary already exists"; \
	fi

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Build playground
.PHONY: build
build: build-orus $(BUILD_DIR)
	@echo "🚀 Building Orus Playground..."
	@echo "Platform: $(PLATFORM)"
	@cp -r web $(BUILD_DIR)/
	@cp -r examples $(BUILD_DIR)/
	@cp -r scripts $(BUILD_DIR)/
	@cp $(PROJECT_ROOT)/orus $(BUILD_DIR)/ 2>/dev/null || cp $(PROJECT_ROOT)/orus.exe $(BUILD_DIR)/ 2>/dev/null || true
	@echo "✅ Build complete in $(BUILD_DIR)"

# Package for distribution
.PHONY: package
package: build
	@echo "📦 Packaging playground for distribution..."
	@mkdir -p $(DIST_DIR)
	
	# Create platform-specific package
ifeq ($(PLATFORM),windows)
	@cd $(BUILD_DIR) && zip -r ../dist/orus-playground-windows.zip .
	@echo "✅ Windows package: $(DIST_DIR)/orus-playground-windows.zip"
else ifeq ($(PLATFORM),macos)
	@cd $(BUILD_DIR) && tar -czf ../dist/orus-playground-macos.tar.gz .
	@echo "✅ macOS package: $(DIST_DIR)/orus-playground-macos.tar.gz"
else
	@cd $(BUILD_DIR) && tar -czf ../dist/orus-playground-linux.tar.gz .
	@echo "✅ Linux package: $(DIST_DIR)/orus-playground-linux.tar.gz"
endif

# Test the playground
.PHONY: test
test: build
	@echo "🧪 Testing playground functionality..."
	@cd $(BUILD_DIR) && python3 scripts/server.py 8001 &
	@sleep 2
	@curl -s http://localhost:8001 > /dev/null && echo "✅ Server starts correctly" || echo "❌ Server failed to start"
	@pkill -f "python3.*server.py" || true

# Run the playground
.PHONY: run
run: build
	@echo "🌐 Starting Orus Playground..."
	@cd $(BUILD_DIR) && ./scripts/start.sh

# Clean build artifacts
.PHONY: clean
clean:
	@echo "🧹 Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(DIST_DIR)
	@echo "✅ Clean complete"

# Install dependencies (if needed)
.PHONY: deps
deps:
	@echo "📋 Checking dependencies..."
	@command -v python3 >/dev/null 2>&1 || (echo "❌ Python 3 required" && exit 1)
	@echo "✅ All dependencies satisfied"

# Development server (auto-reload)
.PHONY: dev
dev: build-orus
	@echo "🔄 Starting development server..."
	@cd $(PLAYGROUND_DIR) && python3 scripts/server.py 8000

# Help
.PHONY: help
help:
	@echo "Orus Playground Build System"
	@echo "============================"
	@echo ""
	@echo "Available targets:"
	@echo "  build        - Build the playground"
	@echo "  package      - Create distribution package"
	@echo "  test         - Test playground functionality"
	@echo "  run          - Run the playground server"
	@echo "  dev          - Start development server"
	@echo "  clean        - Clean build artifacts"
	@echo "  deps         - Check dependencies"
	@echo "  help         - Show this help"
	@echo ""
	@echo "Platform: $(PLATFORM)"