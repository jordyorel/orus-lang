# Orus Language Refactoring Roadmap

## Overview

This roadmap addresses the architectural improvements needed to transform Orus from a proof-of-concept into a production-ready language implementation. The refactoring focuses on four key areas identified in the codebase analysis:

1. **Global State Reduction** - Eliminate excessive global variables
2. **Function Decomposition** - Break down overly long functions
3. **Configurability Enhancement** - Add runtime and build-time configuration
4. **Unit Testing Infrastructure** - Enable component-level testing

## Current State Assessment

- **Grade**: B+ (Very good organization with room for improvement)
- **Strengths**: Solid foundation, comprehensive integration tests, good performance
- **Critical Issues**: Too much global state, functions too long, limited configurability, hard to unit test

## Phase 1: Global State Reduction (Priority: CRITICAL)

### 1.1 Parser State Refactoring ✅ COMPLETED
**Target**: `src/compiler/parser.c` (Lines 22-94)
**Current Issues**:
- 6+ global variables managing parser state
- Prevents concurrent parsing
- Complicates testing

**Tasks**:
- [x] Create `ParserContext` structure
- [x] Move global parser state into context
- [x] Update all parser functions to use context
- [x] Add context initialization/cleanup

**Completed Impact**: 
- ✅ Enabled concurrent parsing
- ✅ Improved testability (70/70 tests passing)
- ✅ Reduced global dependencies (0 global variables remain)
- ✅ Context-based architecture implemented

### 1.2 Lexer State Refactoring ✅ COMPLETED
**Target**: `src/compiler/lexer.c` (Line 27)
**Current Issues**:
- Global lexer instance prevents reentrant parsing
- Tight coupling with global state

**Tasks**:
- [x] Create `LexerContext` structure
- [x] Move global lexer state into context
- [x] Update tokenization functions
- [x] Add lexer context lifecycle management

**Completed Impact**:
- ✅ Enabled reentrant lexing (multiple contexts supported)
- ✅ Improved parser flexibility (context-based API available)
- ✅ Better error isolation (context-specific error handling)
- ✅ Backward compatibility maintained (all existing APIs preserved)
- ✅ 100% test success rate (70/70 tests passing)

### 1.3 Type System State Refactoring
**Target**: `src/type/type_representation.c` (Lines 17-21)
**Current Issues**:
- Global type arena and cache
- Prevents type system isolation

**Tasks**:
- [ ] Create `TypeContext` structure
- [ ] Move type arena to context
- [ ] Update type inference to use context
- [ ] Add type context lifecycle management

**Expected Impact**:
- Enable type system testing
- Improve memory management
- Better type system isolation

### 1.4 Error System State Refactoring
**Target**: `src/errors/infrastructure/error_infrastructure.c` (Line 26)
**Current Issues**:
- Global error reporting state
- Affects all error handling globally

**Tasks**:
- [ ] Create `ErrorContext` structure
- [ ] Move global error state into context
- [ ] Update error reporting functions
- [ ] Add error context management

**Expected Impact**:
- Enable error system testing
- Improve error isolation
- Better error handling flexibility

## Phase 2: Function Decomposition (Priority: HIGH)

### 2.1 VM Dispatch System Refactoring
**Target**: `src/vm/dispatch/vm_dispatch_*.c` (~2,100 lines each)
**Current Issues**:
- Massive functions containing entire VM execution
- Difficult to debug and maintain
- Code duplication between goto/switch versions

**Tasks**:
- [ ] Create opcode handler functions
- [ ] Extract arithmetic operations
- [ ] Extract memory operations
- [ ] Extract control flow operations
- [ ] Refactor dispatch loop to use handlers
- [ ] Eliminate code duplication

**Expected Impact**:
- Dramatically improved maintainability
- Enable opcode-specific testing
- Reduce code duplication
- Better debugging capabilities

### 2.2 Compiler Expression Handling
**Target**: `src/compiler/compiler.c` `compileExpr()` (373 lines)
**Current Issues**:
- Single function handling all expression types
- Complex type conversion logic embedded

**Tasks**:
- [ ] Extract literal compilation
- [ ] Extract binary operation compilation
- [ ] Extract cast compilation
- [ ] Extract unary operation compilation
- [ ] Create expression-specific handlers

**Expected Impact**:
- Improved code organization
- Better testing granularity
- Easier maintenance

### 2.3 Parser Expression Handling
**Target**: `src/compiler/parser.c` `parsePrimaryExpression()` (259 lines)
**Current Issues**:
- Massive switch statement for all primary expressions
- Complex number parsing logic

**Tasks**:
- [ ] Extract number literal parsing
- [ ] Extract string literal parsing
- [ ] Extract identifier parsing
- [ ] Extract boolean literal parsing
- [ ] Create token-specific parsers

**Expected Impact**:
- Better code organization
- Improved testability
- Easier debugging

## Phase 3: Configurability Enhancement (Priority: MEDIUM)

### 3.1 Configuration System Architecture
**Tasks**:
- [ ] Design configuration structure
- [ ] Implement environment variable support
- [ ] Add command-line configuration options
- [ ] Create configuration file support
- [ ] Add runtime configuration API

**Configuration Areas**:
- VM parameters (registers, memory limits)
- GC settings (thresholds, strategies)
- Parser limits (recursion depth, buffer sizes)
- Error reporting (verbosity, formatting)
- Development tools (debug levels, profiling)

### 3.2 Build System Enhancement
**Tasks**:
- [ ] Add build profiles (debug/release/profiling)
- [ ] Implement conditional compilation
- [ ] Add cross-compilation support
- [ ] Create optimization level configuration
- [ ] Add static analysis integration

## Phase 4: Unit Testing Infrastructure (Priority: MEDIUM)

### 4.1 Testing Framework Integration
**Tasks**:
- [ ] Integrate C testing framework (Unity/cmocka)
- [ ] Create test project structure
- [ ] Add test build configuration
- [ ] Implement test discovery and execution

### 4.2 Component Testing Infrastructure
**Tasks**:
- [ ] Create testable interfaces for components
- [ ] Implement dependency injection patterns
- [ ] Add mocking capabilities
- [ ] Create test fixtures and utilities

### 4.3 Test Coverage and Quality
**Tasks**:
- [ ] Add code coverage measurement
- [ ] Implement property-based testing
- [ ] Add fuzzing infrastructure
- [ ] Create performance micro-benchmarks

## Phase 5: Advanced Improvements (Priority: LOW)

### 5.1 Memory Management Enhancement
**Tasks**:
- [ ] Implement configurable GC strategies
- [ ] Add memory pool management
- [ ] Implement leak detection
- [ ] Add memory profiling tools

### 5.2 Performance Optimization
**Tasks**:
- [ ] Profile-guided optimization
- [ ] Implement register allocation improvements
- [ ] Add instruction-level optimizations
- [ ] Create performance monitoring

### 5.3 Developer Experience
**Tasks**:
- [ ] Enhanced debugging tools
- [ ] Improved error messages
- [ ] Better IDE integration
- [ ] Comprehensive documentation

## Implementation Timeline

### Immediate (1-2 weeks)
- **Phase 1.1**: ✅ Parser state refactoring (COMPLETED)
- **Phase 1.2**: ✅ Lexer state refactoring (COMPLETED)

### Short-term (2-4 weeks)
- **Phase 1.3**: Type system refactoring
- **Phase 1.4**: Error system refactoring
- **Phase 2.1**: VM dispatch refactoring (start)

### Medium-term (1-2 months)
- **Phase 2.1**: VM dispatch refactoring (complete)
- **Phase 2.2**: Compiler expression handling
- **Phase 2.3**: Parser expression handling

### Long-term (2-3 months)
- **Phase 3**: Configuration system
- **Phase 4**: Unit testing infrastructure
- **Phase 5**: Advanced improvements

## Success Metrics

### Phase 1 Success
- [x] Zero global variables in parser (✅ Phase 1.1 COMPLETED)
- [x] Zero global variables in lexer (✅ Phase 1.2 COMPLETED)
- [x] All tests pass after refactoring (✅ 70/70 tests passing)
- [x] No performance regression (✅ Maintained performance)
- [x] Improved test isolation (✅ Context-based architecture)
- [ ] Zero global variables in type system (Phase 1.3 PENDING)
- [ ] Zero global variables in error system (Phase 1.4 PENDING)

### Phase 2 Success
- [ ] No function > 100 lines
- [ ] VM dispatch handlers < 50 lines each
- [ ] Improved maintainability score
- [ ] Better code coverage

### Phase 3 Success
- [ ] Runtime configuration system
- [ ] Environment variable support
- [ ] Build profile support
- [ ] Configurable development tools

### Phase 4 Success
- [ ] Unit test coverage > 80%
- [ ] Component-level testing
- [ ] Mock/stub capabilities
- [ ] Continuous integration

## Risk Mitigation

### Technical Risks
- **Performance regression**: Continuous benchmarking
- **Functionality breaking**: Comprehensive test suite
- **Complexity increase**: Code review and documentation

### Process Risks
- **Scope creep**: Strict phase boundaries
- **Timeline delays**: Incremental delivery
- **Quality issues**: Test-driven development

## Dependencies

### External Dependencies
- C testing framework (Unity/cmocka)
- Build system enhancements
- CI/CD integration tools

### Internal Dependencies
- All tests must pass before proceeding
- Performance benchmarks must be maintained
- Code review approval required

## Conclusion

This roadmap provides a systematic approach to improving the Orus language implementation architecture. By addressing global state, function complexity, configurability, and testing infrastructure, we can transform Orus from a B+ proof-of-concept into an A-grade production-ready language implementation.

The phased approach ensures continuous functionality while making incremental improvements, with clear success metrics and risk mitigation strategies at each stage.