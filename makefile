# Orus Language Makefile
# Standard build: make
# Run tests: make test

CC = gcc
CFLAGS = -Wall -Wextra -O0 -g -std=c11 -DUSE_COMPUTED_GOTO=1
LDFLAGS = -lm

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build
TESTDIR = tests

# Include path
INCLUDES = -I$(INCDIR)

# Core source files
CORE_SRCS = \
	$(SRCDIR)/main.c \
	$(SRCDIR)/repl.c \
	$(SRCDIR)/vm/vm.c \
	$(SRCDIR)/vm/memory.c \
	$(SRCDIR)/vm/debug.c \
	$(SRCDIR)/vm/builtins.c \
	$(SRCDIR)/vm/vm_dispatch_goto.c \
	$(SRCDIR)/compiler/compiler.c \
	$(SRCDIR)/compiler/lexer.c \
	$(SRCDIR)/compiler/parser.c \
	$(SRCDIR)/compiler/symbol_table.c \
	$(SRCDIR)/compiler/scope_analysis.c \
	$(SRCDIR)/type/type_representation.c \
	$(SRCDIR)/type/type_inference.c

# Object files
CORE_OBJS = $(CORE_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Target
ORUS = orus

# Default target
all: $(ORUS)

# Create build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR) $(BUILDDIR)/vm $(BUILDDIR)/compiler $(BUILDDIR)/type

# Main target
$(ORUS): $(CORE_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Object file compilation
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test target - run only working tests by category
test: $(ORUS)
	@echo "🧪 Running Orus Test Suite"
	@echo "=========================="
	@passed=0; failed=0; \
	echo ""; \
	echo "📄 Testing Literals..."; \
	for test_file in $(TESTDIR)/literals/*.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "  Testing: $$(basename $$test_file) ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "✅ PASS\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "❌ FAIL\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "➕ Testing Expressions..."; \
	for test_file in $(TESTDIR)/expressions/binary.orus $(TESTDIR)/expressions/boolean.orus; do \
		if [ -f "$$test_file" ]; then \
			printf "  Testing: $$(basename $$test_file) ... "; \
			if ./$(ORUS) "$$test_file" >/dev/null 2>&1; then \
				printf "✅ PASS\n"; \
				passed=$$((passed + 1)); \
			else \
				printf "❌ FAIL\n"; \
				failed=$$((failed + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "========================"; \
	echo "📊 Test Results:"; \
	echo "  ✅ Passed: $$passed"; \
	echo "  ❌ Failed: $$failed"; \
	echo "  📈 Total:  $$((passed + failed))"; \
	if [ $$failed -eq 0 ]; then \
		echo ""; \
		echo "🎉 All tests passed!"; \
		echo "✨ Current working features:"; \
		echo "   - Basic literal values (numbers)"; \
		echo "   - Binary arithmetic expressions (+, -, *, /)"; \
		echo "   - Boolean expressions (true, false)"; \
		echo "   - Print statements"; \
	else \
		echo ""; \
		echo "⚠️  Some tests failed. Run individual tests for details."; \
	fi

# Individual test categories for debugging
test-literals: $(ORUS)
	@echo "Testing literals..."
	@./$(ORUS) $(TESTDIR)/literals/literal.orus

test-expressions: $(ORUS)  
	@echo "Testing expressions..."
	@./$(ORUS) $(TESTDIR)/expressions/binary.orus
	@./$(ORUS) $(TESTDIR)/expressions/boolean.orus

# Clean
clean:
	rm -f $(ORUS)
	rm -rf $(BUILDDIR)

# Help
help:
	@echo "Orus Language Build System"
	@echo "========================="
	@echo "Available targets:"
	@echo "  make         - Build the Orus interpreter (default)"
	@echo "  make test    - Run all working tests by category"
	@echo "  make clean   - Remove build artifacts"
	@echo ""
	@echo "Individual test categories:"
	@echo "  make test-literals    - Test literal parsing"
	@echo "  make test-expressions - Test basic expressions" 
	@echo ""
	@echo "Usage: ./orus <file.orus> to run individual files"
	@echo ""
	@echo "Current Status: ✅ Basic interpreter working"
	@echo "  - Literals and expressions are functional"
	@echo "  - Print statements work"
	@echo "  - Ready for incremental feature development"

.PHONY: all test clean help test-literals test-expressions