CC = gcc
EMCC ?= emcc

# Build Profile Configuration
# Usage: make PROFILE=debug|release|profiling|ci
# Default is debug for development
PROFILE ?= debug

# Dedicated profiles for higher-level automation targets.
# These default to release builds so that long-running suites remain
# responsive now that loop-specific fast paths have been removed.
TEST_PROFILE ?= release
BENCHMARK_PROFILE ?= release

# Architecture Detection
UNAME_M := $(shell uname -m)
UNAME_S := $(shell uname -s)

# Base compiler flags
BASE_CFLAGS = -Wall -Wextra -std=c11

empty :=
space := $(empty) $(empty)

# Architecture-specific optimizations
# Set PORTABLE=1 to build without aggressive CPU-specific tuning (safer on some macOS setups)
# On Apple Silicon we default to the portable configuration because the aggressive
# cpu-specific flags have been observed to cause truncated execution when Apple
# Clang mis-optimizes hot dispatch loops. Users can opt back in with PORTABLE=0
# once they have verified their toolchain.

# Determine the default portability mode only if the caller did not specify it.
ifeq ($(origin PORTABLE), undefined)
    ifeq ($(UNAME_S),Darwin)
        ifeq ($(UNAME_M),arm64)
            PORTABLE := 1
            PORTABLE_AUTO_REASON := $(space)(auto-enabled for macOS arm64 to avoid Apple Clang mis-optimizations)
        endif
    endif
endif

PORTABLE ?= 0

PORTABLE_DESC = $(if $(filter 1,$(PORTABLE)),enabled,disabled)
PORTABLE_NOTE = $(if $(and $(filter 1,$(PORTABLE)),$(PORTABLE_AUTO_REASON)),$(PORTABLE_AUTO_REASON),)

ARCH_FLAGS =
ARCH_DEFINES =

# Build metadata (commit + date)
GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null)
GIT_COMMIT := $(strip $(GIT_COMMIT))
ifeq ($(GIT_COMMIT),)
    GIT_COMMIT := unknown
endif

GIT_COMMIT_DATE := $(shell git show -s --format=%cs HEAD 2>/dev/null)
GIT_COMMIT_DATE := $(strip $(GIT_COMMIT_DATE))

BUILD_DATE := $(shell date -u +%Y-%m-%d 2>/dev/null)
BUILD_DATE := $(strip $(BUILD_DATE))
ifeq ($(BUILD_DATE),)
    BUILD_DATE := unknown
endif

ifeq ($(GIT_COMMIT_DATE),)
    GIT_COMMIT_DATE := $(BUILD_DATE)
endif


ifeq ($(PORTABLE),1)
    # Portable build avoids micro-arch specific flags that can trigger kernel kills on some systems
    ifeq ($(UNAME_M),arm64)
        ARCH_FLAGS = -arch arm64
    else ifeq ($(UNAME_M),x86_64)
        ARCH_FLAGS = -arch x86_64
    endif
else
    ifeq ($(UNAME_M),arm64)
        # Apple Silicon (M1/M2/M3) optimizations
        ARCH_FLAGS = -mcpu=apple-m1 -mtune=apple-m1 -march=armv8.4-a+simd+crypto+sha3
    else ifeq ($(UNAME_M),x86_64)
        # Intel/AMD x86_64 optimizations
        ARCH_FLAGS = -march=native -mtune=native
    endif
endif

# Dispatch selection: prefer computed goto on Linux/macOS, fall back to switch on Linux ARM and other platforms
DISPATCH_MODE ?= auto

ifeq ($(DISPATCH_MODE),auto)
    ifeq ($(UNAME_S),Linux)
        ifeq ($(UNAME_M),arm64)
            DISPATCH_DEFINE = -DUSE_COMPUTED_GOTO=0
        else ifeq ($(UNAME_M),aarch64)
            DISPATCH_DEFINE = -DUSE_COMPUTED_GOTO=0
        else
            DISPATCH_DEFINE = -DUSE_COMPUTED_GOTO=1
        endif
    else ifeq ($(UNAME_S),Darwin)
        DISPATCH_DEFINE = -DUSE_COMPUTED_GOTO=1
    else
        DISPATCH_DEFINE = -DUSE_COMPUTED_GOTO=0
    endif
else ifeq ($(DISPATCH_MODE),goto)
    DISPATCH_DEFINE = -DUSE_COMPUTED_GOTO=1
else ifeq ($(DISPATCH_MODE),switch)
    DISPATCH_DEFINE = -DUSE_COMPUTED_GOTO=0
else
    $(error Invalid DISPATCH_MODE: $(DISPATCH_MODE). Use auto, goto, or switch)
endif

ARCH_DEFINES += $(DISPATCH_DEFINE)

# Distribution packaging configuration
DIST_DIR = dist

ifeq ($(UNAME_S),Darwin)
    DIST_OS = macos
else ifeq ($(UNAME_S),Linux)
    DIST_OS = linux
else
    DIST_OS = $(shell echo $(UNAME_S) | tr '[:upper:]' '[:lower:]')
endif

ifeq ($(UNAME_M),arm64)
    DIST_ARCH = arm64
else ifeq ($(UNAME_M),aarch64)
    DIST_ARCH = arm64
else ifeq ($(UNAME_M),x86_64)
    DIST_ARCH = x86_64
else ifeq ($(UNAME_M),amd64)
    DIST_ARCH = x86_64
else
    DIST_ARCH = $(UNAME_M)
endif

DIST_ARCHIVE = $(DIST_DIR)/orus-$(DIST_OS)-$(DIST_ARCH).tar.gz

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
CFLAGS += -DORUS_BUILD_COMMIT=\"$(GIT_COMMIT)\" -DORUS_BUILD_DATE=\"$(GIT_COMMIT_DATE)\"

LDFLAGS = -lm

# Add profiling link flags if needed
ifeq ($(PROFILE),profiling)
    LDFLAGS += -pg
endif

# WebAssembly build configuration
WASM_OUTPUT_DIR = web
WASM_MODULE = orus_web$(SUFFIX)
WASM_JS = $(WASM_OUTPUT_DIR)/$(WASM_MODULE).js
WASM_WASM = $(WASM_OUTPUT_DIR)/$(WASM_MODULE).wasm
WASM_SRCS = $(COMPILER_SRCS) $(VM_SRCS) $(REPL_SRC) $(MAIN_SRC) $(SRCDIR)/web/wasm_bridge.c
WASM_EXPORTED_FUNCTIONS = ["_main","_initWebVM","_runSource","_freeWebVM","_getVersion","_setInputCallback","_setOutputCallback","_registerWebBuiltins","_getLastError","_clearLastError","_isVMReady","_resetVMState","_getVMStackSize","_getVMFrameCount","_getVMModuleCount"]
WASM_RUNTIME_METHODS = ["cwrap","ccall","UTF8ToString","lengthBytesUTF8"]
WASM_CFLAGS = $(BASE_CFLAGS) $(DEFINES)

ifeq ($(PROFILE),debug)
    WASM_OPT_FLAGS = -O0 -g4 -sASSERTIONS=1
else
    WASM_OPT_FLAGS = -O3
endif

WASM_EMCC_FLAGS = $(WASM_OPT_FLAGS) -sWASM=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sENVIRONMENT=web \
                  -sEXPORTED_FUNCTIONS=$(WASM_EXPORTED_FUNCTIONS) -sEXPORTED_RUNTIME_METHODS=$(WASM_RUNTIME_METHODS)

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
COMPILER_BACKEND_SRCS = $(SRCDIR)/compiler/backend/typed_ast_visualizer.c $(SRCDIR)/compiler/backend/register_allocator.c $(SRCDIR)/compiler/backend/compiler.c $(SRCDIR)/compiler/backend/optimization/optimizer.c $(SRCDIR)/compiler/backend/optimization/constantfold.c $(SRCDIR)/compiler/backend/codegen/codegen.c $(SRCDIR)/compiler/backend/codegen/expressions.c $(SRCDIR)/compiler/backend/codegen/statements.c $(SRCDIR)/compiler/backend/codegen/functions.c $(SRCDIR)/compiler/backend/codegen/modules.c $(SRCDIR)/compiler/backend/codegen/peephole.c $(SRCDIR)/compiler/backend/error_reporter.c $(SRCDIR)/compiler/backend/scope_stack.c $(SRCDIR)/compiler/symbol_table.c

# Combined simplified compiler sources  
COMPILER_SRCS = $(COMPILER_FRONTEND_SRCS) $(COMPILER_BACKEND_SRCS) $(SRCDIR)/compiler/typed_ast.c $(SRCDIR)/debug/debug_config.c
VM_SRCS = $(SRCDIR)/vm/core/vm_core.c $(SRCDIR)/vm/core/vm_tagged_union.c $(SRCDIR)/vm/runtime/vm.c $(SRCDIR)/vm/core/vm_memory.c $(SRCDIR)/vm/utils/debug.c $(SRCDIR)/vm/runtime/builtin_print.c $(SRCDIR)/vm/runtime/builtin_input.c $(SRCDIR)/vm/runtime/builtin_array_push.c $(SRCDIR)/vm/runtime/builtin_array_pop.c $(SRCDIR)/vm/runtime/builtin_timestamp.c $(SRCDIR)/vm/runtime/builtin_number.c $(SRCDIR)/vm/runtime/builtin_typeof.c $(SRCDIR)/vm/runtime/builtin_istype.c $(SRCDIR)/vm/runtime/builtin_range.c $(SRCDIR)/vm/runtime/builtin_sorted.c $(SRCDIR)/vm/runtime/builtin_assert.c $(SRCDIR)/vm/operations/vm_arithmetic.c $(SRCDIR)/vm/operations/vm_control_flow.c $(SRCDIR)/vm/operations/vm_typed_ops.c $(SRCDIR)/vm/operations/vm_string_ops.c $(SRCDIR)/vm/operations/vm_comparison.c $(SRCDIR)/vm/handlers/vm_arithmetic_handlers.c $(SRCDIR)/vm/handlers/vm_control_flow_handlers.c $(SRCDIR)/vm/handlers/vm_memory_handlers.c $(SRCDIR)/vm/dispatch/vm_dispatch_switch.c $(SRCDIR)/vm/dispatch/vm_dispatch_goto.c $(SRCDIR)/vm/core/vm_validation.c $(SRCDIR)/vm/register_file.c $(SRCDIR)/vm/spill_manager.c $(SRCDIR)/vm/module_manager.c $(SRCDIR)/vm/register_cache.c $(SRCDIR)/vm/profiling/vm_profiling.c $(SRCDIR)/vm/vm_config.c $(SRCDIR)/type/type_representation.c $(SRCDIR)/type/type_inference.c $(SRCDIR)/errors/infrastructure/error_infrastructure.c $(SRCDIR)/errors/core/error_base.c $(SRCDIR)/errors/features/type_errors.c $(SRCDIR)/errors/features/variable_errors.c $(SRCDIR)/errors/features/control_flow_errors.c $(SRCDIR)/config/config.c $(SRCDIR)/internal/logging.c
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
BYTECODE_TEST_BIN = $(BUILDDIR)/tests/test_jump_patch
SOURCE_MAP_TEST_BIN = $(BUILDDIR)/tests/test_source_mapping
SCOPE_TRACKING_TEST_BIN = $(BUILDDIR)/tests/test_scope_tracking
PEEPHOLE_TEST_BIN = $(BUILDDIR)/tests/test_constant_propagation
TAGGED_UNION_TEST_BIN = $(BUILDDIR)/tests/test_vm_tagged_union
TYPED_REGISTER_TEST_BIN = $(BUILDDIR)/tests/test_vm_typed_registers
REGISTER_ALLOCATOR_TEST_BIN = $(BUILDDIR)/tests/test_register_allocator
BUILTIN_INPUT_TEST_BIN = $(BUILDDIR)/tests/test_builtin_input
CONSTANT_FOLD_TEST_BIN = $(BUILDDIR)/tests/test_constant_folding
BUILTIN_SORTED_ORUS_TESTS = \
    tests/builtins/sorted_runtime.orus
BUILTIN_SORTED_ORUS_FAIL_TESTS = \
    tests/builtins/sorted_struct_fail.orus \
    tests/builtins/sorted_nested_array_fail.orus
BUILTIN_RANGE_ORUS_TESTS = tests/builtins/range_runtime.orus
BUILTIN_RANGE_ORUS_FAIL_TESTS = \
    tests/builtins/range_invalid_string_stop.orus \
    tests/builtins/range_invalid_string_bounds.orus \
    tests/builtins/range_zero_step.orus \
    tests/builtins/range_float_step.orus \
    tests/builtins/range_overflow_stop.orus

.PHONY: all clean test unit-test test-control-flow benchmark help debug release release-with-wasm profiling analyze install dist package bytecode-jump-tests source-map-tests scope-tracking-tests peephole-tests cli-smoke-tests tagged-union-tests typed-register-tests register-allocator-tests builtin-input-tests builtin-range-tests test-optimizer wasm _test-run _benchmark-run

all: build-info $(ORUS)

# Build information
build-info:
	@echo "Building Orus Language Interpreter"
	@echo "Profile: $(PROFILE_DESC)"
	@echo "Target: $(ORUS)"
	@echo "Architecture: $(UNAME_S) $(UNAME_M)"
	@echo "Portable mode: $(PORTABLE_DESC)$(PORTABLE_NOTE)"
	@echo ""

# Profile-specific build targets
debug:
	@$(MAKE) PROFILE=debug

release:
	@$(MAKE) PROFILE=release all
	@$(MAKE) PROFILE=release package

release-with-wasm:
	@$(MAKE) PROFILE=release all
	@$(MAKE) PROFILE=release wasm
	@$(MAKE) PROFILE=release package

profiling:
	@$(MAKE) PROFILE=profiling

ci:
	@$(MAKE) PROFILE=ci

# Create build directory
$(BUILDDIR):
	@mkdir -p $(BUILDDIR) $(BUILDDIR)/vm/core $(BUILDDIR)/vm/dispatch $(BUILDDIR)/vm/operations $(BUILDDIR)/vm/runtime $(BUILDDIR)/vm/utils $(BUILDDIR)/vm/handlers $(BUILDDIR)/vm/profiling $(BUILDDIR)/compiler/backend/optimization $(BUILDDIR)/compiler/backend/codegen $(BUILDDIR)/type $(BUILDDIR)/errors/core $(BUILDDIR)/errors/features $(BUILDDIR)/errors/infrastructure $(BUILDDIR)/config $(BUILDDIR)/internal $(BUILDDIR)/debug $(BUILDDIR)/tests/unit $(BUILDDIR)/web

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

wasm: $(WASM_JS)
	@echo "✓ WebAssembly artifacts ready: $(WASM_JS) and $(WASM_WASM)"

$(WASM_JS): $(WASM_SRCS)
	@mkdir -p $(WASM_OUTPUT_DIR)
	@echo "Compiling WebAssembly target ($(PROFILE))..."
	@command -v $(EMCC) >/dev/null 2>&1 || { \
		echo "Emscripten compiler '$(EMCC)' not found. Install Emscripten SDK or set EMCC=<path>."; \
		exit 1; \
	}
	@$(EMCC) $(WASM_CFLAGS) $(INCLUDES) $(WASM_EMCC_FLAGS) $(WASM_SRCS) -o $(WASM_JS)

$(WASM_WASM): $(WASM_JS)

# Run comprehensive test suite
test:
	@$(MAKE) PROFILE=$(TEST_PROFILE) _test-run

_test-run: $(ORUS)
	@echo "Running Comprehensive Test Suite..."
	@echo "==================================="
	@passed=0; failed=0; current_dir=""; \
		expected_fail_files="$$(find "$(TESTDIR)/type_safety_fails" -type f -name "*.orus" 2>/dev/null)"; \
		expected_fail_files="$$expected_fail_files $(TESTDIR)/types/u32/u32_division_by_zero.orus $(TESTDIR)/types/f64/test_f64_runtime_div.orus"; \
		expected_fail_files="$$(printf '%s\n' $$expected_fail_files | sed '/^$$/d' | sort)"; \
	SUBDIRS="algorithms arithmetic arrays benchmarks builtins comments comprehensive control_flow expressions functions io modules strings structs types variables"; \
	for subdir in $$SUBDIRS; do \
		subdir_path="$(TESTDIR)/$$subdir"; \
		if [ ! -d "$$subdir_path" ]; then \
			continue; \
		fi; \
		for test_file in $$(find "$$subdir_path" -type f -name "*.orus" | sort); do \
			if printf '%s\n' "$$expected_fail_files" | grep -Fx "$$test_file" >/dev/null; then \
				continue; \
			fi; \
			if [ "$$subdir" != "$$current_dir" ]; then \
				current_dir=$$subdir; \
				echo ""; \
				echo "\033[36m=== $$current_dir Tests ===\033[0m"; \
			fi; \
                        stdin_file="$${test_file%.orus}.stdin"; \
                        printf "Testing: $$test_file ... "; \
                        if [ -f "$$stdin_file" ]; then \
                                if ./$(ORUS) "$$test_file" < "$$stdin_file" >/dev/null 2>&1; then \
                                        printf "\033[32mPASS\033[0m\n"; \
                                        passed=$$((passed + 1)); \
                                else \
                                        printf "\033[31mFAIL\033[0m\n"; \
                                        failed=$$((failed + 1)); \
                                fi; \
                        elif ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
                                printf "\033[32mPASS\033[0m\n"; \
                                passed=$$((passed + 1)); \
                        else \
                                printf "\033[31mFAIL\033[0m\n"; \
                                failed=$$((failed + 1)); \
			fi; \
		done; \
	done; \
	if [ -n "$$expected_fail_files" ]; then \
		echo ""; \
		echo "\033[36m=== CORRECT FAIL Tests ===\033[0m"; \
		for fail_test in $$expected_fail_files; do \
			if [ ! -f "$$fail_test" ]; then \
				continue; \
			fi; \
			printf "Testing: $$fail_test ... "; \
			if ./$(ORUS) "$$fail_test" >/dev/null 2>&1; then \
				printf "\033[31mUNEXPECTED PASS\033[0m\n"; \
				failed=$$((failed + 1)); \
			else \
				printf "\033[32mCORRECT FAIL\033[0m\n"; \
				passed=$$((passed + 1)); \
			fi; \
			done; \
	fi; \
	echo ""; \
	echo "========================"; \
	echo "\033[36m=== Test Summary ===\033[0m"; \
	if [ $$failed -eq 0 ]; then \
		echo "\033[32m✓ All $$passed tests passed!\033[0m"; \
	else \
		echo "\033[31m✗ $$failed test(s) failed, $$passed test(s) passed.\033[0m"; \
	fi; \
	echo ""; \

	@echo "\033[36m=== Bytecode Jump Patch Tests ===\033[0m"
	@$(MAKE) bytecode-jump-tests
	@echo ""
	@echo "\033[36m=== Source Mapping Tests ===\033[0m"
	@$(MAKE) source-map-tests
	@echo ""
	@echo "\033[36m=== Scope Tracking Tests ===\033[0m"
	@$(MAKE) scope-tracking-tests
	@echo ""
	@echo "\033[36m=== Peephole Constant Propagation Tests ===\033[0m"
	@$(MAKE) peephole-tests
	@echo ""
	@echo "\033[36m=== Constant Folding Tests ===\033[0m"
	@$(MAKE) constant-fold-tests
	@echo ""
	@echo "\033[36m=== Tagged Union Tests ===\033[0m"
	@$(MAKE) tagged-union-tests
	@echo ""
	@echo "\033[36m=== Typed Register Tests ===\033[0m"
	@$(MAKE) typed-register-tests
	@echo ""
	@echo "\033[36m=== Register Allocator Tests ===\033[0m"
	@$(MAKE) register-allocator-tests
	@echo ""
	@echo "\033[36m=== Builtin Input Tests ===\033[0m"
	@$(MAKE) builtin-input-tests
	@echo ""
	@echo "\033[36m=== Builtin Sorted Tests ===\033[0m"
	@$(MAKE) builtin-sorted-tests
	@echo ""
	@echo "\033[36m=== Builtin Range Tests ===\033[0m"
	@$(MAKE) builtin-range-tests
	@echo ""
	@echo "\033[36m=== Error Reporting Tests ===\033[0m"
	@python3 tests/error_reporting/run_error_tests.py ./$(ORUS)
	@echo ""
	@echo "\033[36m=== CLI Smoke Tests ===\033[0m"
	@python3 tests/comprehensive/run_cli_smoke_tests.py ./$(ORUS)

# Run unit tests
unit-test: $(UNIT_TEST_RUNNER)
	@echo "Running Unit Test Suite..."
	@echo "=========================="
	@./$(UNIT_TEST_RUNNER)

$(BYTECODE_TEST_BIN): tests/unit/test_jump_patch.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling bytecode jump patch tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

bytecode-jump-tests: $(BYTECODE_TEST_BIN)
	@echo "Running bytecode jump patch tests..."
	@./$(BYTECODE_TEST_BIN)

$(SOURCE_MAP_TEST_BIN): tests/unit/test_source_mapping.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling source mapping tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

source-map-tests: $(SOURCE_MAP_TEST_BIN)
	@echo "Running source mapping tests..."
	@./$(SOURCE_MAP_TEST_BIN)

$(SCOPE_TRACKING_TEST_BIN): tests/unit/test_scope_stack.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling scope tracking tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

scope-tracking-tests: $(SCOPE_TRACKING_TEST_BIN)
	@echo "Running scope tracking tests..."
	@./$(SCOPE_TRACKING_TEST_BIN)

$(PEEPHOLE_TEST_BIN): tests/unit/test_constant_propagation.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling constant propagation tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

peephole-tests: $(PEEPHOLE_TEST_BIN)
	@echo "Running constant propagation tests..."
	@./$(PEEPHOLE_TEST_BIN)

$(CONSTANT_FOLD_TEST_BIN): tests/unit/test_constant_folding.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling constant folding tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

constant-fold-tests: $(CONSTANT_FOLD_TEST_BIN)
	@echo "Running constant folding tests..."
	@./$(CONSTANT_FOLD_TEST_BIN)

$(TAGGED_UNION_TEST_BIN): tests/unit/test_vm_tagged_union.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling tagged union tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tagged-union-tests: $(TAGGED_UNION_TEST_BIN)
	@echo "Running tagged union tests..."
	@./$(TAGGED_UNION_TEST_BIN)

$(TYPED_REGISTER_TEST_BIN): tests/unit/test_vm_typed_registers.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling typed register coherence tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

typed-register-tests: $(TYPED_REGISTER_TEST_BIN)
	@echo "Running typed register coherence tests..."
	@./$(TYPED_REGISTER_TEST_BIN)

$(REGISTER_ALLOCATOR_TEST_BIN): tests/unit/test_register_allocator.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling register allocator tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

register-allocator-tests: $(REGISTER_ALLOCATOR_TEST_BIN)
	@echo "Running register allocator tests..."
	@./$(REGISTER_ALLOCATOR_TEST_BIN)

$(BUILTIN_INPUT_TEST_BIN): tests/unit/test_builtin_input.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling builtin input tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

builtin-input-tests: $(BUILTIN_INPUT_TEST_BIN)
	@echo "Running builtin input tests..."
	@./$(BUILTIN_INPUT_TEST_BIN)

builtin-sorted-tests: $(ORUS)
	@echo "Running builtin sorted runtime tests..."
	@passed=0; failed=0; \
	for test_file in $(BUILTIN_SORTED_ORUS_TESTS); do \
		printf "Testing: $$test_file ... "; \
		if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
			printf "\033[32mPASS\033[0m\n"; \
			passed=$$((passed + 1)); \
		else \
			printf "\033[31mFAIL\033[0m\n"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	for test_file in $(BUILTIN_SORTED_ORUS_FAIL_TESTS); do \
		printf "Testing: $$test_file (expected failure) ... "; \
		if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
			printf "\033[31mUNEXPECTED PASS\033[0m\n"; \
			failed=$$((failed + 1)); \
		else \
			printf "\033[32mCORRECT FAIL\033[0m\n"; \
			passed=$$((passed + 1)); \
		fi; \
	done; \
	if [ $$failed -ne 0 ]; then \
		exit 1; \
	fi

builtin-range-tests: $(ORUS)
	@echo "Running builtin range runtime tests..."
	@passed=0; failed=0; \
	for test_file in $(BUILTIN_RANGE_ORUS_TESTS); do \
		printf "Testing: $$test_file ... "; \
		if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
			printf "\033[32mPASS\033[0m\n"; \
			passed=$$((passed + 1)); \
		else \
			printf "\033[31mFAIL\033[0m\n"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	for test_file in $(BUILTIN_RANGE_ORUS_FAIL_TESTS); do \
		printf "Testing: $$test_file (expected failure) ... "; \
		if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
			printf "\033[31mUNEXPECTED PASS\033[0m\n"; \
			failed=$$((failed + 1)); \
		else \
			printf "\033[32mCORRECT FAIL\033[0m\n"; \
			passed=$$((passed + 1)); \
		fi; \
	done; \
	if [ $$failed -ne 0 ]; then \
		exit 1; \
	fi

cli-smoke-tests:
	@python3 tests/comprehensive/run_cli_smoke_tests.py ./$(ORUS)

test-control-flow: $(ORUS)
	@echo "Running Control Flow Tests..."
	@passed=0; failed=0; \
	for test_file in $$(find $(TESTDIR)/control_flow -type f -name "*.orus" | sort); do \
		printf "Testing: $$test_file ... "; \
		if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
			printf "\033[32mPASS\033[0m\n"; \
			passed=$$((passed + 1)); \
		else \
			printf "\033[31mFAIL\033[0m\n"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
		echo "\033[32m✓ Control flow tests passed ($$passed)\033[0m"; \
	else \
		echo "\033[31m✗ $$failed control flow test(s) failed\033[0m"; \
	fi


test-optimizer:
	@echo "Optimizer-specific tests removed with loop optimizer retirement."

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
benchmark:
	@$(MAKE) PROFILE=$(BENCHMARK_PROFILE) _benchmark-run

_benchmark-run: $(ORUS)
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
	@$(MAKE) CC=x86_64-linux-gnu-gcc PROFILE=release DISPATCH_MODE=goto

cross-windows:
	@echo "Cross-compiling for Windows x86_64..."
	@$(MAKE) CC=x86_64-w64-mingw32-gcc PROFILE=release DISPATCH_MODE=switch LDFLAGS="-lm -static"

# Installation
install: release
	@echo "Installing Orus to /usr/local/bin..."
	@sudo cp orus /usr/local/bin/orus
	@echo "✓ Orus installed successfully"

dist:
	@echo "'make dist' is deprecated; use 'make release' instead."
	@$(MAKE) release

package: $(ORUS)
	@mkdir -p $(DIST_DIR)
	@echo "Packaging $(DIST_ARCHIVE)..."
	@tar -czf $(DIST_ARCHIVE) $(ORUS) LICENSE
	@echo "✓ Package ready: $(DIST_ARCHIVE)"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -f orus orus_debug orus_profiling orus_ci
	@rm -f test_runner test_runner_debug test_runner_profiling test_runner_ci
	@rm -rf build/
	@rm -rf web/
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
	@echo "  release-with-wasm - Build release binary and WebAssembly bundle"
	@echo ""
	@echo "Cross-compilation:"
	@echo "  cross-linux   - Cross-compile for Linux x86_64"
	@echo "  cross-windows - Cross-compile for Windows x86_64"
	@echo ""
	@echo "Examples:"
	@echo "  make                    - Build debug version (creates orus_debug)"
	@echo "  make release            - Build optimized release version and package dist/orus-<os>-<arch>.tar.gz"
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
