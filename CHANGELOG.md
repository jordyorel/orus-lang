# Changelog

All notable changes to the Orus programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial project reorganization for scalability
- CMake build system for better cross-platform support
- Comprehensive documentation (architecture, contributing, language spec)
- Example programs demonstrating language features
- Modern build script with Python
- Proper directory structure for large-scale development

### Changed
- Reorganized source code into logical directories
- Updated Makefile for new project structure
- Improved .gitignore for comprehensive coverage

### Removed
- Old flat file structure

## [0.1.0] - 2025-01-XX

### Added
- Register-based virtual machine implementation
- Complete lexer and parser
- AST-based compilation pipeline
- Type system with static typing
- Basic control flow constructs (if, while, for)
- Function declarations and calls
- Array support
- Error handling with try/catch
- Module system basics
- Garbage collector
- Comprehensive test suite

### Features
- 256 registers per call frame
- Multiple numeric types (i32, i64, u32, u64, f64)
- String and boolean types
- Array indexing and operations
- Binary and unary expressions
- Variable declarations and assignments
- Nested scopes and local variables
- Function parameters and return values
- Print statements for output
- Performance optimizations

### Performance
- 2-3x performance improvement over stack-based VMs
- Efficient register allocation
- Reduced instruction count for complex expressions
- Memory-efficient bytecode representation
