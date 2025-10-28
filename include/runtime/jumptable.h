//  Orus Language Project

#ifndef ORUS_JUMPTABLE_H
#define ORUS_JUMPTABLE_H

#include "internal/intvec.h"

typedef struct {
    IntVec offsets;
} JumpTable;

static inline JumpTable jumptable_new() {
    JumpTable jt;
    jt.offsets = intvec_new();
    return jt;
}

static inline void jumptable_free(JumpTable* jt) {
    intvec_free(&jt->offsets);
}

static inline void jumptable_add(JumpTable* jt, int offset) {
    intvec_push(&jt->offsets, offset);
}

#endif // ORUS_JUMPTABLE_H
