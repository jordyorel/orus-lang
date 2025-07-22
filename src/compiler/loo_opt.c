// #include <assert.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>

// #include "compiler.h"
// #include "compiler/ast.h"
// #include "compiler/compiler.h"
// #include "compiler/lexer.h"
// #include "compiler/loop_optimization.h"
// #include "compiler/symbol_table.h"
// #include "runtime/memory.h"
// #include "vm/vm.h"

// // Constants
// #define MAX_INVARIANTS 64     // Maximum invariant expressions
// #define MAX_REDUCTIONS 32     // Maximum strength reductions
// #define MAX_UNROLL_FACTOR 16  // Maximum iterations for unrolling
// #define INITIAL_INVARIANTS_CAPACITY 8
// #define INITIAL_MODIFIED_CAPACITY 8
// #define INITIAL_REDUCTIONS_CAPACITY 8

// // Structures
// typedef struct {
//     ASTNode* expr;  // Invariant expression
//     uint8_t reg;    // Assigned register
// } InvariantEntry;

// typedef struct {
//     ASTNode* expr;          // Multiplication expression
//     ASTNode* inductionVar;  // Loop variable
//     uint8_t shiftAmount;    // Shift amount for power of two
// } ReductionEntry;

// typedef struct {
//     InvariantEntry* entries;
//     int count;
//     int capacity;
// } LoopInvariants;

// typedef struct {
//     char** names;
//     int count;
//     int capacity;
// } ModifiedSet;

// typedef struct {
//     ReductionEntry* entries;
//     int count;
//     int capacity;
// } ReductionSet;

// typedef struct {
//     LoopInvariants invariants;  // Hoisted expressions
//     ModifiedSet modifiedVars;   // Modified variables
//     ReductionSet reductions;    // Strength reduction opportunities
//     int64_t iterationCount;     // For constant-range loops
//     bool isConstantRange;       // Whether loop has fixed iterations
//     bool canUnroll;             // Safe to unroll
//     bool canEliminateBounds;    // Safe to skip bounds checks
//     int startInstr;             // Bytecode index at loop start
//     int scopeDepth;             // Scope depth
// } LoopContext;

// // Helper Functions

// // Arena-based string duplication
// static char* arena_strdup(Arena* arena, const char* str) {
//     if (!str) return NULL;
//     size_t len = strlen(str) + 1;
//     char* copy = arena_alloc(arena, len);
//     if (copy) {
//         memcpy(copy, str, len);
//     }
//     return copy;
// }

// // Add a variable to the modified set
// static void addModified(ModifiedSet* set, const char* name, Arena* arena) {
//     if (!name || !set || !arena) return;
//     for (int i = 0; i < set->count; i++) {
//         if (strcmp(set->names[i], name) == 0) return;
//     }
//     if (set->count >= set->capacity) {
//         int new_capacity =
//             set->capacity ? set->capacity * 2 : INITIAL_MODIFIED_CAPACITY;
//         char** new_names = arena_alloc(arena, new_capacity * sizeof(char*));
//         if (!new_names) return;
//         for (int i = 0; i < set->count; i++) {
//             new_names[i] = set->names[i];
//         }
//         set->names = new_names;
//         set->capacity = new_capacity;
//     }
//     set->names[set->count++] = arena_strdup(arena, name);
// }

// // Collect modified variables
// static void collectModifiedVariables(ASTNode* node, ModifiedSet* modified,
//                                      Arena* arena) {
//     if (!node) return;
//     switch (node->type) {
//         case NODE_ASSIGN:
//             addModified(modified, node->assign.name, arena);
//             collectModifiedVariables(node->assign.value, modified, arena);
//             break;
//         case NODE_BINARY:
//             collectModifiedVariables(node->binary.left, modified, arena);
//             collectModifiedVariables(node->binary.right, modified, arena);
//             break;
//         case NODE_CALL:
//             collectModifiedVariables(node->call.callee, modified, arena);
//             for (int i = 0; i < node->call.argCount; i++) {
//                 collectModifiedVariables(node->call.args[i], modified, arena);
//             }
//             break;
//         case NODE_FOR_RANGE:
//         case NODE_WHILE:
//             collectModifiedVariables(node->type == NODE_FOR_RANGE
//                                          ? node->forRange.body
//                                          : node->whileLoop.body,
//                                      modified, arena);
//             break;
//         default:
//             break;
//     }
// }

// // Check for side effects
// static bool hasSideEffects(ASTNode* node) {
//     if (!node) return false;
//     switch (node->type) {
//         case NODE_CALL:
//             return true;
//         case NODE_ASSIGN:
//             return true;
//         case NODE_BINARY:
//             return hasSideEffects(node->binary.left) ||
//                    hasSideEffects(node->binary.right);
//         case NODE_IDENTIFIER:
//         case NODE_NUMBER:
//             return false;
//         default:
//             return false;
//     }
// }

// // Check if expression depends on modified variables
// static bool dependsOnModified(ASTNode* node, ModifiedSet* modified) {
//     if (!node) return false;
//     switch (node->type) {
//         case NODE_IDENTIFIER:
//             for (int i = 0; i < modified->count; i++) {
//                 if (strcmp(node->identifier.name, modified->names[i]) == 0) {
//                     return true;
//                 }
//             }
//             return false;
//         case NODE_BINARY:
//             return dependsOnModified(node->binary.left, modified) ||
//                    dependsOnModified(node->binary.right, modified);
//         case NODE_CALL:
//             if (dependsOnModified(node->call.callee, modified)) return true;
//             for (int i = 0; i < node->call.argCount; i++) {
//                 if (dependsOnModified(node->call.args[i], modified))
//                     return true;
//             }
//             return false;
//         default:
//             return false;
//     }
// }

// // Check if expression is constant
// static bool isConstantExpression(ASTNode* node) {
//     if (!node) return false;
//     switch (node->type) {
//         case NODE_NUMBER:
//             return true;
//         case NODE_BINARY:
//             return isConstantExpression(node->binary.left) &&
//                    isConstantExpression(node->binary.right);
//         default:
//             return false;
//     }
// }

// // Evaluate constant integer expression
// static int64_t evaluateConstantInt(ASTNode* node) {
//     if (!isConstantExpression(node)) return 0;
//     switch (node->type) {
//         case NODE_NUMBER:
//             return node->number.value;
//         case NODE_BINARY: {
//             int64_t left = evaluateConstantInt(node->binary.left);
//             int64_t right = evaluateConstantInt(node->binary.right);
//             switch (node->binary.op[0]) {
//                 case '+':
//                     return left + right;
//                 case '-':
//                     return left - right;
//                 case '*':
//                     return left * right;
//                 case '/':
//                     return right != 0 ? left / right : 0;
//                 case '%':
//                     return right != 0 ? left % right : 0;
//                 default:
//                     return 0;
//             }
//         }
//         default:
//             return 0;
//     }
// }

// // Check for break/continue statements
// static bool hasBreakOrContinue(ASTNode* node) {
//     if (!node) return false;
//     switch (node->type) {
//         case NODE_BREAK:
//         case NODE_CONTINUE:
//             return true;
//         case NODE_BLOCK:
//             for (int i = 0; i < node->block.count; i++) {
//                 if (hasBreakOrContinue(node->block.statements[i])) return true;
//             }
//             return false;
//         case NODE_IF:
//             return hasBreakOrContinue(node->ifStmt.thenBranch) ||
//                    hasBreakOrContinue(node->ifStmt.elseBranch);
//         case NODE_FOR_RANGE:
//         case NODE_WHILE:
//             return hasBreakOrContinue(node->type == NODE_FOR_RANGE
//                                           ? node->forRange.body
//                                           : node->whileLoop.body);
//         default:
//             return false;
//     }
// }

// // Check if value is power of two
// static bool isPowerOfTwo(int64_t n) { return n > 0 && (n & (n - 1)) == 0; }

// // Get shift amount for power of two
// static uint8_t getShiftAmount(int64_t n) {
//     if (!isPowerOfTwo(n)) return 0;
//     uint8_t shift = 0;
//     while (n > 1) {
//         n >>= 1;
//         shift++;
//     }
//     return shift;
// }

// // Collect candidate expressions for LICM
// static void collectCandidates(ASTNode* node, ASTNode** candidates, int* count,
//                               int* capacity, Compiler* compiler) {
//     if (!node) return;
//     switch (node->type) {
//         case NODE_BINARY:
//             if (!hasSideEffects(node)) {
//                 if (*count >= *capacity) {
//                     int new_capacity = *capacity * 2;
//                     ASTNode** new_candidates = arena_alloc(
//                         compiler->arena, new_capacity * sizeof(ASTNode*));
//                     if (!new_candidates) {
//                         reportError(compiler, node->location,
//                                     "Arena allocation failed for candidates");
//                         return;
//                     }
//                     for (int i = 0; i < *count; i++) {
//                         new_candidates[i] = candidates[i];
//                     }
//                     candidates = new_candidates;
//                     *capacity = new_capacity;
//                 }
//                 candidates[(*count)++] = node;
//             }
//             collectCandidates(node->binary.left, candidates, count, capacity,
//                               compiler);
//             collectCandidates(node->binary.right, candidates, count, capacity,
//                               compiler);
//             break;
//         case NODE_CALL:
//             collectCandidates(node->call.callee, candidates, count, capacity,
//                               compiler);
//             for (int i = 0; i < node->call.argCount; i++) {
//                 collectCandidates(node->call.args[i], candidates, count,
//                                   capacity, compiler);
//             }
//             break;
//         case NODE_ASSIGN:
//             collectCandidates(node->assign.value, candidates, count, capacity,
//                               compiler);
//             break;
//         case NODE_FOR_RANGE:
//         case NODE_WHILE:
//             collectCandidates(node->type == NODE_FOR_RANGE
//                                   ? node->forRange.body
//                                   : node->whileLoop.body,
//                               candidates, count, capacity, compiler);
//             break;
//         default:
//             break;
//     }
// }

// // Collect strength reduction opportunities
// static void collectReductions(ASTNode* node, const char* loopVarName,
//                               ReductionSet* reductions, Arena* arena,
//                               Compiler* compiler) {
//     if (!node || reductions->count >= MAX_REDUCTIONS) return;
//     switch (node->type) {
//         case NODE_BINARY:
//             if (node->binary.op[0] == '*' && node->binary.op[1] == '\0') {
//                 ASTNode* constantNode = NULL;
//                 ASTNode* inductionNode = NULL;
//                 if (node->binary.left &&
//                     node->binary.left->type == NODE_IDENTIFIER &&
//                     strcmp(node->binary.left->identifier.name, loopVarName) ==
//                         0 &&
//                     isConstantExpression(node->binary.right)) {
//                     inductionNode = node->binary.left;
//                     constantNode = node->binary.right;
//                 } else if (node->binary.right &&
//                            node->binary.right->type == NODE_IDENTIFIER &&
//                            strcmp(node->binary.right->identifier.name,
//                                   loopVarName) == 0 &&
//                            isConstantExpression(node->binary.left)) {
//                     inductionNode = node->binary.right;
//                     constantNode = node->binary.left;
//                 }
//                 if (constantNode && inductionNode) {
//                     int64_t multiplier = evaluateConstantInt(constantNode);
//                     if (isPowerOfTwo(multiplier)) {
//                         if (reductions->count >= reductions->capacity) {
//                             int new_capacity = reductions->capacity * 2;
//                             ReductionEntry* new_entries = arena_alloc(
//                                 arena, new_capacity * sizeof(ReductionEntry));
//                             if (!new_entries) {
//                                 reportError(
//                                     compiler, node->location,
//                                     "Arena allocation failed for reductions");
//                                 return;
//                             }
//                             for (int i = 0; i < reductions->count; i++) {
//                                 new_entries[i] = reductions->entries[i];
//                             }
//                             reductions->entries = new_entries;
//                             reductions->capacity = new_capacity;
//                         }
//                         ReductionEntry* entry =
//                             &reductions->entries[reductions->count++];
//                         entry->expr = node;
//                         entry->inductionVar = inductionNode;
//                         entry->shiftAmount = getShiftAmount(multiplier);
//                     }
//                 }
//             }
//             collectReductions(node->binary.left, loopVarName, reductions, arena,
//                               compiler);
//             collectReductions(node->binary.right, loopVarName, reductions,
//                               arena, compiler);
//             break;
//         case NODE_ASSIGN:
//             collectReductions(node->assign.value, loopVarName, reductions,
//                               arena, compiler);
//             break;
//         case NODE_FOR_RANGE:
//         case NODE_WHILE:
//             collectReductions(node->type == NODE_FOR_RANGE
//                                   ? node->forRange.body
//                                   : node->whileLoop.body,
//                               loopVarName, reductions, arena, compiler);
//             break;
//         default:
//             break;
//     }
// }

// // Mark array accesses for bounds elimination
// static void markBoundsSafe(ASTNode* node, bool canEliminateBounds) {
//     if (!node) return;
//     if (node->type == NODE_ARRAY_ACCESS && canEliminateBounds) {
//         node->arrayAccess.isBoundsSafe =
//             true;  // Assumes ASTNode has isBoundsSafe flag
//     }
//     switch (node->type) {
//         case NODE_BINARY:
//             markBoundsSafe(node->binary.left, canEliminateBounds);
//             markBoundsSafe(node->binary.right, canEliminateBounds);
//             break;
//         case NODE_ASSIGN:
//             markBoundsSafe(node->assign.value, canEliminateBounds);
//             break;
//         case NODE_FOR_RANGE:
//         case NODE_WHILE:
//             markBoundsSafe(node->type == NODE_FOR_RANGE ? node->forRange.body
//                                                         : node->whileLoop.body,
//                            canEliminateBounds);
//             break;
//         default:
//             break;
//     }
// }

// // Analyze loop for optimizations
// static void analyzeLoop(ASTNode* node, Compiler* compiler,
//                         LoopContext* context) {
//     if (!node || !compiler || !context) return;

//     // Initialize context
//     context->invariants.entries = arena_alloc(
//         compiler->arena, INITIAL_INVARIANTS_CAPACITY * sizeof(InvariantEntry));
//     context->invariants.count = 0;
//     context->invariants.capacity = INITIAL_INVARIANTS_CAPACITY;
//     context->modifiedVars.names =
//         arena_alloc(compiler->arena, INITIAL_MODIFIED_CAPACITY * sizeof(char*));
//     context->modifiedVars.count = 0;
//     context->modifiedVars.capacity = INITIAL_MODIFIED_CAPACITY;
//     context->reductions.entries = arena_alloc(
//         compiler->arena, INITIAL_REDUCTIONS_CAPACITY * sizeof(ReductionEntry));
//     context->reductions.count = 0;
//     context->reductions.capacity = INITIAL_REDUCTIONS_CAPACITY;
//     context->scopeDepth = compiler->scopeDepth;
//     context->startInstr = compiler->codeCount;

//     if (!context->invariants.entries || !context->modifiedVars.names ||
//         !context->reductions.entries) {
//         reportError(
//             compiler,
//             (SrcLocation){compiler->currentLine, compiler->currentColumn},
//             "Arena allocation failed for loop context");
//         return;
//     }

//     // Get loop body and variable
//     ASTNode* loopBody = (node->type == NODE_FOR_RANGE) ? node->forRange.body
//                                                        : node->whileLoop.body;
//     const char* loopVarName =
//         (node->type == NODE_FOR_RANGE) ? node->forRange.varName : NULL;

//     // Collect modified variables
//     collectModifiedVariables(loopBody, &context->modifiedVars, compiler->arena);

//     // Analyze constant range for NODE_FOR_RANGE
//     if (node->type == NODE_FOR_RANGE) {
//         bool startConstant = isConstantExpression(node->forRange.start);
//         bool endConstant = isConstantExpression(node->forRange.end);
//         bool stepConstant =
//             !node->forRange.step || isConstantExpression(node->forRange.step);
//         if (startConstant && endConstant && stepConstant) {
//             context->isConstantRange = true;
//             int64_t start = evaluateConstantInt(node->forRange.start);
//             int64_t end = evaluateConstantInt(node->forRange.end);
//             int64_t step = node->forRange.step
//                                ? evaluateConstantInt(node->forRange.step)
//                                : 1;
//             if (step > 0 && end > start) {
//                 context->iterationCount = (end - start + step - 1) / step;
//             } else if (step < 0 && end < start) {
//                 context->iterationCount = (start - end + (-step) - 1) / (-step);
//             } else {
//                 context->iterationCount = 0;
//             }
//             context->canUnroll = context->iterationCount > 0 &&
//                                  context->iterationCount <= MAX_UNROLL_FACTOR &&
//                                  !hasBreakOrContinue(loopBody);
//             context->canEliminateBounds = context->iterationCount > 0;
//         }
//     }

//     // Collect invariant candidates
//     int candidate_capacity = INITIAL_INVARIANTS_CAPACITY;
//     ASTNode** candidates =
//         arena_alloc(compiler->arena, candidate_capacity * sizeof(ASTNode*));
//     if (!candidates) {
//         reportError(
//             compiler,
//             (SrcLocation){compiler->currentLine, compiler->currentColumn},
//             "Arena allocation failed for candidates");
//         return;
//     }
//     int candidate_count = 0;
//     collectCandidates(loopBody, candidates, &candidate_count,
//                       &candidate_capacity, compiler);

//     // Filter invariants
//     for (int i = 0;
//          i < candidate_count && context->invariants.count < MAX_INVARIANTS;
//          i++) {
//         ASTNode* expr = candidates[i];
//         if (!hasSideEffects(expr) &&
//             !dependsOnModified(expr, &context->modifiedVars)) {
//             if (context->invariants.count >= context->invariants.capacity) {
//                 int new_capacity = context->invariants.capacity * 2;
//                 InvariantEntry* new_entries = arena_alloc(
//                     compiler->arena, new_capacity * sizeof(InvariantEntry));
//                 if (!new_entries) {
//                     reportError(compiler, expr->location,
//                                 "Arena allocation failed for invariants");
//                     break;
//                 }
//                 for (int j = 0; j < context->invariants.count; j++) {
//                     new_entries[j] = context->invariants.entries[j];
//                 }
//                 context->invariants.entries = new_entries;
//                 context->invariants.capacity = new_capacity;
//             }
//             InvariantEntry* entry =
//                 &context->invariants.entries[context->invariants.count++];
//             entry->expr = expr;
//             entry->reg = allocateRegister(compiler);
//             compiler->registers[entry->reg].isPersistent = true;
//         }
//     }

//     // Collect strength reductions (only for NODE_FOR_RANGE with loop variable)
//     if (node->type == NODE_FOR_RANGE && loopVarName) {
//         collectReductions(loopBody, loopVarName, &context->reductions,
//                           compiler->arena, compiler);
//     }

//     // Mark bounds-safe array accesses
//     if (context->canEliminateBounds) {
//         markBoundsSafe(loopBody, true);
//     }
// }

// // Unroll constant-range loop
// static bool unrollLoop(ASTNode* node, LoopContext* context,
//                        Compiler* compiler) {
//     if (!context->canUnroll || context->iterationCount <= 0) return false;

//     // Get loop properties
//     int64_t start = evaluateConstantInt(node->forRange.start);
//     int64_t step =
//         node->forRange.step ? evaluateConstantInt(node->forRange.step) : 1;
//     const char* loopVarName = node->forRange.varName;

//     // Save symbol table state
//     int oldVarIdx = -1;
//     bool hadOldVar =
//         symbol_table_get(&compiler->symbols, loopVarName, &oldVarIdx);

//     // Unroll iterations
//     for (int64_t i = 0, current = start; i < context->iterationCount;
//          i++, current += step) {
//         uint8_t iterReg = allocateRegister(compiler);
//         compiler->registers[iterReg].isPersistent = true;
//         emitByte(compiler, OP_CONSTANT);
//         emitByte(compiler, iterReg);
//         emitConstantValue(compiler,
//                           current);  // Assumes emitConstantValue emits int64_t
//         symbol_table_set(&compiler->symbols, loopVarName, -(iterReg + 1),
//                          compiler->scopeDepth);

//         compiler->currentInvariants = &context->invariants;
//         if (!compileNode(node->forRange.body, compiler)) {
//             freeRegister(compiler, iterReg);
//             if (hadOldVar) {
//                 symbol_table_set(&compiler->symbols, loopVarName, oldVarIdx,
//                                  compiler->scopeDepth);
//             }
//             compiler->currentInvariants = NULL;
//             return false;
//         }
//         freeRegister(compiler, iterReg);
//     }

//     // Restore symbol table
//     if (hadOldVar) {
//         symbol_table_set(&compiler->symbols, loopVarName, oldVarIdx,
//                          compiler->scopeDepth);
//     }
//     compiler->currentInvariants = NULL;
//     return true;
// }

// // Apply strength reductions
// static void applyReductions(LoopContext* context, Compiler* compiler) {
//     for (int i = 0; i < context->reductions.count; i++) {
//         ReductionEntry* reduction = &context->reductions.entries[i];
//         ASTNode* expr = reduction->expr;
//         expr->type = NODE_BINARY;
//         expr->binary.op = arena_strdup(compiler->arena, "<<");
//         expr->binary.right->type = NODE_NUMBER;
//         expr->binary.right->number.value = reduction->shiftAmount;
//     }
// }

// // Main loop optimization function
// bool optimizeLoop(ASTNode* node, Compiler* compiler) {
//     if (!node || !compiler) {
//         reportError(
//             compiler,
//             (SrcLocation){compiler->currentLine, compiler->currentColumn},
//             "Invalid loop or compiler state");
//         return false;
//     }

//     if (node->type != NODE_FOR_RANGE && node->type != NODE_WHILE) {
//         reportError(compiler, node->location,
//                     "Unsupported loop type for optimization");
//         return false;
//     }

//     // Setup loop context
//     compiler->loopCount++;
//     if (compiler->loopCount >= compiler->loopCapacity) {
//         int new_capacity =
//             compiler->loopCapacity ? compiler->loopCapacity * 2 : 8;
//         LoopContext* new_loops =
//             arena_alloc(compiler->arena, new_capacity * sizeof(LoopContext));
//         if (!new_loops) {
//             reportError(compiler, node->location,
//                         "Arena allocation failed for loop stack");
//             compiler->loopCount--;
//             return false;
//         }
//         for (int i = 0; i < compiler->loopCount; i++) {
//             new_loops[i] = compiler->loops[i];
//         }
//         compiler->loops = new_loops;
//         compiler->loopCapacity = new_capacity;
//     }
//     LoopContext* context = &compiler->loops[compiler->loopCount - 1];

//     // Get loop body
//     ASTNode* loopBody = (node->type == NODE_FOR_RANGE) ? node->forRange.body
//                                                        : node->whileLoop.body;
//     if (!loopBody) {
//         reportError(compiler, node->location, "Empty loop body");
//         compiler->loopCount--;
//         return false;
//     }

//     // Mark arena state
//     size_t arena_mark = arena_get_mark(compiler->arena);

//     // Analyze loop
//     analyzeLoop(node, compiler, context);

//     // Apply strength reductions
//     if (context->reductions.count > 0) {
//         applyReductions(context, compiler);
//     }

//     // Try unrolling for NODE_FOR_RANGE
//     if (node->type == NODE_FOR_RANGE && context->canUnroll) {
//         if (unrollLoop(node, context, compiler)) {
//             // Cleanup
//             for (int i = 0; i < context->invariants.count; i++) {
//                 freeRegister(compiler, context->invariants.entries[i].reg);
//             }
//             arena_free_all(compiler->arena, arena_mark);
//             compiler->loopCount--;
//             return true;
//         }
//     }

//     // Compile hoisted invariants
//     for (int i = 0; i < context->invariants.count; i++) {
//         uint8_t reg = context->invariants.entries[i].reg;
//         int tempReg =
//             compileExpr(context->invariants.entries[i].expr, compiler);
//         if (tempReg == -1) {
//             reportError(compiler, context->invariants.entries[i].expr->location,
//                         "Failed to compile invariant expression");
//             for (int j = 0; j < context->invariants.count; j++) {
//                 freeRegister(compiler, context->invariants.entries[j].reg);
//             }
//             arena_free_all(compiler->arena, arena_mark);
//             compiler->loopCount--;
//             return false;
//         }
//         emitByte(compiler, OP_MOVE);
//         emitByte(compiler, reg);
//         emitByte(compiler, tempReg);
//         freeRegister(compiler, tempReg);
//     }

//     // Compile loop body
//     compiler->currentInvariants = &context->invariants;
//     bool result = compileNode(loopBody, compiler);
//     compiler->currentInvariants = NULL;

//     // Cleanup
//     for (int i = 0; i < context->invariants.count; i++) {
//         freeRegister(compiler, context->invariants.entries[i].reg);
//     }
//     arena_free_all(compiler->arena, arena_mark);
//     compiler->loopCount--;

//     return result;
// }