cmake_minimum_required(VERSION 3.16)
project(orus-lang VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Include directories
include_directories(include)

# Core library
file(GLOB_RECURSE CORE_SOURCES "src/core/*.c")
file(GLOB_RECURSE COMPILER_SOURCES "src/compiler/*.c")
file(GLOB_RECURSE VM_SOURCES "src/vm/*.c")

add_library(orus-core STATIC ${CORE_SOURCES})
add_library(orus-compiler STATIC ${COMPILER_SOURCES})
add_library(orus-vm STATIC ${VM_SOURCES})

# Link libraries
target_link_libraries(orus-compiler orus-core)
target_link_libraries(orus-vm orus-core)

# Main interpreter executable
add_executable(orus src/main.c)
target_link_libraries(orus orus-vm orus-compiler orus-core m)

# Test executables
add_executable(test-register tests/test_register.c)
target_link_libraries(test-register orus-vm orus-compiler orus-core m)

add_executable(test-parser tests/test_parser.c)
target_link_libraries(test-parser orus-compiler orus-core m)

# Enable testing
enable_testing()
add_test(NAME RegisterVMTests COMMAND test-register)
add_test(NAME ParserTests COMMAND test-parser)

# Install targets
install(TARGETS orus DESTINATION bin)
install(DIRECTORY include/ DESTINATION include/orus)
install(DIRECTORY docs/ DESTINATION share/doc/orus)
