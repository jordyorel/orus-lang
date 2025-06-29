#!/bin/bash
# Development setup script for Orus language

set -e

echo "Setting up Orus development environment..."

# Check for required tools
check_command() {
    if ! command -v "$1" &> /dev/null; then
        echo "Error: $1 is required but not installed."
        echo "Please install $1 and try again."
        exit 1
    fi
}

echo "Checking required tools..."
check_command gcc
check_command make

# Check for optional tools
if command -v cmake &> /dev/null; then
    echo "✓ CMake found"
    HAS_CMAKE=1
else
    echo "⚠ CMake not found (optional, will use Make)"
    HAS_CMAKE=0
fi

if command -v clang-format &> /dev/null; then
    echo "✓ clang-format found"
    HAS_CLANG_FORMAT=1
else
    echo "⚠ clang-format not found (optional, for code formatting)"
    HAS_CLANG_FORMAT=0
fi

# Build the project
echo
echo "Building Orus..."

if [ $HAS_CMAKE -eq 1 ]; then
    echo "Using CMake build..."
    mkdir -p build
    cd build
    cmake ..
    make -j$(nproc 2>/dev/null || echo 4)
    cd ..
    echo "✓ CMake build completed"
else
    echo "Using Make build..."
    make -j$(nproc 2>/dev/null || echo 4)
    echo "✓ Make build completed"
fi

# Run tests
echo
echo "Running tests..."
if [ $HAS_CMAKE -eq 1 ]; then
    cd build && ctest && cd ..
else
    make test
fi
echo "✓ Tests completed"

# Format code if available
if [ $HAS_CLANG_FORMAT -eq 1 ]; then
    echo
    echo "Formatting code..."
    make format
    echo "✓ Code formatted"
fi

echo
echo "Development environment setup complete!"
echo
echo "Next steps:"
echo "  - Read docs/architecture.md to understand the codebase"
echo "  - Check out examples/ for sample programs"
echo "  - Read docs/contributing.md for contribution guidelines"
echo
if [ $HAS_CMAKE -eq 1 ]; then
    echo "Build commands:"
    echo "  ./build.py build          # Build project"
    echo "  ./build.py test           # Run tests"
    echo "  ./build.py clean          # Clean build"
else
    echo "Build commands:"
    echo "  make                      # Build project"
    echo "  make test                 # Run tests"
    echo "  make clean                # Clean build"
fi
