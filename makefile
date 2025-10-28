ZIG ?= zig
PROFILE ?= debug
DISPATCH_MODE ?= auto
PORTABLE ?= 0
STRICT_JIT ?= 0

ZIG_PORTABLE_BOOL := $(if $(filter 1,$(PORTABLE)),true,false)
ZIG_STRICT_JIT_BOOL := $(if $(filter 1,$(STRICT_JIT)),true,false)
ZIG_BUILD_ARGS := -Dprofile=$(PROFILE) -Ddispatch-mode=$(DISPATCH_MODE) -Dportable=$(ZIG_PORTABLE_BOOL) -Dstrict-jit=$(ZIG_STRICT_JIT_BOOL)

.DEFAULT_GOAL := all

.PHONY: all debug release profiling ci install clean test benchmark help jit-benchmark

all:
	@$(ZIG) build $(ZIG_BUILD_ARGS)

debug:
	@$(MAKE) PROFILE=debug all

release:
	@$(MAKE) PROFILE=release all

profiling:
	@$(MAKE) PROFILE=profiling all

ci:
	@$(MAKE) PROFILE=ci all

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
	@echo "  make [PROFILE=...]        # build (default profile=debug)"
	@echo "  make release              # build with release profile"
	@echo "  make profiling            # build with profiling profile"
	@echo "  make ci                   # build with CI profile"
	@echo "  make install              # run 'zig build install'"
	@echo "  make clean                # remove zig build artifacts"
	@echo "  make test                 # forward to 'zig build test'"
	@echo "  make benchmark            # run interpreter benchmark suite"
	@echo "  make jit-benchmark        # run JIT uplift benchmark harness"
	@echo "Environment passthrough: DISPATCH_MODE=auto|goto|switch, PORTABLE=0|1, STRICT_JIT=0|1"
