/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/compiler/backend/scope_stack.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Implements the scope stack data structure used during backend
 *              transformations.
 */

#include "compiler/scope_stack.h"
#include <stdlib.h>
#include <string.h>

static bool ensure_capacity(ScopeStack* stack, int min_capacity) {
    if (!stack) {
        return false;
    }
    if (stack->capacity >= min_capacity) {
        return true;
    }

    int new_capacity = stack->capacity == 0 ? 8 : stack->capacity * 2;
    while (new_capacity < min_capacity) {
        new_capacity *= 2;
    }

    ScopeFrame* new_frames = realloc(stack->frames, (size_t)new_capacity * sizeof(ScopeFrame));
    if (!new_frames) {
        return false;
    }

    stack->frames = new_frames;
    stack->capacity = new_capacity;
    return true;
}

ScopeStack* scope_stack_create(void) {
    ScopeStack* stack = malloc(sizeof(ScopeStack));
    if (!stack) {
        return NULL;
    }

    stack->frames = NULL;
    stack->count = 0;
    stack->capacity = 0;
    stack->loop_depth = 0;
    return stack;
}

void scope_stack_destroy(ScopeStack* stack) {
    if (!stack) {
        return;
    }

    if (stack->frames) {
        for (int i = 0; i < stack->count; i++) {
            ScopeFrame* frame = &stack->frames[i];
            free(frame->loop_break_statements);
            free(frame->loop_continue_statements);
        }
    }

    free(stack->frames);
    free(stack);
}

ScopeFrame* scope_stack_push(ScopeStack* stack, ScopeKind kind) {
    if (!stack) {
        return NULL;
    }

    if (!ensure_capacity(stack, stack->count + 1)) {
        return NULL;
    }

    ScopeFrame* frame = &stack->frames[stack->count];
    memset(frame, 0, sizeof(ScopeFrame));
    frame->kind = kind;
    frame->lexical_depth = stack->count;
    frame->start_offset = -1;
    frame->end_offset = -1;
    frame->continue_offset = -1;
    frame->loop_id = 0;
    frame->prev_loop_id = 0;
    frame->prev_loop_start = -1;
    frame->prev_loop_end = -1;
    frame->prev_loop_continue = -1;
    frame->saved_break_statements = NULL;
    frame->saved_break_count = 0;
    frame->saved_break_capacity = 0;
    frame->saved_continue_statements = NULL;
    frame->saved_continue_count = 0;
    frame->saved_continue_capacity = 0;
    frame->loop_break_statements = NULL;
    frame->loop_break_count = 0;
    frame->loop_break_capacity = 0;
    frame->loop_continue_statements = NULL;
    frame->loop_continue_count = 0;
    frame->loop_continue_capacity = 0;
    frame->label = NULL;

    stack->count++;

    if (kind == SCOPE_KIND_LOOP) {
        stack->loop_depth++;
    }

    return frame;
}

void scope_stack_pop(ScopeStack* stack) {
    if (!stack || stack->count == 0) {
        return;
    }

    ScopeFrame* frame = &stack->frames[stack->count - 1];
    if (frame->kind == SCOPE_KIND_LOOP && stack->loop_depth > 0) {
        stack->loop_depth--;
    }

    stack->count--;
}

ScopeFrame* scope_stack_current(ScopeStack* stack) {
    if (!stack || stack->count == 0) {
        return NULL;
    }
    return &stack->frames[stack->count - 1];
}

ScopeFrame* scope_stack_current_loop(ScopeStack* stack) {
    if (!stack) {
        return NULL;
    }

    for (int i = stack->count - 1; i >= 0; --i) {
        if (stack->frames[i].kind == SCOPE_KIND_LOOP) {
            return &stack->frames[i];
        }
    }

    return NULL;
}

int scope_stack_depth(const ScopeStack* stack) {
    return stack ? stack->count : 0;
}

int scope_stack_loop_depth(const ScopeStack* stack) {
    return stack ? stack->loop_depth : 0;
}

bool scope_stack_is_in_loop(const ScopeStack* stack) {
    return scope_stack_loop_depth(stack) > 0;
}

ScopeFrame* scope_stack_get_frame(ScopeStack* stack, int index) {
    if (!stack || index < 0 || index >= stack->count) {
        return NULL;
    }
    return &stack->frames[index];
}

ScopeFrame* scope_stack_find_loop_by_label(ScopeStack* stack, const char* label) {
    if (!stack || !label || *label == '\0') {
        return NULL;
    }

    for (int i = stack->count - 1; i >= 0; --i) {
        ScopeFrame* frame = &stack->frames[i];
        if (frame->kind == SCOPE_KIND_LOOP && frame->label && strcmp(frame->label, label) == 0) {
            return frame;
        }
    }

    return NULL;
}
