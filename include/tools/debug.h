#ifndef ORUS_DEBUG_H
#define ORUS_DEBUG_H

#include "public/common.h"
#include "vm/vm.h"

// Debug function declarations
void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);
void dumpProfile(void);

#endif