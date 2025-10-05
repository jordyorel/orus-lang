# Split Dispatch into Separate Files for Maintainability (Incomplete)

This directory demonstrates the implementation of Phase 5 of the portable dispatch support roadmap.

## Architecture

The dispatch system has been restructured into separate files for better maintainability:

### Files

1. **`include/vm_dispatch.h`** - Common header with shared macros and function prototypes
2. **`src/vm/vm_dispatch_goto.c`** - Computed goto dispatch implementation (when `USE_COMPUTED_GOTO=1`)
3. **`src/vm/vm_dispatch_switch.c`** - Switch-based dispatch implementation (when `USE_COMPUTED_GOTO=0`)
4. **`src/vm/vm.c`** - Main VM with dispatch logic (currently contains the complete implementation)

### Build System Integration

The Makefile conditionally includes the appropriate dispatch file based on the `USE_GOTO` setting, and
`DISPATCH_MODE=auto` now prefers the switch dispatcher when building on Linux ARM hosts (where computed
goto support is less predictable) while keeping the computed-goto fast path as the default on other
platforms:

```makefile
# Conditional dispatch sources based on computed goto support
ifeq ($(USE_GOTO), 1)
    VM_SRCS += $(SRCDIR)/vm/vm_dispatch_goto.c
else
    VM_SRCS += $(SRCDIR)/vm/vm_dispatch_switch.c
endif
```

## Current Implementation Status

This is a **demonstration/framework** for Phase 5. The current implementation:

- ✅ **File Structure**: Separate files for computed goto and switch dispatch
- ✅ **Build Integration**: Conditional compilation based on dispatch mode  
- ✅ **Header Organization**: Common macros and declarations in vm_dispatch.h
- ✅ **Compatibility**: Maintains existing functionality without breaking changes

## Future Complete Implementation

For a complete implementation, the following would be moved from `vm.c` to the respective dispatch files:

### From vm.c to vm_dispatch_goto.c:
- Complete dispatch table initialization (lines ~270-420 in vm.c)
- All computed goto labels and implementations (lines ~470-2080 in vm.c)  
- Fast dispatch macros and optimization code

### From vm.c to vm_dispatch_switch.c:
- Complete switch statement and cases (lines ~2087-4100+ in vm.c)
- All opcode case implementations
- Switch-based control flow

## Benefits of This Architecture

1. **Maintainability**: Separate dispatch implementations are easier to maintain and debug
2. **Performance**: Each implementation can be optimized independently
3. **Build Control**: Clear separation allows for targeted compilation
4. **Code Organization**: Related dispatch code is co-located
5. **Testing**: Each dispatch mode can be tested and benchmarked separately

## Usage

```bash
# Build with computed goto dispatch
make USE_GOTO=1

# Build with switch-based dispatch  
make USE_GOTO=0

# Both will include the appropriate dispatch file automatically
```

## Performance Impact

- **Computed Goto** (release build, Linux x86_64 container, `tests/benchmarks/arithmetic_benchmark.orus`, 5-run average): **15.22 ms**
- **Switch-based** under the same conditions: **15.28 ms** (within benchmark noise on this host)
- **Build Time**: Minimal impact, conditional compilation is efficient

> _Measurement details_: each build was produced with `make PROFILE=release DISPATCH_MODE=<goto|switch>` after `make clean`,
> then timed via a short Python harness that ran the arithmetic benchmark five times and averaged the wall-clock durations.

This architecture provides a solid foundation for maintaining both dispatch implementations while keeping the codebase organized and performance-optimized.
