# Modern Makefile
# Legacy support for building without CMake

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -std=c11
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
COMPILER_SRCS =
VM_SRCS = $(SRCDIR)/vm/vm.c
MAIN_SRC = $(SRCDIR)/main.c

# Object files
CORE_OBJS = $(CORE_SRCS:.c=.o)
COMPILER_OBJS = $(COMPILER_SRCS:.c=.o)
VM_OBJS = $(VM_SRCS:.c=.o)
MAIN_OBJ = $(MAIN_SRC:.c=.o)

# Test files
TEST_REGISTER_SRC = $(TESTDIR)/test_register.c

# Targets
ORUS = orus
TEST_REGISTER = test-register


.PHONY: all clean test help format

all: $(ORUS) $(TEST_REGISTER)

# Main interpreter
$(ORUS): $(MAIN_OBJ) $(VM_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Test executables
$(TEST_REGISTER): $(TEST_REGISTER_SRC) $(VM_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)


# Object files
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Run tests
test: $(TEST_REGISTER)
	@echo "Running register VM tests..."
	./$(TEST_REGISTER)

# Clean build artifacts
clean:
	rm -f $(ORUS) $(TEST_REGISTER)
	rm -f $(VM_OBJS) $(MAIN_OBJ)
	rm -f *.o
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
	@echo "  test     - Build and run tests"
	@echo "  clean    - Remove build artifacts"
	@echo "  format   - Format source code"
	@echo "  help     - Show this help message"