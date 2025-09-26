/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/public/common.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Public-facing definitions, macros, and shared types exported by the
 *              Orus SDK.
 */

// common.h - Common definitions and utilities for Orus Language
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Common exit codes
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define EXIT_COMPILE_ERROR 65
#define EXIT_RUNTIME_ERROR 70
#define EXIT_USAGE_ERROR 64

// Debug configuration
#ifdef DEBUG
// #define DEBUG_PRINT_CODE
// #define DEBUG_TRACE_EXECUTION
#endif

// Utility macros
#define UNUSED(x) ((void)(x))

// AGENTS.md Performance: Branch prediction hints for optimal CPU pipeline usage
#ifdef __GNUC__
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

#endif // COMMON_H
