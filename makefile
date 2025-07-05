# Modern Makefile
# Legacy suppor# Test files
C_TEST_SRCS = $(wildcard $(TESTDIR)/c/unit_tests/*.c) $(wildcard $(TESTDIR)/c/integration_tests/*.c)
C_TEST_TARGETS = $(C_TEST_SRCS:$(TESTDIR)/c/%.c=$(BUILDDIR)/test_%)or building without CMake

CC = gcc
# Enable performance optimizations by default
CFLAGS = -Wall -Wextra -O2 -g -std=c11 -DUSE_COMPUTED_GOTO=1 -DUSE_FAST_ARITH=1
LDFLAGS = -lm

# Directories
SRCDIR = src
INCDIR = include
TESTDIR = tests
BUILDDIR = build

# Include path
INCLUDES = -I$(INCDIR)

# Source files
CORE_SRCS =
COMPILER_SRCS = $(SRCDIR)/compiler/compiler.c $(SRCDIR)/compiler/lexer.c $(SRCDIR)/compiler/parser.c
VM_SRCS = $(SRCDIR)/vm/vm.c $(SRCDIR)/vm/memory.c $(SRCDIR)/vm/debug.c \
          $(SRCDIR)/vm/builtins.c $(SRCDIR)/type/type_representation.c \
          $(SRCDIR)/type/type_inference.c
REPL_SRC = $(SRCDIR)/repl.c
MAIN_SRC = $(SRCDIR)/main.c

# Object files (in build directory)
CORE_OBJS = $(CORE_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
COMPILER_OBJS = $(COMPILER_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
VM_OBJS = $(VM_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
REPL_OBJ = $(REPL_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
MAIN_OBJ = $(MAIN_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Test files
C_TEST_SRCS = $(wildcard $(TESTDIR)/c/*.c)
C_TEST_TARGETS = $(C_TEST_SRCS:$(TESTDIR)/c/%.c=$(BUILDDIR)/%)

# Targets
ORUS = orus

.PHONY: all clean test test-verbose test-basic test-types test-all c-test benchmark help format

all: $(ORUS)

# Create build directory if it doesn't exist
$(BUILDDIR):
	mkdir -p $(BUILDDIR) $(BUILDDIR)/vm $(BUILDDIR)/compiler $(BUILDDIR)/type

# Main interpreter
$(ORUS): $(MAIN_OBJ) $(REPL_OBJ) $(VM_OBJS) $(COMPILER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


# Object files - create build directory structure and compile
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# C test executables
$(BUILDDIR)/test_%: $(TESTDIR)/c/unit_tests/test_%.c $(VM_OBJS) $(COMPILER_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(VM_OBJS) $(COMPILER_OBJS) $(LDFLAGS)

$(BUILDDIR)/test_%: $(TESTDIR)/c/integration_tests/test_%.c $(VM_OBJS) $(COMPILER_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(VM_OBJS) $(COMPILER_OBJS) $(LDFLAGS)

# Adjust the test_lexer target to compile source files into object files first
build/test_lexer: build/test_lexer.o build/lexer.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_lexer.o: $(TESTDIR)/c/test_lexer.c $(INCDIR)/*
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

build/lexer.o: $(SRCDIR)/compiler/lexer.c $(INCDIR)/*
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Run tests on .orus files
test: $(ORUS)
	@echo "Running Orus Test Suite..."
	@echo "=========================="
	@passed=0; failed=0; \
	echo ""; \
	echo "\033[36m=== Type System Tests ===\033[0m"; \
	for test_file in $(shell find $(TESTDIR)/types -name '*.orus' | sort); do \
		printf "Testing: $$test_file ... "; \
		case "$$test_file" in \
			*/errors/u32_*|*/errors/u64_*|*division_by_zero*|*modulo_by_zero*) \
				if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
					printf "\033[31mFAIL (should have failed)\033[0m\n"; \
					failed=$$((failed + 1)); \
				else \
					printf "\033[32mPASS (expected failure)\033[0m\n"; \
					passed=$$((passed + 1)); \
				fi; \
				;; \
			*) \
				if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
					printf "\033[32mPASS\033[0m\n"; \
					passed=$$((passed + 1)); \
				else \
					printf "\033[31mFAIL\033[0m\n"; \
					failed=$$((failed + 1)); \
				fi; \
				;; \
		esac; \
	done; \
	echo ""; \
	echo "\033[36m=== Conditionals Tests ===\033[0m"; \
	for test_file in $(shell find $(TESTDIR)/conditionals -name '*.orus' | sort); do \
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
	echo "\033[36m=== Control Flow Tests ===\033[0m"; \
	for test_file in $(shell find $(TESTDIR)/control_flow -name '*.orus' | sort); do \
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
	echo "\033[36m=== Expressions Tests ===\033[0m"; \
	for test_file in $(shell find $(TESTDIR)/expressions -name '*.orus' | sort); do \
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
	echo "\033[36m=== Variables Tests ===\033[0m"; \
	for test_file in $(shell find $(TESTDIR)/variables -name '*.orus' | sort); do \
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
	echo "\033[36m=== Literals Tests ===\033[0m"; \
	for test_file in $(shell find $(TESTDIR)/literals -name '*.orus' | sort); do \
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
	echo "\033[36m=== Formatting Tests ===\033[0m"; \
	for test_file in $(shell find $(TESTDIR)/formatting -name '*.orus' | sort); do \
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
	echo "\033[36m=== Edge Cases Tests ===\033[0m"; \
	for test_file in $(shell find $(TESTDIR)/edge_cases -name '*.orus' | sort); do \
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
	echo "========================"; \
	echo "\033[36m=== Test Summary ===\033[0m"; \
	if [ $$failed -eq 0 ]; then \
		echo "\033[32m✓ All $$passed tests passed!\033[0m"; \
	else \
		echo "\033[31m✗ $$failed test(s) failed, $$passed test(s) passed.\033[0m"; \
		echo "Run 'make test-verbose' to see detailed error output."; \
	fi; \
	echo ""

# Run C unit tests
c-test: $(C_TEST_TARGETS)
	@echo "Running C unit tests..."
	@passed=0; failed=0; \
	for test_exe in $(C_TEST_TARGETS); do \
		if [ -f "$$test_exe" ]; then \
			printf "Running: $$test_exe ... "; \
			if "$$test_exe" >/dev/null 2>&1; then \
				printf "\033[32mPASS\033[0m\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "\033[31mFAIL\033[0m\n"; \
				printf "  Run '$$test_exe' manually to see details\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
		echo "\033[32mAll $$passed C tests passed!\033[0m"; \
	else \
		echo "\033[31m$$failed C test(s) failed, $$passed C test(s) passed.\033[0m"; \
	fi

# Run tests with verbose output (shows errors)
test-verbose: $(ORUS)
	@echo "Running Orus tests with verbose output..."
	@passed=0; failed=0; \
	for test_file in $(shell find $(TESTDIR) -name '*.orus' | sort); do \
		printf "Testing: $$test_file ... "; \
		if ./$(ORUS) "$$test_file" 2>&1; then \
			printf "\033[32mPASS\033[0m\n"; \
			passed=$$((passed + 1)); \
		else \
			printf "\033[31mFAIL\033[0m\n"; \
			echo "Error output for $$test_file:"; \
			./$(ORUS) "$$test_file" 2>&1 || true; \
			echo ""; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "========================"; \
	if [ $$failed -eq 0 ]; then \
		echo "\033[32mAll $$passed tests passed!\033[0m"; \
	else \
		echo "\033[31m$$failed test(s) failed, $$passed test(s) passed.\033[0m"; \
	fi

# Run comprehensive tests (all tests + C tests)
test-all: test c-test
	@echo "\033[36m=== Comprehensive Test Suite Complete ===\033[0m"

# Run all benchmarks
benchmark: $(ORUS)
	@cd tests/benchmarks && ./run_all_benchmarks.sh

# Clean build artifacts
clean:
	rm -f $(ORUS)
	rm -rf $(BUILDDIR)

# Format code
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		find $(SRCDIR) $(INCDIR) $(TESTDIR) -name "*.c" -o -name "*.h" | xargs clang-format -i; \
		echo "Code formatted."; \
	else \
		echo "clang-format not found. Please install it to format code."; \
	fi

# Help
help:
	@echo "Available targets:"
	@echo "  all         - Build all targets (default)"
	@echo "  orus        - Build main interpreter"
	@echo "  test        - Run all organized tests by category"
	@echo "  test-verbose - Run tests with detailed error output"
	@echo "  test-basic  - Run only basic tests (quick smoke test)"
	@echo "  test-types  - Run only type system tests"
	@echo "  test-all    - Run comprehensive test suite (all tests + C tests)"
	@echo "  c-test      - Run C unit tests for VM and critical components"
	@echo "  benchmark   - Run all benchmarks (Orus native + cross-language comparisons)"
	@echo "  clean       - Remove build artifacts"
	@echo "  format      - Format source code"
	@echo "  help        - Show this help message"