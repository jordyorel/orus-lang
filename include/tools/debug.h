// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/tools/debug.h
// Author: Jordy Orel KONDA
// Copyright (c) 2022 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares developer debugging utilities and instrumentation hooks.


#ifndef ORUS_DEBUG_H
#define ORUS_DEBUG_H

#include "public/common.h"
#include "vm/vm.h"

// Debug function declarations
void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);
void dumpProfile(void);

#endif