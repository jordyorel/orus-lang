ZIG ?= zig
DISPATCH_MODE ?= auto
PORTABLE ?= 0
STRICT_JIT ?= 0

ZIG_BUILD_ARGS :=
ifneq ($(DISPATCH_MODE),auto)
ZIG_BUILD_ARGS += -Ddispatch-mode=$(DISPATCH_MODE)
endif
ifneq ($(filter 1 true,$(PORTABLE)),)
ZIG_BUILD_ARGS += -Dportable=true
endif
ifneq ($(filter 1 true,$(STRICT_JIT)),)
ZIG_BUILD_ARGS += -Dstrict-jit=true
endif

.DEFAULT_GOAL := all

.PHONY: all install clean test benchmark help jit-benchmark

all:
	@$(ZIG) build $(ZIG_BUILD_ARGS)

install:
	@$(ZIG) build install $(ZIG_BUILD_ARGS)

clean:
	@$(ZIG) build clean $(ZIG_BUILD_ARGS)

test:
	@$(ZIG) build test $(ZIG_BUILD_ARGS)

benchmark:
	@$(ZIG) build benchmarks $(ZIG_BUILD_ARGS)

jit-benchmark:
	@$(ZIG) build jit-benchmark $(ZIG_BUILD_ARGS)

help:
	@echo "Orus build is powered by zig build"
	@echo "Available make shortcuts:"
	@echo "  make                      # build optimized interpreter"
	@echo "  make install              # run 'zig build install'"
	@echo "  make clean                # remove zig build artifacts"
	@echo "  make test                 # forward to 'zig build test'"
	@echo "  make benchmark            # run interpreter benchmark suite"
	@echo "  make jit-benchmark        # run JIT uplift benchmark harness"
	@echo "Environment passthrough: DISPATCH_MODE=auto|goto|switch, PORTABLE=0|1, STRICT_JIT=0|1"
