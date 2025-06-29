#!/usr/bin/env python3
"""
Orus Language Build Script
Provides convenient build commands for the Orus language implementation.
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

def run_command(cmd, cwd=None):
    """Run a command and return success status."""
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    return result.returncode == 0

def build_cmake(build_type="Release", clean=False):
    """Build using CMake."""
    build_dir = Path("build")
    
    if clean and build_dir.exists():
        print("Cleaning build directory...")
        subprocess.run(["rm", "-rf", str(build_dir)])
    
    if not build_dir.exists():
        build_dir.mkdir()
    
    # Configure
    cmake_cmd = ["cmake", "..", f"-DCMAKE_BUILD_TYPE={build_type}"]
    if not run_command(cmake_cmd, cwd=build_dir):
        return False
    
    # Build
    make_cmd = ["make", "-j4"]
    return run_command(make_cmd, cwd=build_dir)

def build_make():
    """Build using legacy Makefile."""
    return run_command(["make"])

def run_tests():
    """Run all tests."""
    if Path("build").exists():
        print("Running CMake tests...")
        return run_command(["ctest"], cwd="build")
    else:
        print("Running Makefile tests...")
        return run_command(["make", "test"])

def clean():
    """Clean build artifacts."""
    subprocess.run(["rm", "-rf", "build"])
    subprocess.run(["make", "clean"], stderr=subprocess.DEVNULL)
    print("Build artifacts cleaned.")

def format_code():
    """Format C code using clang-format."""
    c_files = []
    for pattern in ["src/**/*.c", "include/**/*.h", "tests/**/*.c"]:
        c_files.extend(Path(".").glob(pattern))
    
    if not c_files:
        print("No C files found to format.")
        return True
    
    print(f"Formatting {len(c_files)} files...")
    cmd = ["clang-format", "-i"] + [str(f) for f in c_files]
    return run_command(cmd)

def main():
    parser = argparse.ArgumentParser(description="Orus Language Build Tool")
    parser.add_argument("command", choices=["build", "test", "clean", "format"], 
                       help="Command to execute")
    parser.add_argument("--debug", action="store_true", 
                       help="Build in debug mode")
    parser.add_argument("--clean", action="store_true", 
                       help="Clean before building")
    parser.add_argument("--legacy", action="store_true", 
                       help="Use legacy Makefile instead of CMake")
    
    args = parser.parse_args()
    
    if args.command == "build":
        if args.legacy:
            success = build_make()
        else:
            build_type = "Debug" if args.debug else "Release"
            success = build_cmake(build_type, args.clean)
    elif args.command == "test":
        success = run_tests()
    elif args.command == "clean":
        clean()
        success = True
    elif args.command == "format":
        success = format_code()
    
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
