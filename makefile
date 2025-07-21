# Enhanced Orus Makefile with Build Profiles
# Supports debug, release, and profiling configurations

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

# Both dispatchers are always compiled and linked
# The runtime will auto-detect the best dispatch method
# No flags needed - both switch and goto dispatchers are included

# Directories
SRCDIR = src
INCDIR = include
TESTDIR = tests
BUILDDIR = build/$(PROFILE)

# Include path
INCLUDES = -I$(INCDIR)

# Source files
COMPILER_SRCS = $(SRCDIR)/compiler/compiler.c $(SRCDIR)/compiler/lexer.c $(SRCDIR)/compiler/parser.c $(SRCDIR)/compiler/symbol_table.c $(SRCDIR)/compiler/loop_optimization.c

VM_SRCS = $(SRCDIR)/vm/core/vm_core.c $(SRCDIR)/vm/runtime/vm.c $(SRCDIR)/vm/core/vm_memory.c $(SRCDIR)/vm/utils/debug.c $(SRCDIR)/vm/runtime/builtins.c $(SRCDIR)/vm/operations/vm_arithmetic.c $(SRCDIR)/vm/operations/vm_control_flow.c $(SRCDIR)/vm/operations/vm_typed_ops.c $(SRCDIR)/vm/operations/vm_string_ops.c $(SRCDIR)/vm/operations/vm_comparison.c $(SRCDIR)/vm/dispatch/vm_dispatch_switch.c $(SRCDIR)/vm/dispatch/vm_dispatch_goto.c $(SRCDIR)/vm/core/vm_validation.c $(SRCDIR)/vm/register_file.c $(SRCDIR)/vm/spill_manager.c $(SRCDIR)/vm/module_manager.c $(SRCDIR)/vm/register_cache.c $(SRCDIR)/type/type_representation.c $(SRCDIR)/errors/infrastructure/error_infrastructure.c $(SRCDIR)/errors/core/error_base.c $(SRCDIR)/errors/features/type_errors.c $(SRCDIR)/errors/features/variable_errors.c $(SRCDIR)/errors/features/control_flow_errors.c $(SRCDIR)/config/config.c

REPL_SRC = $(SRCDIR)/repl.c
MAIN_SRC = $(SRCDIR)/main.c

# Object files (profile-specific)
COMPILER_OBJS = $(COMPILER_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
VM_OBJS = $(VM_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
REPL_OBJ = $(REPL_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
MAIN_OBJ = $(MAIN_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Target (profile-specific)
ORUS = orus$(SUFFIX)

.PHONY: all clean test benchmark help debug release profiling analyze install

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
	@mkdir -p $(BUILDDIR) $(BUILDDIR)/vm/core $(BUILDDIR)/vm/dispatch $(BUILDDIR)/vm/operations $(BUILDDIR)/vm/runtime $(BUILDDIR)/vm/utils $(BUILDDIR)/vm/handlers $(BUILDDIR)/compiler $(BUILDDIR)/type $(BUILDDIR)/errors/core $(BUILDDIR)/errors/features $(BUILDDIR)/errors/infrastructure $(BUILDDIR)/config

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

# Run comprehensive test suite
test: $(ORUS)
	@echo "Running Comprehensive Test Suite..."
	@echo "==================================="
	@passed=0; failed=0; \
	echo ""; \
	echo "\033[36m=== Basic Expression Tests ===\033[0m"; \
	for test_file in $(TESTDIR)/expressions/binary.orus \
	                  $(TESTDIR)/expressions/simple_add.orus \
	                  $(TESTDIR)/expressions/simple_literal.orus \
	                  $(TESTDIR)/expressions/parenthesized_cast_should_parse.orus \
	                  $(TESTDIR)/expressions/comprehensive_parenthesized_casts.orus \
	                  $(TESTDIR)/expressions/unary_operators.orus \
	                  $(TESTDIR)/expressions/unary_comprehensive.orus \
	                  $(TESTDIR)/expressions/unary_edge_cases.orus \
	                  $(TESTDIR)/expressions/ternary_basic.orus \
	                  $(TESTDIR)/expressions/ternary_advanced.orus \
	                  $(TESTDIR)/expressions/ternary_edge_cases.orus \
	                  $(TESTDIR)/expressions/ternary_types.orus \
	                  $(TESTDIR)/expressions/ternary_optimization.orus \
	                  $(TESTDIR)/expressions/ternary_comprehensive.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Variables Tests ===\033[0m"; \
	for test_file in $(TESTDIR)/variables/basic_var.orus \
	                  $(TESTDIR)/variables/multiple_variable_declarations.orus \
	                  $(TESTDIR)/variables/multiple_variable_edge_cases.orus \
	                  $(TESTDIR)/variables/mutable_test.orus \
	                  $(TESTDIR)/variables/compound_assignments_pass.orus \
	                  $(TESTDIR)/variables/basic_scope.orus \
	                  $(TESTDIR)/variables/for_loop_scope.orus \
	                  $(TESTDIR)/variables/while_loop_scope.orus \
	                  $(TESTDIR)/variables/nested_for_scope.orus \
	                  $(TESTDIR)/variables/variable_shadowing.orus \
	                  $(TESTDIR)/variables/scope_variable_lifetime.orus \
	                  $(TESTDIR)/variables/complex_scope_interactions.orus \
	                  $(TESTDIR)/variables/scope_with_control_flow.orus \
	                  $(TESTDIR)/variables/loop_scope_edge_cases.orus \
	                  $(TESTDIR)/variables/step_range_scope.orus \
	                  $(TESTDIR)/variables/if_else_scope.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Control Flow Tests ===\033[0m"; \
	for test_file in $(TESTDIR)/control_flow/basic_if.orus \
	                  $(TESTDIR)/control_flow/if_else.orus \
	                  $(TESTDIR)/control_flow/nested_if.orus \
	                  $(TESTDIR)/control_flow/inline_if.orus \
	                  $(TESTDIR)/control_flow/elif_basic.orus \
	                  $(TESTDIR)/control_flow/elif_multiple.orus \
	                  $(TESTDIR)/control_flow/elif_inline.orus \
	                  $(TESTDIR)/control_flow/elif_nested.orus \
	                  $(TESTDIR)/control_flow/basic_while.orus \
	                  $(TESTDIR)/control_flow/inline_while.orus \
	                  $(TESTDIR)/control_flow/comprehensive_control.orus \
	                  $(TESTDIR)/control_flow/edge_zero_iterations.orus \
	                  $(TESTDIR)/control_flow/edge_complex_conditions.orus \
	                  $(TESTDIR)/control_flow/edge_deep_nesting.orus \
	                  $(TESTDIR)/control_flow/edge_mixed_styles.orus \
	                  $(TESTDIR)/control_flow/optimization_verification.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Loop Control Tests ===\033[0m"; \
	for test_file in $(TESTDIR)/loops/test_for_simple.orus \
	                  $(TESTDIR)/loops/test_for_break.orus \
	                  $(TESTDIR)/loops/test_for_continue.orus \
	                  $(TESTDIR)/loops/test_for_continue_simple.orus \
	                  $(TESTDIR)/loops/test_for_ranges.orus \
	                  $(TESTDIR)/loops/test_step_ranges.orus \
	                  $(TESTDIR)/loops/test_loop_optimization.orus \
	                  $(TESTDIR)/loops/test_licm_optimization.orus \
                  $(TESTDIR)/loops/test_strength_reduction.orus \
	                  $(TESTDIR)/loops/test_while_break.orus \
	                  $(TESTDIR)/loops/test_while_continue.orus \
	                  $(TESTDIR)/loops/test_nested_simple.orus \
	                  $(TESTDIR)/loops/test_nested_loops.orus \
	                  $(TESTDIR)/loops/test_complex_nested.orus \
	                  $(TESTDIR)/loops/test_deep_simple.orus \
	                  $(TESTDIR)/loops/test_deep_nesting.orus \
	                  $(TESTDIR)/loops/test_break_edge_cases.orus \
	                  $(TESTDIR)/loops/test_continue_edge_cases.orus \
	                  $(TESTDIR)/loops/test_auto_mutable_comprehensive.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Literals Tests ===\033[0m"; \
	for test_file in $(TESTDIR)/literals/literal.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Function Tests ===\033[0m"; \
	for test_file in $(TESTDIR)/functions/basic_function.orus \
	                  $(TESTDIR)/functions/simple_add.orus \
	                  $(TESTDIR)/functions/simple_function.orus \
	                  $(TESTDIR)/functions/void_function.orus \
	                  $(TESTDIR)/functions/call_test.orus \
	                  $(TESTDIR)/functions/function_edge_cases.orus \
	                  $(TESTDIR)/functions/just_definition.orus \
	                  $(TESTDIR)/functions/first_class_functions.orus \
	                  $(TESTDIR)/functions/higher_order_functions.orus \
	                  $(TESTDIR)/functions/function_objects.orus \
	                  $(TESTDIR)/functions/nested_function_calls.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Type System Tests ===\033[0m"; \
	for test_file in $(TESTDIR)/types/basic_inference.orus \
	                  $(TESTDIR)/types/annotations.orus \
	                  $(TESTDIR)/types/same_type_arithmetic.orus \
	                  $(TESTDIR)/types/literal_suffixes.orus \
	                  $(TESTDIR)/types/boolean_operations.orus \
	                  $(TESTDIR)/types/string_operations.orus \
	                  $(TESTDIR)/types/type_propagation.orus \
	                  $(TESTDIR)/types/complex_expressions.orus \
	                  $(TESTDIR)/types/float_precision.orus \
                          $(TESTDIR)/types/edge_case_limits.orus \
                          $(TESTDIR)/types/implicit_conversion_test.orus \
                          $(TESTDIR)/types/type_safety_fail.orus \
                          $(TESTDIR)/types/bool_ops_on_int_fail.orus \
                          $(TESTDIR)/types/not_on_int_fail.orus \
                          $(TESTDIR)/types/type_rule_simple_pass.orus \
                          $(TESTDIR)/types/type_rule_complex_pass.orus \
                          $(TESTDIR)/types/type_rule_edge_pass.orus \
                          $(TESTDIR)/types/arithmetic_same_types_v2.orus \
                          $(TESTDIR)/types/explicit_cast_arithmetic.orus \
                          $(TESTDIR)/types/complex_expression_with_casts.orus \
                          $(TESTDIR)/types/cross_type_comparison.orus \
                          $(TESTDIR)/types/valid_string_conversions.orus \
                          $(TESTDIR)/types/valid_numeric_conversions.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Type Safety Tests (Expected to Fail) ===\033[0m"; \
	for test_file in $(TESTDIR)/type_safety_fails/int_float_mix_fail.orus \
	                  $(TESTDIR)/type_safety_fails/signed_unsigned_mix_fail.orus \
	                  $(TESTDIR)/type_safety_fails/different_int_sizes_fail.orus \
	                  $(TESTDIR)/type_safety_fails/bool_arithmetic_fail.orus \
	                  $(TESTDIR)/type_safety_fails/string_arithmetic_fail.orus \
	                  $(TESTDIR)/type_safety_fails/invalid_cast_chain_fail.orus \
                          $(TESTDIR)/type_safety_fails/direct_cast_chain_should_fail.orus \
                          $(TESTDIR)/type_safety_fails/minus_on_bool_fail.orus \
                          $(TESTDIR)/type_safety_fails/type_rule_simple_fail.orus \
                          $(TESTDIR)/type_safety_fails/type_rule_complex_fail.orus \
                          $(TESTDIR)/type_safety_fails/type_rule_edge_fail.orus \
                          $(TESTDIR)/type_safety_fails/mixed_type_operations_fail.orus \
                          $(TESTDIR)/type_safety_fails/implicit_widening_fail.orus \
                          $(TESTDIR)/type_safety_fails/chained_mixed_ops_fail.orus \
                          $(TESTDIR)/type_safety_fails/type_mismatch_string_to_int.orus \
                          $(TESTDIR)/type_safety_fails/type_mismatch_bool_to_float.orus \
                          $(TESTDIR)/type_safety_fails/type_mismatch_float_to_bool.orus \
                          $(TESTDIR)/type_safety_fails/invalid_cast_string_to_int.orus \
                          $(TESTDIR)/type_safety_fails/invalid_cast_string_to_bool.orus \
                          $(TESTDIR)/type_safety_fails/invalid_cast_string_to_float.orus \
                          $(TESTDIR)/type_safety_fails/mixed_arithmetic_int_float.orus \
                          $(TESTDIR)/type_safety_fails/mixed_arithmetic_signed_unsigned.orus \
                          $(TESTDIR)/type_safety_fails/mixed_arithmetic_different_sizes.orus \
                          $(TESTDIR)/type_safety_fails/undefined_type_cast.orus \
                          $(TESTDIR)/type_safety_fails/complex_mixed_operations.orus \
                          $(TESTDIR)/type_safety_fails/chain_cast_with_error.orus \
                          $(TESTDIR)/type_safety_fails/control_flow_non_bool_condition.orus \
                          $(TESTDIR)/type_safety_fails/control_flow_string_condition.orus \
                          $(TESTDIR)/type_safety_fails/control_flow_float_condition.orus \
                          $(TESTDIR)/type_safety_fails/immutable_assignment_fail.orus \
                          $(TESTDIR)/type_safety_fails/compound_assignment_immutable_fail.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[31mUNEXPECTED PASS\033[0m\n"; \
				failed=$$((failed + 1)); \
			else \
				printf "\033[32mCORRECT FAIL\033[0m\n"; \
				passed=$$((passed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Arithmetic Edge Cases ===\033[0m"; \
	for test_file in $(TESTDIR)/edge_cases/arithmetic_edge_cases.orus \
	                  $(TESTDIR)/edge_cases/operator_precedence.orus \
	                  $(TESTDIR)/edge_cases/boundary_values.orus \
                          $(TESTDIR)/edge_cases/error_conditions.orus \
                          $(TESTDIR)/edge_cases/modulo_overflow_test.orus \
                          $(TESTDIR)/edge_cases/overflow_i32_plus_one.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Variable Edge Cases ===\033[0m"; \
	for test_file in $(TESTDIR)/edge_cases/variable_edge_cases.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Literal Edge Cases ===\033[0m"; \
	for test_file in $(TESTDIR)/edge_cases/literal_edge_cases.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Print Edge Cases ===\033[0m"; \
	for test_file in $(TESTDIR)/edge_cases/print_edge_cases.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Expression Nesting ===\033[0m"; \
	for test_file in $(TESTDIR)/edge_cases/expression_nesting.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Benchmark Tests ===\033[0m"; \
	for test_file in $(TESTDIR)/benchmarks/arithmetic_benchmark.orus \
	                  $(TESTDIR)/benchmarks/extreme_benchmark.orus \
	                  $(TESTDIR)/benchmarks/modulo_operations_benchmark.orus \
	                  $(TESTDIR)/benchmarks/loop_optimization_benchmark.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "\033[36m=== Division by Zero Tests (Expected to Fail) ===\033[0m"; \
	for test_file in $(TESTDIR)/edge_cases/modulo_by_zero_test.orus \
	                  $(TESTDIR)/edge_cases/large_number_modulo_by_zero.orus \
                          $(TESTDIR)/edge_cases/expression_modulo_by_zero.orus \
                          $(TESTDIR)/edge_cases/division_by_zero_enhanced.orus \
                          $(TESTDIR)/edge_cases/division_by_zero_runtime.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "Testing: $$test_file ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "\033[31mUNEXPECTED PASS\033[0m\n"; \
				failed=$$((failed + 1)); \
			else \
				printf "\033[32mCORRECT FAIL\033[0m\n"; \
				passed=$$((passed + 1)); \
			fi; \
		fi; \
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

# CI test target: Build with warnings as errors and run full test suite
ci-test: 
	@echo "Building with CI profile (warnings as errors)..."
	@$(MAKE) clean
	@$(MAKE) PROFILE=ci
	@echo ""
	@echo "Running tests with CI build..."
	@$(MAKE) test ORUS=orus_ci

# Run cross-language benchmark tests
benchmark: $(ORUS)
	@cd $(TESTDIR)/benchmarks && ./unified_benchmark.sh

# Run loop optimization performance benchmark
benchmark-loops: $(ORUS)
	@./benchmark_loop_optimization.sh

# Run integration tests
integration-test: $(ORUS)
	@./scripts/run_integration_tests.sh

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
	@rm -f orus orus_debug orus_profiling
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