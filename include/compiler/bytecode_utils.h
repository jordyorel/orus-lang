// Author: Jordy Orel KONDA
// Copyright (c) 2024 Orus Language Project
// Description: Shared helpers for reasoning about bytecode instruction layout.

#ifndef ORUS_COMPILER_BYTECODE_UTILS_H
#define ORUS_COMPILER_BYTECODE_UTILS_H

#include <stddef.h>
#include <stdint.h>

#include "compiler/compiler.h"

size_t bytecode_prefix_size(uint8_t opcode);
size_t bytecode_operand_size(uint8_t opcode);
size_t bytecode_instruction_length(const BytecodeBuffer* buffer, size_t offset);

#endif /* ORUS_COMPILER_BYTECODE_UTILS_H */
