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

# Installation directories
ifeq ($(UNAME_S),Darwin)
    INSTALL_PREFIX ?= /Library/Orus
else ifeq ($(UNAME_S),Linux)
    INSTALL_PREFIX ?= /usr/local/lib/orus
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    INSTALL_PREFIX ?= C:/Program Files/Orus
else ifneq (,$(findstring MSYS,$(UNAME_S)))
    INSTALL_PREFIX ?= C:/Program Files/Orus
else
    INSTALL_PREFIX ?= /usr/local/lib/orus
endif

INSTALL_BIN_DIR = $(INSTALL_PREFIX)/bin

# macOS arm64 builds must be codesigned with the JIT entitlement before the
# runtime can allocate MAP_JIT pages. Integrate the helper directly into the
# build so locally produced binaries are signed automatically when possible.
SIGN_HELPER := scripts/macos/sign-with-jit.sh

ifeq ($(UNAME_S),Darwin)
ifeq ($(UNAME_M),arm64)
define sign_with_jit
    @if command -v codesign >/dev/null 2>&1; then \
        if [ -x "$(SIGN_HELPER)" ]; then \
            if "$(SIGN_HELPER)" "$(1)"; then :; \
            else \
                echo "  ↳ warning: automatic macOS JIT signing failed for $(1)."; \
                echo "    Run $(SIGN_HELPER) manually once codesign is configured."; \
            fi; \
        else \
            echo "  ↳ warning: missing $(SIGN_HELPER); skipping macOS JIT signing for $(1)"; \
        fi; \
    else \
        echo "  ↳ warning: codesign not found; skipping macOS JIT signing for $(1)"; \
    fi
endef
else
define sign_with_jit
endef
endif
else
define sign_with_jit
endef
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
CFLAGS += -DORUS_BUILD_COMMIT=\"$(GIT_COMMIT)\" -DORUS_BUILD_DATE=\"$(GIT_COMMIT_DATE)\"

# Native builds on Unix platforms rely on pthread primitives for the JIT runtime
# (for example pthread_jit_write_protect_np on macOS arm64). clang on macOS does
# not automatically link against libpthread, so we need to propagate -pthread to
# both the compile and link flags when building natively. Keep the WebAssembly
# build separate so emscripten doesn't receive the flag.
PTHREAD_FLAGS =
ifneq (,$(filter Linux Darwin,$(UNAME_S)))
    PTHREAD_FLAGS = -pthread
endif

CFLAGS += $(PTHREAD_FLAGS)

LDFLAGS = -lm $(PTHREAD_FLAGS)

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
THIRD_PARTY_DIR = third_party
DYNASM_DIR = $(THIRD_PARTY_DIR)/dynasm

INCLUDES = -I$(INCDIR) -I$(DYNASM_DIR)

# Source files
# Compiler frontend (language processing)
COMPILER_FRONTEND_SRCS = $(SRCDIR)/compiler/frontend/lexer.c $(SRCDIR)/compiler/frontend/parser.c

# Keep multipass compiler and minimal dependencies 
COMPILER_BACKEND_SRCS = $(SRCDIR)/compiler/backend/typed_ast_visualizer.c $(SRCDIR)/compiler/backend/register_allocator.c $(SRCDIR)/compiler/backend/compiler.c $(SRCDIR)/compiler/backend/optimization/optimizer.c $(SRCDIR)/compiler/backend/optimization/loop_type_affinity.c $(SRCDIR)/compiler/backend/optimization/loop_type_residency.c $(SRCDIR)/compiler/backend/optimization/constantfold.c $(SRCDIR)/compiler/backend/codegen/codegen.c $(SRCDIR)/compiler/backend/codegen/expressions.c $(SRCDIR)/compiler/backend/codegen/statements.c $(SRCDIR)/compiler/backend/codegen/functions.c $(SRCDIR)/compiler/backend/codegen/modules.c $(SRCDIR)/compiler/backend/codegen/peephole.c $(SRCDIR)/compiler/backend/error_reporter.c $(SRCDIR)/compiler/backend/scope_stack.c $(SRCDIR)/compiler/backend/specialization/profiling_feedback.c $(SRCDIR)/compiler/symbol_table.c

# Combined simplified compiler sources  
COMPILER_SRCS = $(COMPILER_FRONTEND_SRCS) $(COMPILER_BACKEND_SRCS) $(SRCDIR)/compiler/typed_ast.c $(SRCDIR)/debug/debug_config.c
VM_SRCS = $(SRCDIR)/vm/core/vm_core.c $(SRCDIR)/vm/core/vm_tagged_union.c $(SRCDIR)/vm/runtime/vm.c $(SRCDIR)/vm/core/vm_memory.c $(SRCDIR)/vm/utils/debug.c $(SRCDIR)/vm/runtime/builtin_print.c $(SRCDIR)/vm/runtime/builtin_input.c $(SRCDIR)/vm/runtime/builtin_array_push.c $(SRCDIR)/vm/runtime/builtin_array_pop.c $(SRCDIR)/vm/runtime/builtin_array_repeat.c $(SRCDIR)/vm/runtime/builtin_timestamp.c $(SRCDIR)/vm/runtime/builtin_number.c $(SRCDIR)/vm/runtime/builtin_typeof.c $(SRCDIR)/vm/runtime/builtin_istype.c $(SRCDIR)/vm/runtime/builtin_range.c $(SRCDIR)/vm/runtime/builtin_sorted.c $(SRCDIR)/vm/runtime/builtin_assert.c $(SRCDIR)/vm/runtime/jit_benchmark.c $(SRCDIR)/vm/operations/vm_arithmetic.c $(SRCDIR)/vm/operations/vm_control_flow.c $(SRCDIR)/vm/operations/vm_typed_ops.c $(SRCDIR)/vm/operations/vm_string_ops.c $(SRCDIR)/vm/operations/vm_comparison.c $(SRCDIR)/vm/handlers/vm_arithmetic_handlers.c $(SRCDIR)/vm/handlers/vm_control_flow_handlers.c $(SRCDIR)/vm/handlers/vm_memory_handlers.c $(SRCDIR)/vm/dispatch/vm_dispatch_switch.c $(SRCDIR)/vm/dispatch/vm_dispatch_goto.c $(SRCDIR)/vm/core/vm_validation.c $(SRCDIR)/vm/register_file.c $(SRCDIR)/vm/spill_manager.c $(SRCDIR)/vm/module_manager.c $(SRCDIR)/vm/register_cache.c $(SRCDIR)/vm/profiling/vm_profiling.c $(SRCDIR)/vm/runtime/vm_tiering.c $(SRCDIR)/vm/vm_config.c $(SRCDIR)/vm/jit/orus_jit_backend.c $(SRCDIR)/vm/jit/orus_jit_debug.c $(SRCDIR)/vm/jit/orus_jit_ir.c $(SRCDIR)/vm/jit/orus_jit_ir_debug.c $(SRCDIR)/type/type_representation.c $(SRCDIR)/type/type_inference.c $(SRCDIR)/errors/infrastructure/error_infrastructure.c $(SRCDIR)/errors/core/error_base.c $(SRCDIR)/errors/features/type_errors.c $(SRCDIR)/errors/features/variable_errors.c $(SRCDIR)/errors/features/control_flow_errors.c $(SRCDIR)/config/config.c $(SRCDIR)/internal/logging.c
REPL_SRC = $(SRCDIR)/repl.c
MAIN_SRC = $(SRCDIR)/main.c
TOOLS_SRCS = $(SRCDIR)/tools/orus_prof.c

# Object files (profile-specific)
COMPILER_OBJS = $(COMPILER_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
VM_OBJS = $(VM_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
REPL_OBJ = $(REPL_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
MAIN_OBJ = $(MAIN_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
TOOLS_OBJS = $(TOOLS_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

OBJ_DIRS = $(sort $(dir $(COMPILER_OBJS) $(VM_OBJS) $(REPL_OBJ) $(MAIN_OBJ) $(TOOLS_OBJS)))

# Target (profile-specific)
ORUS = orus$(SUFFIX)
ORUS_PROF = orus-prof$(SUFFIX)
BYTECODE_TEST_BIN = $(BUILDDIR)/tests/test_jump_patch
SOURCE_MAP_TEST_BIN = $(BUILDDIR)/tests/test_source_mapping
FUSED_LOOP_CODEGEN_TEST_BIN = $(BUILDDIR)/tests/test_codegen_fused_loops
SCOPE_TRACKING_TEST_BIN = $(BUILDDIR)/tests/test_scope_tracking
FUSED_WHILE_TEST_BIN = $(BUILDDIR)/tests/test_codegen_fused_while
PEEPHOLE_TEST_BIN = $(BUILDDIR)/tests/test_constant_propagation
TAGGED_UNION_TEST_BIN = $(BUILDDIR)/tests/test_vm_tagged_union
TYPED_REGISTER_TEST_BIN = $(BUILDDIR)/tests/test_vm_typed_registers
VM_PRINT_TEST_BIN = $(BUILDDIR)/tests/test_vm_print_format
REGISTER_WINDOW_TEST_BIN = $(BUILDDIR)/tests/test_vm_register_windows
SPILL_GC_TEST_BIN = $(BUILDDIR)/tests/test_vm_spill_gc_root
REGISTER_ALLOCATOR_TEST_BIN = $(BUILDDIR)/tests/test_register_allocator
INC_CMP_JMP_TEST_BIN = $(BUILDDIR)/tests/test_vm_inc_cmp_jmp
FUSED_LOOP_BYTECODE_TEST_BIN = $(BUILDDIR)/tests/test_fused_loop_bytecode
ADD_I32_IMM_TEST_BIN = $(BUILDDIR)/tests/test_vm_add_i32_imm
SUB_I32_IMM_TEST_BIN = $(BUILDDIR)/tests/test_vm_sub_i32_imm
MUL_I32_IMM_TEST_BIN = $(BUILDDIR)/tests/test_vm_mul_i32_imm
INC_R_TEST_BIN = $(BUILDDIR)/tests/test_vm_inc_r
DEC_I32_R_TEST_BIN = $(BUILDDIR)/tests/test_vm_dec_i32_r
HOT_LOOP_PROFILING_TEST_BIN = $(BUILDDIR)/tests/test_vm_hot_loop_profiling
JIT_BENCHMARK_TEST_BIN = $(BUILDDIR)/tests/test_vm_jit_benchmark
JIT_TRANSLATION_TEST_BIN = $(BUILDDIR)/tests/test_vm_jit_translation
JIT_BACKEND_TEST_BIN = $(BUILDDIR)/tests/test_vm_jit_backend
JIT_CROSS_ARCH_PARITY_TEST_BIN = $(BUILDDIR)/tests/test_vm_jit_cross_arch
JIT_STRESS_TEST_BIN = $(BUILDDIR)/tests/test_vm_jit_stress
BUILTIN_INPUT_TEST_BIN = $(BUILDDIR)/tests/test_builtin_input
CONSTANT_FOLD_TEST_BIN = $(BUILDDIR)/tests/test_constant_folding
COMPILER_SPECIALIZATION_TEST_BIN = $(BUILDDIR)/tests/test_compiler_specialization

.PHONY: all clean test unit-test orus-tests c-unit-tests test-control-flow benchmark help debug release profiling analyze install bytecode-jump-tests source-map-tests scope-tracking-tests fused-while-tests peephole-tests tagged-union-tests typed-register-tests vm-print-tests register-window-tests spill-gc-tests inc-cmp-jmp-tests add-i32-imm-tests sub-i32-imm-tests mul-i32-imm-tests inc-r-tests hot-loop-tests dec-i32-r-tests register-allocator-tests builtin-input-tests compiler-specialization-tests constant-fold-tests fused-loop-tests fused-loop-bytecode-tests jit-translation-tests jit-backend-tests jit-backend-helper-tests jit-cross-arch-tests jit-stress-tests test-optimizer wasm _benchmark-run jit-benchmark-orus

all: build-info $(ORUS) $(ORUS_PROF)

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
	$(call sign_with_jit,./orus)

profiling:
	@$(MAKE) PROFILE=profiling

ci:
	@$(MAKE) PROFILE=ci

# Create build directory
$(BUILDDIR):
	@mkdir -p $(OBJ_DIRS)
# Main interpreter
$(ORUS): $(MAIN_OBJ) $(REPL_OBJ) $(VM_OBJS) $(COMPILER_OBJS)
	@echo "Linking $(ORUS)..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	$(call sign_with_jit,$@)
	@echo "✓ Build complete: $(ORUS)"

$(ORUS_PROF): $(TOOLS_OBJS)
	@echo "Linking $(ORUS_PROF)..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	$(call sign_with_jit,$@)
	@echo "✓ Build complete: $(ORUS_PROF)"

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

# Run unit tests
c_unit_test_get = $(patsubst $(2):=%,%,$(filter $(2):=%,$(subst |, ,$(1))))
c_unit_test_bin = $(BUILDDIR)/tests/$(basename $(notdir $(1)))
c_unit_test_desc = $(subst __,$(space),$(1))

# Descriptions encode spaces as "__" so opcode underscores remain intact.
C_UNIT_TEST_SPECS := \
        test_name:=bytecode-jump-tests|src:=tests/unit/test_jump_patch.c|description:=bytecode__jump__patch__tests|extra_cmds:= \
        test_name:=source-map-tests|src:=tests/unit/test_source_mapping.c|description:=source__mapping__tests|extra_cmds:= \
        test_name:=fused-loop-tests|src:=tests/unit/test_codegen_fused_loops.c|description:=fused__loop__codegen__tests|extra_cmds:= \
        test_name:=fused-loop-bytecode-tests|src:=tests/unit/test_fused_loop_bytecode.c|description:=fused__loop__bytecode__tests|extra_cmds:= \
        test_name:=fused-while-tests|src:=tests/unit/test_codegen_fused_while.c|description:=fused__while__codegen__tests|extra_cmds:= \
        test_name:=scope-tracking-tests|src:=tests/unit/test_scope_stack.c|description:=scope__tracking__tests|extra_cmds:= \
        test_name:=peephole-tests|src:=tests/unit/test_constant_propagation.c|description:=constant__propagation__tests|extra_cmds:= \
        test_name:=tagged-union-tests|src:=tests/unit/test_vm_tagged_union.c|description:=tagged__union__tests|extra_cmds:= \
        test_name:=typed-register-tests|src:=tests/unit/test_vm_typed_registers.c|description:=typed__register__coherence__tests|extra_cmds:= \
        test_name:=vm-print-tests|src:=tests/unit/test_vm_print_format.c|description:=VM__print__formatting__tests|extra_cmds:= \
        test_name:=register-window-tests|src:=tests/unit/test_vm_register_windows.c|description:=register__window__reuse__tests|extra_cmds:= \
        test_name:=spill-gc-tests|src:=tests/unit/test_vm_spill_gc_root.c|description:=spill__GC__regression__tests|extra_cmds:= \
        test_name:=inc-cmp-jmp-tests|src:=tests/unit/test_vm_inc_cmp_jmp.c|description:=OP_INC_CMP_JMP__regression__tests|extra_cmds:= \
        test_name:=add-i32-imm-tests|src:=tests/unit/test_vm_add_i32_imm.c|description:=OP_ADD_I32_IMM__regression__tests|extra_cmds:= \
        test_name:=sub-i32-imm-tests|src:=tests/unit/test_vm_sub_i32_imm.c|description:=OP_SUB_I32_IMM__regression__tests|extra_cmds:= \
        test_name:=mul-i32-imm-tests|src:=tests/unit/test_vm_mul_i32_imm.c|description:=OP_MUL_I32_IMM__regression__tests|extra_cmds:= \
        test_name:=inc-r-tests|src:=tests/unit/test_vm_inc_r.c|description:=OP_INC_*__typed__cache__regression__tests|extra_cmds:= \
        test_name:=hot-loop-tests|src:=tests/unit/test_vm_hot_loop_profiling.c|description:=VM__hot__loop__profiling__tests|extra_cmds:= \
        test_name:=dec-i32-r-tests|src:=tests/unit/test_vm_dec_i32_r.c|description:=OP_DEC_I32_R__typed__cache__regression__tests|extra_cmds:= \
        test_name:=register-allocator-tests|src:=tests/unit/test_register_allocator.c|description:=register__allocator__tests|extra_cmds:= \
        test_name:=compiler-specialization-tests|src:=tests/unit/test_compiler_specialization.c|description:=compiler__specialization__regression__tests|extra_cmds:=ORUS_SKIP_SPECIALIZATION_GUARD_TEST=1 \
        test_name:=builtin-input-tests|src:=tests/unit/test_builtin_input.c|description:=builtin__input__tests|extra_cmds:=

C_UNIT_TEST_TARGETS := $(foreach spec,$(C_UNIT_TEST_SPECS),$(call c_unit_test_get,$(spec),test_name))

define c_unit_test_rule
$(call c_unit_test_bin,$(2)): $(2) $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $$(dir $$@)
	@echo "Compiling $(call c_unit_test_desc,$(3))..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $$@ $$^ $(LDFLAGS)
	$(call sign_with_jit,$$@)

$(1): $(call c_unit_test_bin,$(2))
	@echo "Running $(call c_unit_test_desc,$(3))..."
	@$(if $(strip $(4)),$(strip $(4))$(space),)./$(call c_unit_test_bin,$(2))
endef

$(foreach spec,$(C_UNIT_TEST_SPECS),$(eval $(call c_unit_test_rule,$(call c_unit_test_get,$(spec),test_name),$(call c_unit_test_get,$(spec),src),$(call c_unit_test_get,$(spec),description),$(call c_unit_test_get,$(spec),extra_cmds))))

$(CONSTANT_FOLD_TEST_BIN): tests/unit/test_constant_folding.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling constant folding tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	$(call sign_with_jit,$@)

constant-fold-tests: $(CONSTANT_FOLD_TEST_BIN)
	@echo "Running constant folding tests..."
	@./$(CONSTANT_FOLD_TEST_BIN)

$(JIT_BENCHMARK_TEST_BIN): tests/unit/test_vm_jit_benchmark.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling VM JIT benchmark..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	$(call sign_with_jit,$@)

jit-benchmark-tests: $(JIT_BENCHMARK_TEST_BIN)
	@echo "Running VM JIT benchmark..."
	@./$(JIT_BENCHMARK_TEST_BIN)

$(JIT_TRANSLATION_TEST_BIN): tests/unit/test_vm_jit_translation.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling baseline translator tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	$(call sign_with_jit,$@)

jit-translation-tests: $(JIT_TRANSLATION_TEST_BIN)
	@echo "Running baseline translator tests..."
	@./$(JIT_TRANSLATION_TEST_BIN)

$(JIT_BACKEND_TEST_BIN): tests/unit/test_vm_jit_backend.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling baseline backend smoke tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	$(call sign_with_jit,$@)

jit-backend-tests: $(JIT_BACKEND_TEST_BIN)
	@echo "Running baseline backend smoke tests..."
	@./$(JIT_BACKEND_TEST_BIN)

jit-backend-helper-tests: $(JIT_BACKEND_TEST_BIN)
	@echo "Running backend smoke tests via helper stub (cross-arch path)..."
	@ORUS_JIT_FORCE_HELPER_STUB=1 ./$(JIT_BACKEND_TEST_BIN)

$(JIT_CROSS_ARCH_PARITY_TEST_BIN): tests/unit/test_vm_jit_cross_arch.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling cross-architecture parity tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	$(call sign_with_jit,$@)

jit-cross-arch-tests: jit-backend-helper-tests jit-translation-tests $(JIT_CROSS_ARCH_PARITY_TEST_BIN)
	@echo "Running cross-architecture parity harness..."
	@./$(JIT_CROSS_ARCH_PARITY_TEST_BIN)
	@echo "Cross-architecture validation complete."

$(JIT_STRESS_TEST_BIN): tests/unit/test_vm_jit_stress.c $(COMPILER_OBJS) $(VM_OBJS)
	@mkdir -p $(dir $@)
	@echo "Compiling JIT stress tests..."
	@$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	$(call sign_with_jit,$@)

jit-stress-tests: $(JIT_STRESS_TEST_BIN)
	@echo "Running JIT stress harness..."
	@./$(JIT_STRESS_TEST_BIN)

.NOTPARALLEL: unit-test c-unit-tests
c-unit-tests: $(C_UNIT_TEST_TARGETS)
	@echo "C Unit Test Suite complete."

unit-test: c-unit-tests

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
	@$(MAKE) PROFILE=$(TEST_PROFILE) orus-tests
	@echo ""
	@echo "==================================="
	@echo "\033[36m=== C Unit Tests ===\033[0m"
	@$(MAKE) PROFILE=$(TEST_PROFILE) c-unit-tests

orus-tests: $(ORUS)
	@scripts/run_orus_tests.sh "$(ORUS)" "$(TESTDIR)" "$(ORUS_TEST_EXCLUDE_BENCHMARKS)"


.PHONY: jit-benchmark-orus
jit-benchmark-orus: scripts/check_jit_benchmark.py
	@$(MAKE) PROFILE=release orus
	@echo "Running JIT benchmarks with uplift and coverage thresholds..."
	@python3 scripts/check_jit_benchmark.py --binary ./orus --speedup 3.0 --coverage 0.90 \
		tests/benchmarks/optimized_loop_benchmark.orus \
		tests/benchmarks/ffi_ping_pong_benchmark.orus


builtin-input-tests: $(BUILTIN_INPUT_TEST_BIN)
	@echo "Running builtin input tests..."
	@./$(BUILTIN_INPUT_TEST_BIN)

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
	@$(MAKE) PROFILE=ci unit-test

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
	@echo "Installing Orus to $(INSTALL_PREFIX)..."
	@mkdir -p "$(INSTALL_BIN_DIR)"
	@cp orus "$(INSTALL_BIN_DIR)/orus"
	@if [ -f orus-prof ]; then \
		cp orus-prof "$(INSTALL_BIN_DIR)/orus-prof"; \
	fi
	@echo "✓ Orus installed successfully into $(INSTALL_PREFIX)"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -f orus orus_debug orus_profiling orus_ci
	@rm -f orus-prof orus-prof_debug orus-prof_profiling orus-prof_ci
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
	@echo "  install   - Build and install the release binary"
	@echo ""
	@echo "Installation:"
	@echo "  install   - Build the release binary and install it"
	@echo ""
	@echo "Examples:"
	@echo "  make                    - Build debug version (creates orus_debug)"
	@echo "  make release            - Build optimized release version"
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
