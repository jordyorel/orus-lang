# Modern Makefile
# Legacy support for building without CMake

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
          $(SRCDIR)/vm/builtins.c
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

.PHONY: all clean test c-test help format

all: $(ORUS)

# Create build directory if it doesn't exist
$(BUILDDIR):
	mkdir -p $(BUILDDIR) $(BUILDDIR)/vm $(BUILDDIR)/compiler

# Main interpreter
$(ORUS): $(MAIN_OBJ) $(REPL_OBJ) $(VM_OBJS) $(COMPILER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


# Object files - create build directory structure and compile
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# C test executables
$(BUILDDIR)/test_%: $(TESTDIR)/c/test_%.c $(VM_OBJS) $(COMPILER_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(VM_OBJS) $(COMPILER_OBJS) $(LDFLAGS)

# Adjust the test_lexer target to compile source files into object files first
build/test_lexer: build/test_lexer.o build/lexer.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/test_lexer.o: $(TESTDIR)/c/test_lexer.c $(INCDIR)/*
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

build/lexer.o: $(SRCDIR)/compiler/lexer.c $(INCDIR)/*
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Add a target to build and run the test_lexer binary
run_test_lexer: build/test_lexer
	./build/test_lexer

# Run tests on .orus files
test: $(ORUS)
	@echo "Running Orus tests..."
	@passed=0; failed=0; \
	for test_file in $(shell find $(TESTDIR) -name '*.orus' | sort); do \
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
		echo "\033[32mAll $$passed tests passed!\033[0m"; \
	else \
		echo "\033[31m$$failed test(s) failed, $$passed test(s) passed.\033[0m"; \
	fi

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
	@echo "  all      - Build all targets (default)"
	@echo "  orus     - Build main interpreter"
	@echo "  test     - Run tests on .orus files in tests/ directory"
	@echo "  c-test   - Run C unit tests for VM and critical components"
	@echo "  clean    - Remove build artifacts"
	@echo "  format   - Format source code"
	@echo "  help     - Show this help message"