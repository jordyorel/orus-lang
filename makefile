CC = gcc

# Build Profile Configuration
# Usage: make PROFILE=debug|release|profiling|ci
# Default is debug for development
PROFILE ?= debug

# Architecture Detection
UNAME_M := $(shell uname -m)
UNAME_S := $(shell uname -s)

# Base compiler flags
BASE_CFLAGS = -Wall -Wextra -std=c11

# Architecture-specific optimizations
ifeq ($(UNAME_M),arm64)
    # Apple Silicon (M1/M2/M3) optimizations
    ARCH_FLAGS = -mcpu=apple-m1 -mtune=apple-m1 -march=armv8.4-a+simd+crypto+sha3
    ARCH_DEFINES = -DUSE_COMPUTED_GOTO=1
else ifeq ($(UNAME_M),x86_64)
    # Intel/AMD x86_64 optimizations
    ARCH_FLAGS = -march=native -mtune=native
    ARCH_DEFINES = -DUSE_COMPUTED_GOTO=1
else
    # Generic fallback
    ARCH_FLAGS = 
    ARCH_DEFINES = 
endif

# Profile-specific configurations
ifeq ($(PROFILE),debug)
    # Debug build: maximum debugging info, no optimization, all checks enabled
    OPT_FLAGS = -O0 -g3 -DDEBUG=1
    DEBUG_FLAGS = -fno-omit-frame-pointer -fstack-protector-strong
    DEFINES = $(ARCH_DEFINES) -DDEBUG_MODE=1 -DENABLE_ASSERTIONS=1
    SUFFIX = _debug
    PROFILE_DESC = Debug (no optimization, full debugging)
else ifeq ($(PROFILE),release)
    # Release build: maximum optimization, minimal debug info
    OPT_FLAGS = -O3 -g1 -DNDEBUG=1
    FAST_FLAGS = -funroll-loops -finline-functions
    DEFINES = $(ARCH_DEFINES) -DRELEASE_MODE=1 -DENABLE_OPTIMIZATIONS=1
    SUFFIX = 
    PROFILE_DESC = Release (maximum optimization)
else ifeq ($(PROFILE),profiling)
    # Profiling build: optimization + profiling instrumentation
    OPT_FLAGS = -O2 -g2 -pg
    PROFILE_FLAGS = -fno-omit-frame-pointer
    DEFINES = $(ARCH_DEFINES) -DPROFILING_MODE=1 -DENABLE_PROFILING=1
    SUFFIX = _profiling
    PROFILE_DESC = Profiling (optimization + instrumentation)
else ifeq ($(PROFILE),ci)
    # CI build: treat warnings as errors for strict CI enforcement
    OPT_FLAGS = -O2 -g1
    DEBUG_FLAGS = -fno-omit-frame-pointer -fstack-protector-strong
    PROFILE_FLAGS = -Werror
    DEFINES = $(ARCH_DEFINES) -DCI_MODE=1
    SUFFIX = _ci
    PROFILE_DESC = CI (warnings as errors, optimized)
else
    $(error Invalid PROFILE: $(PROFILE). Use debug, release, profiling, or ci)
endif

# Assemble final CFLAGS
CFLAGS = $(BASE_CFLAGS) $(ARCH_FLAGS) $(OPT_FLAGS) $(DEBUG_FLAGS) $(FAST_FLAGS) $(PROFILE_FLAGS) $(DEFINES)

LDFLAGS = -lm

# Add profiling link flags if needed
ifeq ($(PROFILE),profiling)
    LDFLAGS += -pg
endif

# Directories
SRCDIR = src
INCDIR = include
TESTDIR = tests
BUILDDIR = build/$(PROFILE)

# Include path
INCLUDES = -I$(INCDIR)

# Source files
# Compiler frontend (language processing)
COMPILER_FRONTEND_SRCS = $(SRCDIR)/compiler/frontend/lexer.c $(SRCDIR)/compiler/frontend/parser.c

# Keep multipass compiler and minimal dependencies 
COMPILER_BACKEND_SRCS = $(SRCDIR)/compiler/backend/typed_ast_visualizer.c $(SRCDIR)/compiler/backend/register_allocator.c $(SRCDIR)/compiler/backend/compiler.c $(SRCDIR)/compiler/backend/optimization/optimizer.c $(SRCDIR)/compiler/backend/optimization/constantfold.c $(SRCDIR)/compiler/backend/codegen/codegen.c $(SRCDIR)/compiler/backend/codegen/peephole.c $(SRCDIR)/compiler/symbol_table.c

# Combined simplified compiler sources  
COMPILER_SRCS = $(COMPILER_FRONTEND_SRCS) $(COMPILER_BACKEND_SRCS) $(SRCDIR)/compiler/typed_ast.c
VM_SRCS = $(SRCDIR)/vm/core/vm_core.c $(SRCDIR)/vm/runtime/vm.c $(SRCDIR)/vm/core/vm_memory.c $(SRCDIR)/vm/utils/debug.c $(SRCDIR)/vm/runtime/builtins.c $(SRCDIR)/vm/operations/vm_arithmetic.c $(SRCDIR)/vm/operations/vm_control_flow.c $(SRCDIR)/vm/operations/vm_typed_ops.c $(SRCDIR)/vm/operations/vm_string_ops.c $(SRCDIR)/vm/operations/vm_comparison.c $(SRCDIR)/vm/dispatch/vm_dispatch_switch.c $(SRCDIR)/vm/dispatch/vm_dispatch_goto.c $(SRCDIR)/vm/core/vm_validation.c $(SRCDIR)/vm/register_file.c $(SRCDIR)/vm/spill_manager.c $(SRCDIR)/vm/module_manager.c $(SRCDIR)/vm/register_cache.c $(SRCDIR)/vm/profiling/vm_profiling.c $(SRCDIR)/vm/vm_config.c $(SRCDIR)/type/type_representation.c $(SRCDIR)/type/type_inference.c $(SRCDIR)/errors/infrastructure/error_infrastructure.c $(SRCDIR)/errors/core/error_base.c $(SRCDIR)/errors/features/type_errors.c $(SRCDIR)/errors/features/variable_errors.c $(SRCDIR)/errors/features/control_flow_errors.c $(SRCDIR)/config/config.c $(SRCDIR)/internal/logging.c
REPL_SRC = $(SRCDIR)/repl.c
MAIN_SRC = $(SRCDIR)/main.c

# Unit testing files
UNITY_SRCS = $(TESTDIR)/unit/unity.c
UNIT_TEST_SRCS = $(TESTDIR)/unit/test_shared_compilation.c $(TESTDIR)/unit/test_backend_selection.c $(TESTDIR)/unit/test_vm_optimization.c
TEST_RUNNER_SRC = $(TESTDIR)/run_unit_tests.c

# Object files (profile-specific)
COMPILER_OBJS = $(COMPILER_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
VM_OBJS = $(VM_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
REPL_OBJ = $(REPL_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
MAIN_OBJ = $(MAIN_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Unit test object files
UNITY_OBJS = $(UNITY_SRCS:$(TESTDIR)/%.c=$(BUILDDIR)/tests/%.o)
UNIT_TEST_OBJS = $(UNIT_TEST_SRCS:$(TESTDIR)/%.c=$(BUILDDIR)/tests/%.o)
TEST_RUNNER_OBJ = $(TEST_RUNNER_SRC:$(TESTDIR)/%.c=$(BUILDDIR)/tests/%.o)

# Target (profile-specific)
ORUS = orus$(SUFFIX)
UNIT_TEST_RUNNER = test_runner$(SUFFIX)

.PHONY: all clean test unit-test benchmark help debug release profiling analyze install

all: build-info $(ORUS)

# Build information
build-info:
	@echo "Building Orus Language Interpreter"
	@echo "Profile: $(PROFILE_DESC)"
	@echo "Target: $(ORUS)"
	@echo "Architecture: $(UNAME_S) $(UNAME_M)"
	@echo ""

# Profile-specific build targets
debug:
	@$(MAKE) PROFILE=debug

release:
	@$(MAKE) PROFILE=release

profiling:
	@$(MAKE) PROFILE=profiling

ci:
	@$(MAKE) PROFILE=ci

# Create build directory
$(BUILDDIR):
	@mkdir -p $(BUILDDIR) $(BUILDDIR)/vm/core $(BUILDDIR)/vm/dispatch $(BUILDDIR)/vm/operations $(BUILDDIR)/vm/runtime $(BUILDDIR)/vm/utils $(BUILDDIR)/vm/handlers $(BUILDDIR)/vm/profiling $(BUILDDIR)/compiler/backend/optimization $(BUILDDIR)/compiler/backend/codegen $(BUILDDIR)/type $(BUILDDIR)/errors/core $(BUILDDIR)/errors/features $(BUILDDIR)/errors/infrastructure $(BUILDDIR)/config $(BUILDDIR)/internal $(BUILDDIR)/tests/unit

# Main interpreter
$(ORUS): $(MAIN_OBJ) $(REPL_OBJ) $(VM_OBJS) $(COMPILER_OBJS)
	@echo "Linking $(ORUS)..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Build complete: $(ORUS)"

# Object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test object files
$(BUILDDIR)/tests/%.o: $(TESTDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@echo "Compiling test $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Unit test runner
$(UNIT_TEST_RUNNER): $(TEST_RUNNER_OBJ) $(UNITY_OBJS) $(UNIT_TEST_OBJS) $(VM_OBJS) $(COMPILER_OBJS)
	@echo "Linking unit test runner $(UNIT_TEST_RUNNER)..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Unit test runner built: $(UNIT_TEST_RUNNER)"

# Run comprehensive test suite
test: $(ORUS)
	@echo "Running Comprehensive Test Suite..."
	@echo "==================================="
	@passed=0; failed=0; current_dir=""; \
	SUBDIRS="benchmarks comments control_flow edge_cases expressions formatting functions literals register_file scope_analysis strings type_safety_fails types/f64 types/i32 types/i64 variables"; \
	for subdir in $$SUBDIRS; do \
		for test_file in $$(find $(TESTDIR)/$$subdir -type f -name "*.orus" | sort); do \
			if [ -f "$$test_file" ]; then \
				if [ "$$subdir" != "$$current_dir" ]; then \
					current_dir=$$subdir; \
					echo ""; \
					echo "\033[36m=== $$current_dir Tests ===\033[0m"; \
				fi; \
				printf "Testing: $$test_file ... "; \
				if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
					printf "\033[32mPASS\033[0m\n"; \
					passed=$$((passed + 1)); \
				else \
					if echo "$$test_file" | grep -q -E "$(TESTDIR)/type_safety_fails|$(TESTDIR)/edge_cases/modulo_by_zero_test.orus|$(TESTDIR)/edge_cases/large_number_modulo_by_zero.orus|$(TESTDIR)/edge_cases/expression_modulo_by_zero.orus|$(TESTDIR)/edge_cases/division_by_zero_enhanced.orus|$(TESTDIR)/edge_cases/division_by_zero_runtime.orus|$(TESTDIR)/types/u32/u32_division_by_zero.orus|$(TESTDIR)/types/f64/test_f64_runtime_div.orus"; then \
						printf "\033[32mCORRECT FAIL\033[0m\n"; \
						passed=$$((passed + 1)); \
					else \
						printf "\033[31mFAIL\033[0m\n"; \
						failed=$$((failed + 1)); \
					fi; \
				fi; \
			fi; \
		done; \
	done; \
	echo ""; \
	echo "========================"; \
	echo "\033[36m=== Test Summary ===\033[0m"; \
	if [ $$failed -eq 0 ]; then \
		echo "\033[32m✓ All $$passed tests passed!\033[0m"; \
	else \
		echo "\033[31m✗ $$failed test(s) failed, $$passed test(s) passed.\033[0m"; \
	fi; \
	echo ""

# Run unit tests
unit-test: $(UNIT_TEST_RUNNER)
	@echo "Running Unit Test Suite..."
	@echo "=========================="
	@./$(UNIT_TEST_RUNNER)

# CI test target: Build with warnings as errors and run full test suite
ci-test: 
	@echo "Building with CI profile (warnings as errors)..."
	@$(MAKE) clean
	@$(MAKE) PROFILE=ci
	@echo ""
	@echo "Running tests with CI build..."
	@$(MAKE) test ORUS=orus_ci
	@echo "Running unit tests with CI build..."
	@$(MAKE) unit-test UNIT_TEST_RUNNER=test_runner_ci

# Run cross-language benchmark tests
benchmark: $(ORUS)
	@cd $(TESTDIR)/benchmarks && ./unified_benchmark.sh
	
# Static Analysis
analyze:
	@echo "Running static analysis..."
	@echo "==========================="
	@echo "🔍 Running cppcheck..."
	@command -v cppcheck >/dev/null 2>&1 && cppcheck --enable=all --std=c11 --platform=unix64 --error-exitcode=1 $(SRCDIR)/ || echo "⚠️  cppcheck not found, skipping"
	@echo ""
	@echo "🔍 Running clang static analyzer..."
	@command -v clang >/dev/null 2>&1 && scan-build --status-bugs make clean all || echo "⚠️  clang static analyzer not found, skipping"
	@echo ""
	@echo "✓ Static analysis complete"

# Cross-compilation targets
cross-linux:
	@echo "Cross-compiling for Linux x86_64..."
	@$(MAKE) CC=x86_64-linux-gnu-gcc PROFILE=release

cross-windows:
	@echo "Cross-compiling for Windows x86_64..."
	@$(MAKE) CC=x86_64-w64-mingw32-gcc PROFILE=release LDFLAGS="-lm -static"

# Installation
install: release
	@echo "Installing Orus to /usr/local/bin..."
	@sudo cp orus /usr/local/bin/orus
	@echo "✓ Orus installed successfully"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -f orus orus_debug orus_profiling orus_ci
	@rm -f test_runner test_runner_debug test_runner_profiling test_runner_ci
	@rm -rf build/
	@echo "✓ Clean complete"

# Help
help:
	@echo "Enhanced Orus Build System"
	@echo "========================="
	@echo ""
	@echo "Build Profiles:"
	@echo "  debug     - Debug build (no optimization, full debugging)"
	@echo "  release   - Release build (maximum optimization)"
	@echo "  profiling - Profiling build (optimization + instrumentation)"
	@echo ""
	@echo "Main Targets:"
	@echo "  all       - Build for current profile (default: debug)"
	@echo "  test      - Run comprehensive test suite"
	@echo "  unit-test - Run unit tests for compiler components"
	@echo "  benchmark - Run cross-language performance benchmarks"
	@echo "  clean     - Remove all build artifacts"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Development Targets:"
	@echo "  analyze   - Run static analysis (cppcheck, clang analyzer)"
	@echo "  install   - Install release build to /usr/local/bin"
	@echo ""
	@echo "Cross-compilation:"
	@echo "  cross-linux   - Cross-compile for Linux x86_64"
	@echo "  cross-windows - Cross-compile for Windows x86_64"
	@echo ""
	@echo "Examples:"
	@echo "  make                    - Build debug version (creates orus_debug)"
	@echo "  make release            - Build optimized release version (creates orus)"
	@echo "  make PROFILE=profiling  - Build with profiling support (creates orus_profiling)"
	@echo "  make PROFILE=ci         - Build with warnings as errors (creates orus_ci)"
	@echo "  make test               - Run tests (builds debug if needed)"
	@echo "  make ci-test            - Build with warnings as errors and run tests"
	@echo "  make integration-test   - Run cross-feature integration tests"
	@echo "  make benchmark          - Run benchmarks (builds debug if needed)"
	@echo "  make analyze            - Run static analysis tools"
	@echo ""
	@echo "Advanced Usage:"
	@echo "  CC=clang make           - Use clang compiler"
	@echo "  PROFILE=release make    - Explicit profile selection"