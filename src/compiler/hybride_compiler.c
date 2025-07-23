// ============================================================================
// hybrid_compiler.c
// ============================================================================

#include <stdio.h>
#include <string.h>

#include "compiler/hybrid_compiler.h"
#include "compiler/backend_selection.h"
#include "compiler/symbol_table.h"
#include "vm/vm.h"
#include "vm/vm_constants.h"
#include "runtime/memory.h"

// Complexity analysis thresholds
#define SIMPLE_FUNCTION_THRESHOLD 2
#define SIMPLE_LOOP_THRESHOLD 3
#define SIMPLE_NESTING_THRESHOLD 2
#define SIMPLE_COMPLEXITY_THRESHOLD 10

// Forward declarations
static void analyzeNodeComplexity(ASTNode* node, CodeComplexity* complexity,
                                  int depth);
static int countPotentialUpvalues(ASTNode* node);
static bool isComplexExpression(ASTNode* node);

// Analyze code complexity to determine compilation strategy
CodeComplexity analyzeComplexity(ASTNode* ast) {
    CodeComplexity complexity = {0};

    if (!ast) return complexity;

    analyzeNodeComplexity(ast, &complexity, 0);

    // Calculate overall complexity score
    complexity.complexityScore =
        complexity.functionCount * 3 + complexity.loopCount * 2 +
        complexity.nestedLoopDepth * 4 + complexity.upvalueCount * 2 +
        complexity.callCount + (complexity.hasBreakContinue ? 3 : 0) +
        (complexity.hasComplexExpressions ? 2 : 0);

    return complexity;
}

static void analyzeNodeComplexity(ASTNode* node, CodeComplexity* complexity,
                                  int depth) {
    if (!node) return;

    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                analyzeNodeComplexity(node->program.declarations[i], complexity,
                                      depth);
            }
            break;

        case NODE_FUNCTION:
            complexity->functionCount++;
            // Functions increase potential upvalue usage
            complexity->upvalueCount +=
                countPotentialUpvalues(node->function.body);
            analyzeNodeComplexity(node->function.body, complexity, depth + 1);
            break;

        case NODE_FOR_RANGE:
        case NODE_WHILE:
            complexity->loopCount++;
            if (depth > complexity->nestedLoopDepth) {
                complexity->nestedLoopDepth = depth;
            }

            ASTNode* loopBody = (node->type == NODE_FOR_RANGE)
                                    ? node->forRange.body
                                    : node->whileStmt.body;
            analyzeNodeComplexity(loopBody, complexity, depth + 1);
            break;

        case NODE_BREAK:
        case NODE_CONTINUE:
            complexity->hasBreakContinue = true;
            break;

        case NODE_CALL:
            complexity->callCount++;
            analyzeNodeComplexity(node->call.callee, complexity, depth);
            for (int i = 0; i < node->call.argCount; i++) {
                analyzeNodeComplexity(node->call.args[i], complexity, depth);
            }
            break;

        case NODE_BINARY:
            // Check for complex nested expressions
            if (isComplexExpression(node)) {
                complexity->hasComplexExpressions = true;
            }
            analyzeNodeComplexity(node->binary.left, complexity, depth);
            analyzeNodeComplexity(node->binary.right, complexity, depth);
            break;

        case NODE_TERNARY:
            complexity->hasComplexExpressions = true;
            analyzeNodeComplexity(node->ternary.condition, complexity, depth);
            analyzeNodeComplexity(node->ternary.trueExpr, complexity, depth);
            analyzeNodeComplexity(node->ternary.falseExpr, complexity, depth);
            break;

        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                analyzeNodeComplexity(node->block.statements[i], complexity,
                                      depth);
            }
            break;

        case NODE_IF:
            analyzeNodeComplexity(node->ifStmt.condition, complexity, depth);
            analyzeNodeComplexity(node->ifStmt.thenBranch, complexity, depth);
            if (node->ifStmt.elseBranch) {
                analyzeNodeComplexity(node->ifStmt.elseBranch, complexity,
                                      depth);
            }
            break;

        case NODE_VAR_DECL:
            if (node->varDecl.initializer) {
                analyzeNodeComplexity(node->varDecl.initializer, complexity,
                                      depth);
            }
            break;

        case NODE_ASSIGN:
            analyzeNodeComplexity(node->assign.value, complexity, depth);
            break;

        case NODE_PRINT:
            for (int i = 0; i < node->print.count; i++) {
                analyzeNodeComplexity(node->print.values[i], complexity, depth);
            }
            break;

        case NODE_RETURN:
            if (node->returnStmt.value) {
                analyzeNodeComplexity(node->returnStmt.value, complexity,
                                      depth);
            }
            break;

        // Additional nodes that might be needed
        case NODE_UNARY:
            if (isComplexExpression(node)) {
                complexity->hasComplexExpressions = true;
            }
            analyzeNodeComplexity(node->unary.operand, complexity, depth);
            break;
            
        case NODE_CAST:
            analyzeNodeComplexity(node->cast.expression, complexity, depth);
            break;

        // Other nodes can be added as needed
        default:
            break;
    }
}

// Simple heuristic for potential upvalues
static int countPotentialUpvalues(ASTNode* node __attribute__((unused))) {
    // Placeholder: return 1 per function as an estimate
    return 1;
}

// Check if expression is complex (deeply nested or multiple operations)
static bool isComplexExpression(ASTNode* node) {
    if (!node) return false;

    // Consider binary expressions with nested expressions as complex
    if (node->type == NODE_BINARY) {
        return (node->binary.left->type == NODE_BINARY ||
                node->binary.right->type == NODE_BINARY);
    }

    return false;
}

// Smart compilation strategy selection using new backend selection system
CompilationStrategy chooseStrategy(CodeComplexity complexity) {
    printf("[DEBUG] Strategy analysis: functions=%d, calls=%d, loops=%d, nested=%d, break/continue=%d\n", 
           complexity.functionCount, complexity.callCount, complexity.loopCount, 
           complexity.nestedLoopDepth, complexity.hasBreakContinue);
    
    // Initialize compilation context for smart backend selection
    CompilationContext ctx;
    initCompilationContext(&ctx, false); // Assume release mode for now
    
    // Update context based on complexity analysis
    ctx.functionCallDepth = complexity.callCount;
    ctx.loopNestingDepth = complexity.nestedLoopDepth;
    ctx.hasBreakContinue = complexity.hasBreakContinue;
    ctx.hasComplexTypes = complexity.hasComplexExpressions;
    
    // Create a dummy AST node for backend selection (since we only have complexity data)
    // In real implementation, this would use the actual AST
    ASTNode dummyNode = {0};
    if (complexity.loopCount > 0) {
        dummyNode.type = NODE_FOR_RANGE;
    } else if (complexity.callCount > 0) {
        dummyNode.type = NODE_CALL;
    } else {
        dummyNode.type = NODE_LITERAL;
    }
    
    // Use smart backend selection
    CompilerBackend backend = chooseOptimalBackend(&dummyNode, &ctx);
    
    // Override backend selection if complex features require multi-pass
    if (complexity.hasBreakContinue || complexity.nestedLoopDepth > 1) {
        backend = BACKEND_OPTIMIZED;
        printf("[DEBUG] -> Smart Backend Override: MULTI-PASS (complex features: break/continue=%d, nesting=%d)\n", 
               complexity.hasBreakContinue, complexity.nestedLoopDepth);
    }
    
    CompilationStrategy strategy;
    switch (backend) {
        case BACKEND_FAST:
            strategy = COMPILE_SINGLE_PASS;
            printf("[DEBUG] -> Smart Backend Selection: SINGLE-PASS (fast compilation)\n");
            break;
        case BACKEND_OPTIMIZED:
            strategy = COMPILE_MULTI_PASS;
            printf("[DEBUG] -> Smart Backend Selection: MULTI-PASS (optimized compilation)\n");
            break;
        case BACKEND_HYBRID:
        case BACKEND_AUTO:
        default:
            // Fallback logic
            if (complexity.hasBreakContinue || complexity.nestedLoopDepth > 1) {
                strategy = COMPILE_MULTI_PASS;
                printf("[DEBUG] -> Smart Backend Selection: MULTI-PASS (fallback - complex features)\n");
            } else {
                strategy = COMPILE_SINGLE_PASS;
                printf("[DEBUG] -> Smart Backend Selection: SINGLE-PASS (fallback - simple code)\n");
            }
            break;
    }
    
    return strategy;
}

// Compiler initialization and cleanup (compatibility with old API)
void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source) {
    // Initialize the base compiler structure
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->currentLine = 1;
    compiler->currentColumn = 1;
    
    // Initialize symbol table
    symbol_table_init(&compiler->symbols);
    symbol_table_begin_scope(&compiler->symbols, 0);
    
    // Initialize register allocation
    for (int i = 0; i < REGISTER_COUNT; i++) {
        compiler->registerTypes[i] = VAL_NIL;
    }
    
    // Initialize compiler state
    compiler->scopeDepth = 0;
}

void freeCompiler(Compiler* compiler) {
    // Free symbol table
    symbol_table_free(&compiler->symbols);
    
    // Register types don't need explicit freeing
}

// Shared utility functions for compatibility with loop optimization
uint8_t allocateRegister(Compiler* compiler) {
    uint8_t r = compiler->nextRegister++;
    if (compiler->nextRegister > compiler->maxRegisters)
        compiler->maxRegisters = compiler->nextRegister;
    return r;
}

void freeRegister(Compiler* compiler, uint8_t reg) {
    if (reg == compiler->nextRegister - 1) {
        compiler->nextRegister--;
    }
}

void emitByte(Compiler* compiler, uint8_t byte) {
    writeChunk(compiler->chunk, byte, compiler->currentLine, compiler->currentColumn);
}

void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2) {
    emitByte(compiler, byte1);
    emitByte(compiler, byte2);
}

void emitConstant(Compiler* compiler, uint8_t reg, Value value) {
    int constantIndex = addConstant(compiler->chunk, value);

    if (constantIndex < 65536) {
        emitByte(compiler, OP_LOAD_CONST);
        emitByte(compiler, reg);
        emitByte(compiler, (constantIndex >> 8) & 0xff);
        emitByte(compiler, constantIndex & 0xff);
    } else {
        // For now, just use regular OP_LOAD_CONST with truncated index
        // TODO: Implement proper handling of large constant pools
        emitByte(compiler, OP_LOAD_CONST);
        emitByte(compiler, reg);
        emitByte(compiler, (constantIndex >> 8) & 0xff);
        emitByte(compiler, constantIndex & 0xff);
    }
}

// Compatibility wrapper for old compileNode API
bool compileNode(ASTNode* node, Compiler* compiler) {
    // Use hybrid compiler with auto strategy for single nodes
    return compileHybrid(node, compiler, false, COMPILE_AUTO);
}

// Main hybrid compilation interface
bool compileHybrid(ASTNode* ast, Compiler* compiler, bool isModule,
                   CompilationStrategy strategy) {
    if (!ast) return false;

    // Analyze complexity if using auto strategy
    CodeComplexity complexity = {0};
    if (strategy == COMPILE_AUTO) {
        complexity = analyzeComplexity(ast);
        printf("[DEBUG] Complexity analysis: loops=%d, nestedDepth=%d, hasBreakContinue=%d\n", 
               complexity.loopCount, complexity.nestedLoopDepth, complexity.hasBreakContinue);
        strategy = chooseStrategy(complexity);
        printf("[DEBUG] Chosen strategy: %s\n", 
               strategy == COMPILE_MULTI_PASS ? "MULTI_PASS" : "SINGLE_PASS");
    }

    // Choose and execute compilation strategy
    switch (strategy) {
        case COMPILE_SINGLE_PASS:
            initSinglePassCompiler(compiler, compiler->chunk,
                                   compiler->fileName, compiler->source);
            return compileSinglePass(ast, compiler, isModule);

        case COMPILE_MULTI_PASS:
            initMultiPassCompiler(compiler, compiler->chunk, compiler->fileName,
                                  compiler->source);
            return compileMultiPass(ast, compiler, isModule);

        default:
            // Fallback to single-pass
            initSinglePassCompiler(compiler, compiler->chunk,
                                   compiler->fileName, compiler->source);
            return compileSinglePass(ast, compiler, isModule);
    }
}