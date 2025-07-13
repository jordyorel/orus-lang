# VM Refactoring Roadmap

This roadmap tracks progress of the virtual machine refactoring work. It follows the phases outlined in the *VM Code Refactoring Agent Task* specification.

## Phase 1 – Code Organization Restructuring
- [x] Split core initialization into **vm_core.c**
- [x] Move arithmetic helpers into **vm_arithmetic.c**
- [x] Move control flow operations into **vm_control_flow.c**
- [x] Move memory operations into **vm_memory.c**
- [x] Move typed register ops into **vm_typed_ops.c**
- [ ] Move comparison ops into **vm_comparison.c**
- [x] Create **vm_string_ops.c** with string builder
- [x] Create central **vm_dispatch.c** including all modules
- [x] Introduce **vm_internal.h** for shared macros

## Phase 2 – Constants and Configuration
- [x] Created **vm_constants.h** with shared limits
- [ ] Replace all magic numbers with named constants

## Phase 3 – Error Handling Standardization
- [x] Added error handling macros in **vm_internal.h**
- [x] Apply macros across VM code

## Phase 4 – String Operations Optimization
- [x] Added `StringBuilder` implementation
- [x] Introduce rope data structure and interning

## Phase 5 – Performance and Maintainability Enhancements
- [ ] Add profiling infrastructure
- [ ] Add validation functions
- [ ] Improve documentation for public APIs

This file will be updated as tasks are completed.
