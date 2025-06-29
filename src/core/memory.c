/**
 * @file memory.c
 * @brief Memory management and garbage collector.
 */
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "vm.h"
#include "ast.h"
#include "type.h"

#define GC_HEAP_GROW_FACTOR 2

/**
 * Resize a memory allocation similar to realloc.
 *
 * @param pointer Existing allocation.
 * @param oldSize Previous size in bytes.
 * @param newSize New desired size in bytes.
 * @return        Pointer to reallocated memory or NULL when newSize is 0.
 */
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

extern VM vm;

static size_t gcThreshold = 1024 * 1024;

/**
 * Allocate a new garbage-collected object and link it into the VM.
 *
 * @param size Size of the object.
 * @param type Object type tag.
 * @return     Pointer to the allocated object.
 */
static void* allocateObject(size_t size, ObjType type) {
    vm.bytesAllocated += size;
    if (!vm.gcPaused && vm.bytesAllocated > gcThreshold) {
        collectGarbage();
        gcThreshold = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
    }

    Obj* object = (Obj*)malloc(size);
    object->type = type;
    object->marked = false;
    object->next = vm.objects;
    vm.objects = object;
    return object;
}

/**
 * Allocate a new ObjString containing the given characters.
 *
 * @param chars  UTF-8 text.
 * @param length Number of bytes.
 * @return       Newly allocated string object.
 */
ObjString* allocateString(const char* chars, int length) {
    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
    string->length = length;
    vm.bytesAllocated += length + 1;
    string->chars = (char*)malloc(length + 1);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    return string;
}

/**
 * Allocate an array object capable of holding the given number of elements.
 *
 * @param length Initial element count.
 * @return       Newly allocated array object.
 */
ObjArray* allocateArray(int length) {
    ObjArray* array = (ObjArray*)allocateObject(sizeof(ObjArray), OBJ_ARRAY);
    array->length = length;
    array->capacity = length > 0 ? length : 8;
    vm.bytesAllocated += sizeof(Value) * array->capacity;
    array->elements = (Value*)malloc(sizeof(Value) * array->capacity);
    return array;
}

/**
 * Allocate an integer array object.
 *
 * @param length Number of elements.
 * @return       Newly allocated integer array.
 */
ObjIntArray* allocateIntArray(int length) {
    ObjIntArray* array = (ObjIntArray*)allocateObject(sizeof(ObjIntArray), OBJ_INT_ARRAY);
    array->length = length;
    vm.bytesAllocated += sizeof(int64_t) * length;
    array->elements = (int64_t*)malloc(sizeof(int64_t) * length);
    return array;
}

ObjRangeIterator* allocateRangeIterator(int64_t start, int64_t end) {
    ObjRangeIterator* it =
        (ObjRangeIterator*)allocateObject(sizeof(ObjRangeIterator),
                                          OBJ_RANGE_ITERATOR);
    it->current = start;
    it->end = end;
    return it;
}


/**
 * Allocate a runtime error object with message and location.
 *
 * @param type     Kind of error.
 * @param message  Error message text.
 * @param location Source location information.
 * @return         Newly allocated error object.
 */
ObjError* allocateError(ErrorType type, const char* message, SrcLocation location) {
    ObjError* err = (ObjError*)allocateObject(sizeof(ObjError), OBJ_ERROR);
    err->type = type;
    err->message = allocateString(message, (int)strlen(message));
    err->location = location;
    return err;
}

/**
 * Allocate an empty AST node object.
 */
ASTNode* allocateASTNode() {
    ASTNode* node = (ASTNode*)allocateObject(sizeof(ASTNode), OBJ_AST);
    memset(node, 0, sizeof(ASTNode));
    return node;
}

/**
 * Allocate a Type descriptor object.
 */
Type* allocateType() {
    return (Type*)allocateObject(sizeof(Type), OBJ_TYPE);
}

void markObject(Obj* object);
static void freeObject(Obj* object);

/**
 * Mark a value during garbage collection.
 *
 * @param value The value to mark.
 */
void markValue(Value value) {
    if (IS_STRING(value)) {
        markObject((Obj*)AS_STRING(value));
    } else if (IS_ARRAY(value)) {
        markObject((Obj*)AS_ARRAY(value));
    } else if (IS_ERROR(value)) {
        markObject((Obj*)AS_ERROR(value));
    }
}

/**
 * Mark an object and any objects it references.
 *
 * @param object Object to mark.
 */
void markObject(Obj* object) {
    if (object == NULL || object->marked) return;
    object->marked = true;

    switch (object->type) {
        case OBJ_STRING:
            // Strings do not reference other objects
            break;
        case OBJ_ARRAY: {
            ObjArray* array = (ObjArray*)object;
            for (int i = 0; i < array->length; i++) {
                markValue(array->elements[i]);
            }
            break;
        }
        case OBJ_INT_ARRAY: {
            // Int arrays do not reference other objects
            break;
        }
        case OBJ_ERROR: {
            ObjError* err = (ObjError*)object;
            markObject((Obj*)err->message);
            break;
        }
        case OBJ_RANGE_ITERATOR: {
            // Range iterators have no referenced objects
            break;
        }
        case OBJ_AST: {
            ASTNode* node = (ASTNode*)object;
            if (node->left) markObject((Obj*)node->left);
            if (node->right) markObject((Obj*)node->right);
            if (node->next) markObject((Obj*)node->next);

            switch (node->type) {
                case AST_LITERAL:
                    markValue(node->data.literal);
                    break;
                case AST_LET:
                    if (node->data.let.initializer) markObject((Obj*)node->data.let.initializer);
                    if (node->data.let.type) markObject((Obj*)node->data.let.type);
                    break;
                case AST_PRINT:
                    if (node->data.print.format) markObject((Obj*)node->data.print.format);
                    if (node->data.print.arguments) markObject((Obj*)node->data.print.arguments);
                    break;
                case AST_IF:
                    if (node->data.ifStmt.condition) markObject((Obj*)node->data.ifStmt.condition);
                    if (node->data.ifStmt.thenBranch) markObject((Obj*)node->data.ifStmt.thenBranch);
                    if (node->data.ifStmt.elifConditions) markObject((Obj*)node->data.ifStmt.elifConditions);
                    if (node->data.ifStmt.elifBranches) markObject((Obj*)node->data.ifStmt.elifBranches);
                    if (node->data.ifStmt.elseBranch) markObject((Obj*)node->data.ifStmt.elseBranch);
                    break;
                case AST_BLOCK:
                    if (node->data.block.statements) markObject((Obj*)node->data.block.statements);
                    break;
                case AST_WHILE:
                    if (node->data.whileStmt.condition) markObject((Obj*)node->data.whileStmt.condition);
                    if (node->data.whileStmt.body) markObject((Obj*)node->data.whileStmt.body);
                    break;
                case AST_FOR:
                    if (node->data.forStmt.startExpr) markObject((Obj*)node->data.forStmt.startExpr);
                    if (node->data.forStmt.endExpr) markObject((Obj*)node->data.forStmt.endExpr);
                    if (node->data.forStmt.stepExpr) markObject((Obj*)node->data.forStmt.stepExpr);
                    if (node->data.forStmt.body) markObject((Obj*)node->data.forStmt.body);
                    break;
                case AST_FUNCTION:
                    if (node->data.function.parameters) markObject((Obj*)node->data.function.parameters);
                    if (node->data.function.body) markObject((Obj*)node->data.function.body);
                    if (node->data.function.returnType) markObject((Obj*)node->data.function.returnType);
                    if (node->data.function.implType) markObject((Obj*)node->data.function.implType);
                    if (node->data.function.mangledName) markObject((Obj*)node->data.function.mangledName);
                    for (int i = 0; i < node->data.function.genericCount; i++) {
                        markObject((Obj*)node->data.function.genericParams[i]);
                    }
                    break;
                case AST_CALL:
                    if (node->data.call.arguments) markObject((Obj*)node->data.call.arguments);
                    if (node->data.call.staticType) markObject((Obj*)node->data.call.staticType);
                    if (node->data.call.mangledName) markObject((Obj*)node->data.call.mangledName);
                    for (int i = 0; i < node->data.call.genericArgCount; i++) {
                        markObject((Obj*)node->data.call.genericArgs[i]);
                    }
                    break;
                case AST_VARIABLE:
                    for (int i = 0; i < node->data.variable.genericArgCount; i++) {
                        if (node->data.variable.genericArgs[i]) markObject((Obj*)node->data.variable.genericArgs[i]);
                    }
                    break;
                case AST_RETURN:
                    if (node->data.returnStmt.value) markObject((Obj*)node->data.returnStmt.value);
                    break;
                case AST_ARRAY:
                    if (node->data.array.elements) markObject((Obj*)node->data.array.elements);
                    break;
                case AST_ARRAY_SET:
                    if (node->data.arraySet.index) markObject((Obj*)node->data.arraySet.index);
                    break;
                case AST_STRUCT_LITERAL:
                    if (node->data.structLiteral.values) markObject((Obj*)node->data.structLiteral.values);
                    for (int i = 0; i < node->data.structLiteral.genericArgCount; i++) {
                        if (node->data.structLiteral.genericArgs[i]) markObject((Obj*)node->data.structLiteral.genericArgs[i]);
                    }
                    break;
                default:
                    break;
            }
            if (node->valueType) markObject((Obj*)node->valueType);
            break;
        }
        case OBJ_TYPE: {
            Type* type = (Type*)object;
            switch (type->kind) {
                case TYPE_ARRAY:
                    if (type->info.array.elementType)
                        markObject((Obj*)type->info.array.elementType);
                    break;
                case TYPE_FUNCTION:
                    if (type->info.function.returnType)
                        markObject((Obj*)type->info.function.returnType);
                    for (int i = 0; i < type->info.function.paramCount; i++) {
                        markObject((Obj*)type->info.function.paramTypes[i]);
                    }
                    break;
                case TYPE_STRUCT:
                    markObject((Obj*)type->info.structure.name);
                    for (int i = 0; i < type->info.structure.fieldCount; i++) {
                        markObject((Obj*)type->info.structure.fields[i].name);
                        markObject((Obj*)type->info.structure.fields[i].type);
                    }
                    for (int i = 0; i < type->info.structure.genericCount; i++) {
                        markObject((Obj*)type->info.structure.genericParams[i]);
                    }
                    break;
                case TYPE_GENERIC:
                    markObject((Obj*)type->info.generic.name);
                    break;
                default:
                    break;
            }
            break;
        }
    }
}

/**
 * Run the mark-and-sweep garbage collector.
 */
void collectGarbage() {
    if (vm.gcPaused) return;
    // Mark roots
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }
    for (int i = 0; i < vm.variableCount; i++) {
        markValue(vm.globals[i]);
        if (vm.variableNames[i].name != NULL) {
            markObject((Obj*)vm.variableNames[i].name);
        }
        if (variableTypes[i] != NULL) {
            markObject((Obj*)variableTypes[i]);
        }
        if (vm.functionDecls[i] != NULL) {
            markObject((Obj*)vm.functionDecls[i]);
        }
    }
    if (vm.chunk != NULL) {
        for (int i = 0; i < vm.chunk->constants.count; i++) {
            markValue(vm.chunk->constants.values[i]);
        }
    }
    if (vm.astRoot != NULL) {
        markObject((Obj*)vm.astRoot);
    }
    markTypeRoots();

    // Sweep
    Obj** object = &vm.objects;
    while (*object) {
        if (!(*object)->marked) {
            Obj* unreached = *object;
            *object = unreached->next;
            freeObject(unreached);
        } else {
            (*object)->marked = false;
            object = &(*object)->next;
        }
    }
}

/**
 * Free a single heap object.
 *
 * @param object Object to free.
 */
static void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            vm.bytesAllocated -= sizeof(ObjString) + string->length + 1;
            free(string->chars);
            free(string);
            break;
        }
        case OBJ_ARRAY: {
            ObjArray* array = (ObjArray*)object;
            vm.bytesAllocated -= sizeof(ObjArray) + sizeof(Value) * array->capacity;
            FREE_ARRAY(Value, array->elements, array->capacity);
            free(array);
            break;
        }
        case OBJ_INT_ARRAY: {
            ObjIntArray* array = (ObjIntArray*)object;
            vm.bytesAllocated -= sizeof(ObjIntArray) + sizeof(int64_t) * array->length;
            free(array->elements);
            free(array);
            break;
        }
        case OBJ_RANGE_ITERATOR: {
            vm.bytesAllocated -= sizeof(ObjRangeIterator);
            free(object);
            break;
        }
        case OBJ_ERROR: {
            ObjError* err = (ObjError*)object;
            vm.bytesAllocated -= sizeof(ObjError);
            free(err);
            break;
        }
        case OBJ_AST: {
            ASTNode* node = (ASTNode*)object;
            if (node->type == AST_CALL) {
                if (node->data.call.convertArgs) {
                    free(node->data.call.convertArgs);
                }
                if (node->data.call.genericArgs) {
                    free(node->data.call.genericArgs);
                }
            } else if (node->type == AST_STRUCT_LITERAL) {
                if (node->data.structLiteral.genericArgs) {
                    free(node->data.structLiteral.genericArgs);
                }
            } else if (node->type == AST_VARIABLE) {
                if (node->data.variable.genericArgs) {
                    free(node->data.variable.genericArgs);
                }
            }
            if (node->type == AST_FUNCTION && node->data.function.genericParams) {
                free(node->data.function.genericParams);
            }
            free(node);
            break;
        }
        case OBJ_TYPE: {
            Type* type = (Type*)object;
            switch (type->kind) {
                case TYPE_FUNCTION:
                    free(type->info.function.paramTypes);
                    break;
                case TYPE_STRUCT:
                    free(type->info.structure.fields);
                    free(type->info.structure.genericParams);
                    break;
                case TYPE_GENERIC:
                    break;
                default:
                    break;
            }
            free(type);
            break;
        }
    }
}

/**
 * Free all objects currently allocated by the VM.
 */
void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}

void pauseGC() { vm.gcPaused = true; }

void resumeGC() { vm.gcPaused = false; }

/**
 * Duplicate a string into freshly allocated memory.
 *
 * @param chars  Source characters.
 * @param length Number of bytes to copy.
 * @return       Newly allocated null-terminated string.
 */
char* copyString(const char* chars, int length) {
    char* copy = (char*)malloc(length + 1);
    memcpy(copy, chars, length);
    copy[length] = '\0';
    return copy;
}
