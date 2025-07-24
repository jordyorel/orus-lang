// ============================================================================
// hybrid_compiler.c
// ============================================================================

#include <stdio.h>
#include <string.h>

#include "compiler/hybrid_compiler.h"
#include "compiler/backend_selection.h"
#include "compiler/symbol_table.h"
#include "compiler/shared_node_compilation.h"
#include "compiler/node_registry.h"
#include "internal/logging.h"
#include "vm/vm.h"
#include "vm/vm_constants.h"
#include "vm/vm_config.h"
#include "runtime/memory.h"

// Forward declarations
static int addToSecondaryConstantTable(Compiler* compiler, Value value);
static bool compileGranularHybrid(ASTNode* ast, Compiler* compiler, bool isModule);
static CompilationStrategy chooseNodeStrategy(ASTNode* node);
static bool isSimpleProgram(ASTNode* ast);
static bool shouldUseFastPath(ASTNode* ast, CompilationStrategy strategy);

// Complexity analysis thresholds
#define SIMPLE_FUNCTION_THRESHOLD 2
#define SIMPLE_LOOP_THRESHOLD 3
#define SIMPLE_NESTING_THRESHOLD 2
#define SIMPLE_COMPLEXITY_THRESHOLD 10

// Remove duplicated complexity analysis - now using unified version from backend_selection.c
// Forward declarations for remaining functions
static int countPotentialUpvalues(ASTNode* node);
static bool isComplexExpression(ASTNode* node);

// Removed duplicate analyzeNodeComplexity - now using unified version from backend_selection.c

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
    LOG_COMPILER_DEBUG("hybrid", "Strategy analysis: functions=%d, calls=%d, loops=%d, nested=%d, break/continue=%d", 
                      complexity.functionCount, complexity.callCount, complexity.loopCount, 
                      complexity.nestedLoopDepth, complexity.hasBreakContinue);
    
    // Initialize compilation context for smart backend selection
    CompilationContext ctx;
    initCompilationContext(&ctx, false); // Assume release mode for now
    
    // Update context based on complexity analysis
    ctx.functionCallDepth = complexity.callCount;
    ctx.loopNestingDepth = complexity.nestedLoopDepth;
    ctx.hasBreakContinue = complexity.hasBreakContinue;
    ctx.hasComplexTypes = complexity.hasComplexArithmetic;
    
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
        LOG_COMPILER_DEBUG("hybrid", "-> Smart Backend Override: MULTI-PASS (complex features: break/continue=%d, nesting=%d)", 
                          complexity.hasBreakContinue, complexity.nestedLoopDepth);
    }
    
    CompilationStrategy strategy;
    switch (backend) {
        case BACKEND_FAST:
            strategy = COMPILE_SINGLE_PASS;
            LOG_COMPILER_DEBUG("hybrid", "-> Smart Backend Selection: SINGLE-PASS (fast compilation)");
            break;
        case BACKEND_OPTIMIZED:
            strategy = COMPILE_MULTI_PASS;
            LOG_COMPILER_DEBUG("hybrid", "-> Smart Backend Selection: MULTI-PASS (optimized compilation)");
            break;
        case BACKEND_HYBRID:
            // Use granular hybrid compilation for mixed complexity
            if (complexity.functionCount > 0 || 
                (complexity.loopCount > 0 && complexity.complexExpressionCount > 3)) {
                strategy = COMPILE_HYBRID;
                LOG_COMPILER_DEBUG("hybrid", "-> Smart Backend Selection: HYBRID (mixed complexity)");
            } else {
                // Fall back to single strategy for simpler cases
                strategy = (complexity.complexityScore > 15.0f) ? COMPILE_MULTI_PASS : COMPILE_SINGLE_PASS;
                LOG_COMPILER_DEBUG("hybrid", "-> Smart Backend Selection: %s (hybrid fallback)", 
                                  strategy == COMPILE_MULTI_PASS ? "MULTI-PASS" : "SINGLE-PASS");
            }
            break;
        case BACKEND_AUTO:
        default:
            // Fallback logic with hybrid consideration
            if (complexity.hasBreakContinue || complexity.nestedLoopDepth > 1) {
                strategy = COMPILE_MULTI_PASS;
                LOG_COMPILER_DEBUG("hybrid", "-> Smart Backend Selection: MULTI-PASS (fallback - complex features)");
            } else if (complexity.functionCount > 0 && complexity.complexExpressionCount < 10) {
                // Mixed complexity - good candidate for hybrid
                strategy = COMPILE_HYBRID;
                LOG_COMPILER_DEBUG("hybrid", "-> Smart Backend Selection: HYBRID (fallback - mixed complexity)");
            } else {
                strategy = COMPILE_SINGLE_PASS;
                LOG_COMPILER_DEBUG("hybrid", "-> Smart Backend Selection: SINGLE-PASS (fallback - simple code)");
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
        // Store in secondary constant table for large constant pools
        int tableIndex = addToSecondaryConstantTable(compiler, value);
        emitByte(compiler, OPCODE_LOAD_CONST_EXT);
        emitByte(compiler, reg);
        emitByte(compiler, (tableIndex >> 8) & 0xff);
        emitByte(compiler, tableIndex & 0xff);
    }
}

// Helper function for secondary constant table management
static int addToSecondaryConstantTable(Compiler* compiler __attribute__((unused)), Value value) {
    // For now, implement a simple secondary table
    // In a full implementation, this would manage a separate constant table
    // with proper memory management and cleanup
    
    static Value* secondaryConstants = NULL;
    static int secondaryCount = 0;
    static int secondaryCapacity = 0;
    
    if (secondaryConstants == NULL) {
        secondaryCapacity = 256;
        secondaryConstants = malloc(sizeof(Value) * secondaryCapacity);
    }
    
    if (secondaryCount >= secondaryCapacity) {
        secondaryCapacity *= 2;
        secondaryConstants = realloc(secondaryConstants, sizeof(Value) * secondaryCapacity);
    }
    
    secondaryConstants[secondaryCount] = value;
    return secondaryCount++;
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

    // Initialize node registry for extensible compilation
    registerAllNodeHandlers();

    // Fast-path optimization disabled for debugging
    // if (shouldUseFastPath(ast, strategy)) {
    //     LOG_DEBUG("Fast-path compilation: simple program detected, bypassing complexity analysis");
    //     initSinglePassCompiler(compiler, compiler->chunk, compiler->fileName, compiler->source);
    //     enableSinglePassFastPath();
    //     return compileSinglePass(ast, compiler, isModule);
    // }

    // Analyze complexity if using auto strategy
    CodeComplexity complexity = {0};
    if (strategy == COMPILE_AUTO) {
        complexity = analyzeCodeComplexity(ast);
        LOG_DEBUG("Complexity analysis: loops=%d, nestedDepth=%d, hasBreakContinue=%d", 
                 complexity.loopCount, complexity.nestedLoopDepth, complexity.hasBreakContinue);
        strategy = chooseStrategy(complexity);
        LOG_DEBUG("Chosen strategy: %s", 
                 strategy == COMPILE_MULTI_PASS ? "MULTI_PASS" : "SINGLE_PASS");
    }

    // Check if this is a good candidate for granular hybrid compilation
    if (strategy == COMPILE_HYBRID && ast->type == NODE_PROGRAM) {
        LOG_COMPILER_DEBUG("hybrid", "Using granular hybrid compilation for program");
        return compileGranularHybrid(ast, compiler, isModule);
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

        case COMPILE_HYBRID:
            // For non-program nodes, fall back to complexity-based selection
            LOG_COMPILER_DEBUG("hybrid", "Falling back to complexity-based selection for non-program node");
            CodeComplexity nodeComplexity = analyzeCodeComplexity(ast);
            CompilationStrategy fallbackStrategy = chooseStrategy(nodeComplexity);
            return compileHybrid(ast, compiler, isModule, fallbackStrategy);

        default:
            // Fallback to single-pass
            initSinglePassCompiler(compiler, compiler->chunk,
                                   compiler->fileName, compiler->source);
            return compileSinglePass(ast, compiler, isModule);
    }
}

// Granular hybrid compilation implementation
static bool compileGranularHybrid(ASTNode* ast, Compiler* compiler, bool isModule) {
    if (!ast || ast->type != NODE_PROGRAM) {
        LOG_ERROR("compileGranularHybrid called with non-program node");
        return false;
    }

    LOG_COMPILER_DEBUG("hybrid", "Starting granular hybrid compilation for program with %d declarations", 
                      ast->program.count);

    // Fast-path check for very simple programs
    if (isSimpleProgram(ast)) {
        LOG_COMPILER_DEBUG("hybrid", "Fast-path compilation: simple program detected");
        initSinglePassCompiler(compiler, compiler->chunk, compiler->fileName, compiler->source);
        return compileSinglePass(ast, compiler, isModule);
    }

    // Initialize compiler context for hybrid mode
    initMultiPassCompiler(compiler, compiler->chunk, compiler->fileName, compiler->source);
    
    bool success = true;
    int singlePassNodes = 0;
    int multiPassNodes = 0;

    // Compile each top-level declaration with appropriate strategy
    for (int i = 0; i < ast->program.count; i++) {
        ASTNode* node = ast->program.declarations[i];
        if (!node) continue;

        // Analyze complexity of this specific node
        CodeComplexity nodeComplexity = analyzeCodeComplexity(node);
        CompilationStrategy nodeStrategy = chooseNodeStrategy(node);

        LOG_COMPILER_DEBUG("hybrid", "Node %d: type=%d, strategy=%s, complexity=%.1f", 
                          i, node->type,
                          nodeStrategy == COMPILE_SINGLE_PASS ? "SINGLE_PASS" : "MULTI_PASS",
                          nodeComplexity.complexityScore);

        // Track statistics
        if (nodeStrategy == COMPILE_SINGLE_PASS) {
            singlePassNodes++;
        } else {
            multiPassNodes++;
        }

        // Compile the node with the chosen strategy
        switch (nodeStrategy) {
            case COMPILE_SINGLE_PASS: {
                // Use shared node compilation for simple nodes
                CompilerContext ctx = createSinglePassContext();
                if (!compileSharedNode(node, compiler, &ctx)) {
                    LOG_ERROR("Failed to compile node %d with single-pass strategy", i);
                    success = false;
                }
                break;
            }
            case COMPILE_MULTI_PASS: {
                // For multi-pass, compile the node directly
                // In a real implementation, this would use per-node multi-pass compilation
                if (!compileMultiPass(node, compiler, false)) {
                    LOG_ERROR("Failed to compile node %d with multi-pass strategy", i);
                    success = false;
                }
                break;
            }
            default:
                LOG_WARN("Unknown strategy for node %d, falling back to multi-pass", i);
                if (!compileMultiPass(node, compiler, false)) {
                    LOG_ERROR("Failed to compile node %d with fallback strategy", i);
                    success = false;
                }
                break;
        }

        if (!success) {
            break;
        }
    }

    LOG_COMPILER_DEBUG("hybrid", "Granular compilation complete: %d single-pass, %d multi-pass nodes", 
                      singlePassNodes, multiPassNodes);

    return success;
}

// Choose compilation strategy for individual nodes
static CompilationStrategy chooseNodeStrategy(ASTNode* node) {
    if (!node) {
        return COMPILE_SINGLE_PASS;
    }

    // Analyze the specific node
    CodeComplexity complexity = analyzeCodeComplexity(node);

    // Fast compilation for simple statements
    switch (node->type) {
        case NODE_LITERAL:
        case NODE_IDENTIFIER:
        case NODE_VAR_DECL:
            // Simple declarations and expressions
            if (complexity.complexityScore < 5.0f) {
                return COMPILE_SINGLE_PASS;
            }
            break;

        case NODE_ASSIGN:
        case NODE_BINARY:
        case NODE_CAST:
            // Simple operations
            if (complexity.complexityScore < 10.0f && !complexity.hasComplexArithmetic) {
                return COMPILE_SINGLE_PASS;
            }
            break;

        case NODE_IF:
            // Simple if statements without complex nesting
            if (complexity.nestedLoopDepth == 0 && complexity.complexityScore < 15.0f) {
                return COMPILE_SINGLE_PASS;
            }
            break;

        case NODE_WHILE:
        case NODE_FOR_RANGE:
            // Loops generally benefit from multi-pass optimization
            return COMPILE_MULTI_PASS;

        case NODE_FUNCTION:
        case NODE_CALL:
            // Functions always use multi-pass for proper optimization
            return COMPILE_MULTI_PASS;

        case NODE_BREAK:
        case NODE_CONTINUE:
            // Control flow statements need multi-pass support
            return COMPILE_MULTI_PASS;

        case NODE_BLOCK:
            // Blocks depend on their content complexity
            if (complexity.loopCount > 0 || complexity.functionCount > 0) {
                return COMPILE_MULTI_PASS;
            }
            if (complexity.complexityScore < 20.0f) {
                return COMPILE_SINGLE_PASS;
            }
            break;

        default:
            // Unknown or complex nodes use multi-pass
            return COMPILE_MULTI_PASS;
    }

    // Default decision based on overall complexity
    return (complexity.complexityScore > 15.0f) ? COMPILE_MULTI_PASS : COMPILE_SINGLE_PASS;
}

// Fast-path detection for very simple programs
// This function determines if a program is simple enough to skip complexity analysis
static bool isSimpleProgram(ASTNode* ast) {
    if (!ast || ast->type != NODE_PROGRAM) {
        return false;
    }

    // Programs with too many declarations are not simple
    if (ast->program.count > 5) {
        return false;
    }

    // Check if all top-level declarations are simple
    for (int i = 0; i < ast->program.count; i++) {
        ASTNode* node = ast->program.declarations[i];
        if (!node) continue;

        switch (node->type) {
            // Complex constructs that disqualify from fast-path
            case NODE_FUNCTION:
            case NODE_CALL:
            case NODE_BREAK:
            case NODE_CONTINUE:
            case NODE_WHILE:
            case NODE_FOR_RANGE:
                LOG_DEBUG("Fast-path disqualified by complex node type: %d", node->type);
                return false;

            case NODE_IF:
                // Simple if without else is allowed, but if-else is complex
                if (node->ifStmt.elseBranch != NULL) {
                    LOG_DEBUG("Fast-path disqualified by if-else statement");
                    return false;
                }
                // Check if condition is simple (no nested calls or complex expressions)
                if (node->ifStmt.condition && node->ifStmt.condition->type == NODE_CALL) {
                    LOG_DEBUG("Fast-path disqualified by complex if condition");
                    return false;
                }
                break;

            case NODE_BLOCK:
                // Blocks with many statements are complex
                if (node->block.count > 3) {
                    LOG_DEBUG("Fast-path disqualified by large block (%d statements)", node->block.count);
                    return false;
                }
                break;

            case NODE_VAR_DECL:
            case NODE_ASSIGN:
            case NODE_PRINT:
            case NODE_LITERAL:
            case NODE_IDENTIFIER:
            case NODE_BINARY:
            case NODE_CAST:
            case NODE_TIME_STAMP:
                // These are simple and allowed
                break;

            default:
                // Unknown or potentially complex nodes
                LOG_DEBUG("Fast-path disqualified by unknown node type: %d", node->type);
                return false;
        }
    }

    LOG_DEBUG("Program qualified for fast-path compilation (%d declarations)", ast->program.count);
    return true;
}

// Determine if fast-path compilation should be used
// This combines simplicity checks with strategy considerations
static bool shouldUseFastPath(ASTNode* ast, CompilationStrategy strategy) {
    // Only use fast-path for auto strategy (let explicit strategies proceed normally)
    if (strategy != COMPILE_AUTO) {
        return false;
    }

    // Only apply to programs
    if (!ast || ast->type != NODE_PROGRAM) {
        return false;
    }

    // Check if the program qualifies for fast-path
    if (!isSimpleProgram(ast)) {
        return false;
    }

    // Additional checks for very trivial programs that benefit most from fast-path
    // Programs with only literals, simple variable declarations, and basic arithmetic
    int trivialNodes = 0;
    for (int i = 0; i < ast->program.count; i++) {
        ASTNode* node = ast->program.declarations[i];
        if (!node) continue;

        switch (node->type) {
            case NODE_VAR_DECL:
                // Simple variable declarations are trivial
                if (node->varDecl.initializer == NULL || 
                    node->varDecl.initializer->type == NODE_LITERAL ||
                    node->varDecl.initializer->type == NODE_IDENTIFIER) {
                    trivialNodes++;
                }
                break;
            case NODE_ASSIGN:
            case NODE_PRINT:
            case NODE_LITERAL:
                trivialNodes++;
                break;
            case NODE_BINARY:
                // Simple arithmetic is allowed
                if (node->binary.left && node->binary.right &&
                    (node->binary.left->type == NODE_LITERAL || node->binary.left->type == NODE_IDENTIFIER) &&
                    (node->binary.right->type == NODE_LITERAL || node->binary.right->type == NODE_IDENTIFIER)) {
                    trivialNodes++;
                }
                break;
            default:
                break;
        }
    }

    // If most nodes are trivial, it's an excellent fast-path candidate
    bool isMostlyTrivial = (ast->program.count > 0) && (trivialNodes >= ast->program.count * 0.7);
    
    if (isMostlyTrivial) {
        LOG_DEBUG("Fast-path: program is mostly trivial (%d/%d nodes)", trivialNodes, ast->program.count);
    }

    return true; // All simple programs use fast-path
}