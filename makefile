# Simplified Orus Makefile
# For current working test suite only

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -std=c11
LDFLAGS = -lm

# Dispatch method selection (default: auto-detect)
# Usage: make DISPATCH=switch or make DISPATCH=goto
ifdef DISPATCH
  ifeq ($(DISPATCH),switch)
    CFLAGS += -DUSE_COMPUTED_GOTO=0
  else ifeq ($(DISPATCH),goto)
    CFLAGS += -DUSE_COMPUTED_GOTO=1
  else
    $(error Invalid DISPATCH value: $(DISPATCH). Use 'switch' or 'goto')
  endif
endif

# Directories
SRCDIR = src
INCDIR = include
TESTDIR = tests
BUILDDIR = build

# Include path
INCLUDES = -I$(INCDIR)

# Source files
COMPILER_SRCS = $(SRCDIR)/compiler/compiler.c $(SRCDIR)/compiler/lexer.c $(SRCDIR)/compiler/parser.c $(SRCDIR)/compiler/symbol_table.c
VM_SRCS = $(SRCDIR)/vm/vm.c $(SRCDIR)/vm/memory.c $(SRCDIR)/vm/debug.c $(SRCDIR)/vm/builtins.c $(SRCDIR)/vm/vm_dispatch_switch.c $(SRCDIR)/vm/vm_dispatch_goto.c $(SRCDIR)/type/type_representation.c $(SRCDIR)/error_reporting.c
REPL_SRC = $(SRCDIR)/repl.c
MAIN_SRC = $(SRCDIR)/main.c

# Object files
COMPILER_OBJS = $(COMPILER_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
VM_OBJS = $(VM_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
REPL_OBJ = $(REPL_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
MAIN_OBJ = $(MAIN_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Target
ORUS = orus

.PHONY: all clean test benchmark help

all: $(ORUS)

# Create build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR) $(BUILDDIR)/vm $(BUILDDIR)/compiler $(BUILDDIR)/type

# Main interpreter
$(ORUS): $(MAIN_OBJ) $(REPL_OBJ) $(VM_OBJS) $(COMPILER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Run comprehensive test suite
test: $(ORUS)
	@echo "Running Comprehensive Test Suite..."
	@echo "==================================="
	@passed=0; failed=0; \
	echo ""; \
	echo "\033[36m=== Basic Expression Tests ===\033[0m"; \
	for test_file in $(TESTDIR)/expressions/binary.orus \
	                  $(TESTDIR)/expressions/simple_add.orus \
	                  $(TESTDIR)/expressions/simple_literal.orus; do \
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
	for test_file in $(TESTDIR)/variables/basic_var.orus; do \
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
	echo "\033[36m=== Arithmetic Edge Cases ===\033[0m"; \
	for test_file in $(TESTDIR)/edge_cases/arithmetic_edge_cases.orus \
	                  $(TESTDIR)/edge_cases/operator_precedence.orus \
	                  $(TESTDIR)/edge_cases/boundary_values.orus \
	                  $(TESTDIR)/edge_cases/error_conditions.orus \
	                  $(TESTDIR)/edge_cases/modulo_overflow_test.orus; do \
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
	for test_file in $(TESTDIR)/benchmarks/extreme_benchmark.orus \
	                  $(TESTDIR)/benchmarks/modulo_operations_benchmark.orus; do \
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
	                  $(TESTDIR)/edge_cases/division_by_zero_enhanced.orus; do \
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


# Run cross-language benchmark tests
benchmark: $(ORUS)
	@cd $(TESTDIR)/benchmarks && ./unified_benchmark.sh

# Clean build artifacts
clean:
	rm -f $(ORUS)
	rm -rf $(BUILDDIR)

# Help
help:
	@echo "Available targets:"
	@echo "  all       - Build the Orus interpreter (default)"
	@echo "  test      - Run comprehensive test suite (all tests)"
	@echo "  benchmark - Run cross-language performance benchmarks"
	@echo "  clean     - Remove build artifacts"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Dispatch method selection:"
	@echo "  DISPATCH=switch - Force switch-based dispatch"
	@echo "  DISPATCH=goto   - Force computed goto dispatch"
	@echo "  (default)       - Auto-detect best dispatch method"
	@echo ""
	@echo "Examples:"
	@echo "  make DISPATCH=switch    - Build with switch dispatch"
	@echo "  make benchmark DISPATCH=goto  - Benchmark with goto dispatch"
	@echo "  make test DISPATCH=switch     - Test with switch dispatch"