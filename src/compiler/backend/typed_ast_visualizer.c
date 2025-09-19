/*
 * File: src/compiler/backend/typed_ast_visualizer.c
 * Typed AST Visualization Tool for Orus Compiler
 * 
 * This tool outputs the typed AST in a human-readable, tree-like format
 * to aid debugging and verification of HM type inference results.
 * 
 * Features:
 * - Tree-like indented output showing AST structure
 * - Type annotations for each node
 * - Node metadata (constants, register hints, etc.)
 * - Optional detailed mode showing all attributes
 * - Color-coded output support (when terminal supports it)
 */

#include "compiler/typed_ast.h"
#include "compiler/ast.h"
#include "vm/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Configuration for visualization
typedef struct {
    bool show_metadata;     // Show optimization hints and register info
    bool show_locations;    // Show source line/column information
    bool use_colors;        // Use ANSI color codes (if terminal supports)
    bool compact_mode;      // More compact output
    int max_depth;          // Maximum depth to visualize (-1 for unlimited)
} VisualizerConfig;

// Forward declarations
static void visualize_node_recursive(TypedASTNode* node, int depth, bool is_last, const VisualizerConfig* config);
static void print_indent(int depth, bool is_last, const VisualizerConfig* config);

// ANSI color codes for terminal output
#define COLOR_RESET     "\033[0m"
#define COLOR_BOLD      "\033[1m"
#define COLOR_RED       "\033[31m"
#define COLOR_GREEN     "\033[32m"
#define COLOR_YELLOW    "\033[33m"
#define COLOR_BLUE      "\033[34m"
#define COLOR_MAGENTA   "\033[35m"
#define COLOR_CYAN      "\033[36m"
#define COLOR_WHITE     "\033[37m"

// Default configuration
static VisualizerConfig default_config = {
    .show_metadata = true,
    .show_locations = true,
    .use_colors = false,  // Conservative default
    .compact_mode = false,
    .max_depth = -1
};

// Helper function to get node type name
static const char* get_node_type_name(NodeType type) {
    switch (type) {
        case NODE_PROGRAM: return "Program";
        case NODE_VAR_DECL: return "VarDecl";
        case NODE_IDENTIFIER: return "Identifier";
        case NODE_LITERAL: return "Literal";
        case NODE_ARRAY_LITERAL: return "ArrayLiteral";
        case NODE_INDEX_ACCESS: return "IndexAccess";
        case NODE_ARRAY_SLICE: return "ArraySlice";
        case NODE_BINARY: return "Binary";
        case NODE_ASSIGN: return "Assign";
        case NODE_ARRAY_ASSIGN: return "ArrayAssign";
        case NODE_PRINT: return "Print";
        case NODE_TIME_STAMP: return "TimeStamp";
        case NODE_IF: return "If";
        case NODE_WHILE: return "While";
        case NODE_FOR_RANGE: return "ForRange";
        case NODE_FOR_ITER: return "ForIter";
        case NODE_TRY: return "Try";
        case NODE_THROW: return "Throw";
        case NODE_BLOCK: return "Block";
        case NODE_TERNARY: return "Ternary";
        case NODE_UNARY: return "Unary";
        case NODE_TYPE: return "Type";
        case NODE_BREAK: return "Break";
        case NODE_CONTINUE: return "Continue";
        case NODE_FUNCTION: return "Function";
        case NODE_CALL: return "Call";
        case NODE_RETURN: return "Return";
        case NODE_CAST: return "Cast";
        default: return "Unknown";
    }
}


// Helper function to detect if this is an if-elif-else chain
static bool is_if_elif_chain(TypedASTNode* node) {
    if (!node || node->original->type != NODE_IF) return false;
    
    // Check if the else branch is another if statement (indicating elif)
    return (node->typed.ifStmt.elseBranch && 
            node->typed.ifStmt.elseBranch->original->type == NODE_IF);
}

// Helper function to visualize if-elif-else chains in a flattened way
static void visualize_if_elif_chain(TypedASTNode* node, int depth, const VisualizerConfig* config) {
    TypedASTNode* current = node;
    int chain_index = 0;
    
    while (current && current->original->type == NODE_IF) {
        // Print the condition (with appropriate if/elif label)
        const char* prefix = (chain_index == 0) ? "[if]" : "[elif]";
        
        // Use standard indentation for condition
        print_indent(depth + 1, false, (VisualizerConfig*)config);
        printf("%sCondition%s: %s\n", 
               config->use_colors ? COLOR_CYAN : "",
               config->use_colors ? COLOR_RESET : "",
               prefix);
        
        if (current->typed.ifStmt.condition) {
            visualize_node_recursive(current->typed.ifStmt.condition, depth + 2, false, config);
        }
        
        // Visualize the block directly without manual header
        if (current->typed.ifStmt.thenBranch) {
            visualize_node_recursive(current->typed.ifStmt.thenBranch, depth + 1, false, config);
        }
        
        // Move to the next part of the chain
        if (current->typed.ifStmt.elseBranch && 
            current->typed.ifStmt.elseBranch->original->type == NODE_IF) {
            current = current->typed.ifStmt.elseBranch;
            chain_index++;
        } else {
            // Handle the final else block if present
            if (current->typed.ifStmt.elseBranch) {
                // Print the else condition label
                print_indent(depth + 1, false, (VisualizerConfig*)config);
                printf("%sCondition%s: [else]\n", 
                       config->use_colors ? COLOR_CYAN : "",
                       config->use_colors ? COLOR_RESET : "");
                
                // Then visualize the else block
                visualize_node_recursive(current->typed.ifStmt.elseBranch, depth + 1, true, config);
            }
            break;
        }
    }
}

// Helper function to get type name string
static const char* get_type_name(Type* type) {
    if (!type) return "unresolved";
    
    switch (type->kind) {
        case TYPE_UNKNOWN: return "unknown";
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_U32: return "u32";
        case TYPE_U64: return "u64";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_VOID: return "void";
        case TYPE_ARRAY: return "array";
        case TYPE_FUNCTION: {
            // Create a more detailed function type signature
            static char func_sig[256];
            if (type->info.function.arity > 0 && type->info.function.paramTypes && type->info.function.returnType) {
                snprintf(func_sig, sizeof(func_sig), "function(");
                
                for (int i = 0; i < type->info.function.arity; i++) {
                    if (i > 0) strcat(func_sig, ",");
                    const char* param_type = get_type_name(type->info.function.paramTypes[i]);
                    strcat(func_sig, param_type);
                }
                
                strcat(func_sig, ")->");
                const char* return_type = get_type_name(type->info.function.returnType);
                strcat(func_sig, return_type);
                
                return func_sig;
            }
            return "function";
        }
        case TYPE_ERROR: return "error";
        case TYPE_ANY: return "any";
        case TYPE_VAR: return "var";
        case TYPE_GENERIC: return "generic";
        case TYPE_INSTANCE: return "instance";
        default: return "unknown";
    }
}

// Helper function to print indentation
static void print_indent(int depth, bool is_last, const VisualizerConfig* config) {
    if (config->compact_mode) {
        for (int i = 0; i < depth; i++) {
            printf("  ");
        }
        return;
    }
    
    for (int i = 0; i < depth; i++) {
        if (i == depth - 1) {
            printf(is_last ? "└── " : "├── ");
        } else {
            printf("│   ");
        }
    }
}

// Helper function to get literal value as string
static const char* get_literal_value_string(Value* value, char* buffer, size_t buffer_size) {
    if (!value || !buffer) return "null";
    
    switch (value->type) {
        case VAL_BOOL:
            snprintf(buffer, buffer_size, "%s", value->as.boolean ? "true" : "false");
            break;
        case VAL_I32:
            snprintf(buffer, buffer_size, "%d", value->as.i32);
            break;
        case VAL_I64:
            snprintf(buffer, buffer_size, "%lld", (long long)value->as.i64);
            break;
        case VAL_U32:
            snprintf(buffer, buffer_size, "%u", value->as.u32);
            break;
        case VAL_U64:
            snprintf(buffer, buffer_size, "%llu", (unsigned long long)value->as.u64);
            break;
        case VAL_F64:
            snprintf(buffer, buffer_size, "%.6g", value->as.f64);
            break;
        case VAL_NUMBER:
            snprintf(buffer, buffer_size, "%.6g", value->as.number);
            break;
        case VAL_STRING: {
            if (value->as.obj && IS_STRING(*value)) {
                ObjString* str = AS_STRING(*value);
                snprintf(buffer, buffer_size, "\"%.*s\"", str->length, str->chars);
            } else {
                snprintf(buffer, buffer_size, "\"<invalid string>\"");
            }
            break;
        }
        default:
            snprintf(buffer, buffer_size, "<unknown value>");
            break;
    }
    
    return buffer;
}

// Core visualization function - recursive AST traversal
static void visualize_node_recursive(TypedASTNode* node, int depth, bool is_last, const VisualizerConfig* config) {
    if (!node || !node->original) return;
    
    // Check depth limit
    if (config->max_depth >= 0 && depth > config->max_depth) {
        print_indent(depth, is_last, config);
        printf("... (max depth reached)\n");
        return;
    }
    
    // Print indentation and node info
    print_indent(depth, is_last, config);
    
    // Color coding based on node type (if enabled)
    const char* color_start = "";
    const char* color_end = "";
    if (config->use_colors) {
        switch (node->original->type) {
            case NODE_LITERAL:
                color_start = COLOR_GREEN;
                color_end = COLOR_RESET;
                break;
            case NODE_IDENTIFIER:
                color_start = COLOR_BLUE;
                color_end = COLOR_RESET;
                break;
            case NODE_BINARY:
            case NODE_UNARY:
                color_start = COLOR_YELLOW;
                color_end = COLOR_RESET;
                break;
            case NODE_FUNCTION:
            case NODE_CALL:
                color_start = COLOR_MAGENTA;
                color_end = COLOR_RESET;
                break;
            default:
                break;
        }
    }
    
    // Print node name and type information
    printf("%s%s%s", color_start, get_node_type_name(node->original->type), color_end);
    
    // Print type information
    if (node->typeResolved && node->resolvedType) {
        printf(": type=%s", get_type_name(node->resolvedType));
    } else if (node->hasTypeError) {
        printf(": type=ERROR");
        if (config->use_colors) printf("%s", COLOR_RED);
        if (node->errorMessage) {
            printf(" (%s)", node->errorMessage);
        }
        if (config->use_colors) printf("%s", COLOR_RESET);
    } else {
        printf(": type=unresolved");
    }
    
    // Print node-specific details
    char buffer[256];
    switch (node->original->type) {
        case NODE_IDENTIFIER:
            if (node->original->identifier.name) {
                printf(" name='%s'", node->original->identifier.name);
            }
            break;
        case NODE_LITERAL:
            printf(" value=%s", get_literal_value_string(&node->original->literal.value, buffer, sizeof(buffer)));
            if (node->original->literal.hasExplicitSuffix) {
                printf(" [explicit_suffix]");
            }
            break;
        case NODE_BINARY:
            if (node->original->binary.op) {
                printf(" op='%s'", node->original->binary.op);
            }
            break;
        case NODE_UNARY:
            if (node->original->unary.op) {
                printf(" op='%s'", node->original->unary.op);
            }
            break;
        case NODE_VAR_DECL:
            if (node->original->varDecl.name) {
                printf(" name='%s'", node->original->varDecl.name);
            }
            if (node->original->varDecl.isMutable) {
                printf(" [mutable]");
            }
            if (node->original->varDecl.isConst) {
                printf(" [const]");
            }
            break;
        case NODE_ASSIGN:
            if (node->typed.assign.name) {
                printf(" name='%s'", node->typed.assign.name);
            }
            break;
        case NODE_FUNCTION:
            if (node->original->function.name) {
                printf(" name='%s'", node->original->function.name);
            }
            printf(" params=%d", node->original->function.paramCount);
            break;
        case NODE_CALL:
            printf(" args=%d", node->original->call.argCount);
            break;
        case NODE_INDEX_ACCESS:
            printf(" [index]");
            break;
        case NODE_ARRAY_ASSIGN:
            printf(" [array_assign]");
            break;
        case NODE_ARRAY_SLICE:
            printf(" [array_slice]");
            break;
        default:
            break;
    }
    
    // Print metadata if requested
    if (config->show_metadata) {
        if (node->isConstant) {
            printf(" [CONST]");
        }
        if (node->canInline) {
            printf(" [INLINE]");
        }
        if (node->suggestedRegister >= 0) {
            printf(" [REG:R%d]", node->suggestedRegister);
        }
        if (!node->spillable) {
            printf(" [NO_SPILL]");
        }
    }
    
    // Print source location if requested
    if (config->show_locations) {
        printf(" @%d:%d", node->original->location.line, node->original->location.column);
    }
    
    printf("\n");
    
    // Recursively print children based on node type
    // Use the typed AST children that were created during generate_typed_ast
    switch (node->original->type) {
        case NODE_PROGRAM:
            if (node->typed.program.declarations) {
                for (int i = 0; i < node->typed.program.count; i++) {
                    if (node->typed.program.declarations[i]) {
                        visualize_node_recursive(node->typed.program.declarations[i], depth + 1, i == node->typed.program.count - 1, config);
                    }
                }
            }
            break;
        case NODE_BINARY:
            if (node->typed.binary.left) {
                visualize_node_recursive(node->typed.binary.left, depth + 1, false, config);
            }
            if (node->typed.binary.right) {
                visualize_node_recursive(node->typed.binary.right, depth + 1, true, config);
            }
            break;
        case NODE_UNARY:
            if (node->typed.unary.operand) {
                visualize_node_recursive(node->typed.unary.operand, depth + 1, true, config);
            }
            break;
        case NODE_VAR_DECL:
            if (node->typed.varDecl.typeAnnotation) {
                visualize_node_recursive(node->typed.varDecl.typeAnnotation, depth + 1, 
                                       !node->typed.varDecl.initializer, config);
            }
            if (node->typed.varDecl.initializer) {
                visualize_node_recursive(node->typed.varDecl.initializer, depth + 1, true, config);
            }
            break;
        case NODE_ASSIGN:
            if (node->typed.assign.value) {
                visualize_node_recursive(node->typed.assign.value, depth + 1, true, config);
            }
            break;
        case NODE_ARRAY_ASSIGN:
            if (node->typed.arrayAssign.target) {
                visualize_node_recursive(node->typed.arrayAssign.target, depth + 1,
                                         node->typed.arrayAssign.value == NULL, config);
            }
            if (node->typed.arrayAssign.value) {
                visualize_node_recursive(node->typed.arrayAssign.value, depth + 1, true, config);
            }
            break;
        case NODE_INDEX_ACCESS:
            if (node->typed.indexAccess.array) {
                visualize_node_recursive(node->typed.indexAccess.array, depth + 1, false, config);
            }
            if (node->typed.indexAccess.index) {
                visualize_node_recursive(node->typed.indexAccess.index, depth + 1, true, config);
            }
            break;
        case NODE_ARRAY_SLICE:
            if (node->typed.arraySlice.array) {
                visualize_node_recursive(node->typed.arraySlice.array, depth + 1, false, config);
            }
            if (node->typed.arraySlice.start) {
                visualize_node_recursive(node->typed.arraySlice.start, depth + 1, false, config);
            }
            if (node->typed.arraySlice.end) {
                visualize_node_recursive(node->typed.arraySlice.end, depth + 1, true, config);
            }
            break;
        case NODE_CALL:
            if (node->typed.call.callee) {
                visualize_node_recursive(node->typed.call.callee, depth + 1, 
                                       node->typed.call.argCount == 0, config);
            }
            if (node->typed.call.args) {
                for (int i = 0; i < node->typed.call.argCount; i++) {
                    if (node->typed.call.args[i]) {
                        visualize_node_recursive(node->typed.call.args[i], depth + 1, 
                                               i == node->typed.call.argCount - 1, config);
                    }
                }
            }
            break;
        case NODE_PRINT:
            if (node->typed.print.values) {
                for (int i = 0; i < node->typed.print.count; i++) {
                    if (node->typed.print.values[i]) {
                        bool is_last = (i == node->typed.print.count - 1) && !node->typed.print.separator;
                        visualize_node_recursive(node->typed.print.values[i], depth + 1, is_last, config);
                    }
                }
            }
            if (node->typed.print.separator) {
                visualize_node_recursive(node->typed.print.separator, depth + 1, true, config);
            }
            break;
        case NODE_IF:
            // Check if this is part of an if-elif-else chain
            if (is_if_elif_chain(node)) {
                visualize_if_elif_chain(node, depth, config);
            } else {
                // Regular if statement
                if (node->typed.ifStmt.condition) {
                    visualize_node_recursive(node->typed.ifStmt.condition, depth + 1, false, config);
                }
                if (node->typed.ifStmt.thenBranch) {
                    visualize_node_recursive(node->typed.ifStmt.thenBranch, depth + 1, 
                                           !node->typed.ifStmt.elseBranch, config);
                }
                if (node->typed.ifStmt.elseBranch) {
                    visualize_node_recursive(node->typed.ifStmt.elseBranch, depth + 1, true, config);
                }
            }
            break;
        case NODE_WHILE:
            if (node->typed.whileStmt.condition) {
                visualize_node_recursive(node->typed.whileStmt.condition, depth + 1, false, config);
            }
            if (node->typed.whileStmt.body) {
                visualize_node_recursive(node->typed.whileStmt.body, depth + 1, true, config);
            }
            break;
        case NODE_TERNARY:
            if (node->typed.ternary.condition) {
                visualize_node_recursive(node->typed.ternary.condition, depth + 1, false, config);
            }
            if (node->typed.ternary.trueExpr) {
                visualize_node_recursive(node->typed.ternary.trueExpr, depth + 1, false, config);
            }
            if (node->typed.ternary.falseExpr) {
                visualize_node_recursive(node->typed.ternary.falseExpr, depth + 1, true, config);
            }
            break;
        case NODE_BLOCK:
            if (node->typed.block.statements) {
                for (int i = 0; i < node->typed.block.count; i++) {
                    if (node->typed.block.statements[i]) {
                        visualize_node_recursive(node->typed.block.statements[i], depth + 1, 
                                               i == node->typed.block.count - 1, config);
                    }
                }
            }
            break;
        case NODE_FOR_RANGE:
            if (node->typed.forRange.start) {
                visualize_node_recursive(node->typed.forRange.start, depth + 1, false, config);
            }
            if (node->typed.forRange.end) {
                visualize_node_recursive(node->typed.forRange.end, depth + 1, 
                                       !node->typed.forRange.step && !node->typed.forRange.body, config);
            }
            if (node->typed.forRange.step) {
                visualize_node_recursive(node->typed.forRange.step, depth + 1, 
                                       !node->typed.forRange.body, config);
            }
            if (node->typed.forRange.body) {
                visualize_node_recursive(node->typed.forRange.body, depth + 1, true, config);
            }
            break;
        case NODE_FOR_ITER:
            if (node->typed.forIter.iterable) {
                visualize_node_recursive(node->typed.forIter.iterable, depth + 1,
                                       !node->typed.forIter.body, config);
            }
            if (node->typed.forIter.body) {
                visualize_node_recursive(node->typed.forIter.body, depth + 1, true, config);
            }
            break;
        case NODE_TRY:
            if (node->typed.tryStmt.tryBlock) {
                visualize_node_recursive(node->typed.tryStmt.tryBlock, depth + 1,
                                       !node->typed.tryStmt.catchBlock, config);
            }
            if (node->typed.tryStmt.catchBlock) {
                visualize_node_recursive(node->typed.tryStmt.catchBlock, depth + 1, true, config);
            }
            break;
        case NODE_THROW:
            if (node->typed.throwStmt.value) {
                visualize_node_recursive(node->typed.throwStmt.value, depth + 1, true, config);
            }
            break;
        case NODE_FUNCTION:
            if (node->typed.function.returnType) {
                visualize_node_recursive(node->typed.function.returnType, depth + 1, 
                                       !node->typed.function.body, config);
            }
            if (node->typed.function.body) {
                visualize_node_recursive(node->typed.function.body, depth + 1, true, config);
            }
            break;
        case NODE_RETURN:
            if (node->typed.returnStmt.value) {
                visualize_node_recursive(node->typed.returnStmt.value, depth + 1, true, config);
            }
            break;
        // Add more cases as needed for other node types
        default:
            // For leaf nodes or nodes without children, nothing more to print
            break;
    }
}

// Public API functions

void visualize_typed_ast(TypedASTNode* root, FILE* output) {
    if (!root || !output) return;
    
    fprintf(output, "=== TYPED AST VISUALIZATION ===\n");
    
    VisualizerConfig config = default_config;
    visualize_node_recursive(root, 0, true, &config);
    
    fprintf(output, "=== END TYPED AST ===\n");
}

void visualize_typed_ast_detailed(TypedASTNode* root, FILE* output, bool show_metadata, bool show_locations) {
    if (!root || !output) return;
    
    fprintf(output, "=== DETAILED TYPED AST VISUALIZATION ===\n");
    
    VisualizerConfig config = default_config;
    config.show_metadata = show_metadata;
    config.show_locations = show_locations;
    
    visualize_node_recursive(root, 0, true, &config);
    
    fprintf(output, "=== END DETAILED TYPED AST ===\n");
}

void visualize_typed_ast_compact(TypedASTNode* root, FILE* output) {
    if (!root || !output) return;
    
    fprintf(output, "=== COMPACT TYPED AST ===\n");
    
    VisualizerConfig config = default_config;
    config.compact_mode = true;
    config.show_metadata = false;
    config.show_locations = false;
    
    visualize_node_recursive(root, 0, true, &config);
    
    fprintf(output, "=== END COMPACT TYPED AST ===\n");
}

void visualize_typed_ast_colored(TypedASTNode* root, FILE* output) {
    if (!root || !output) return;
    
    fprintf(output, "=== COLORED TYPED AST VISUALIZATION ===\n");
    
    VisualizerConfig config = default_config;
    config.use_colors = true;
    
    visualize_node_recursive(root, 0, true, &config);
    
    fprintf(output, "=== END COLORED TYPED AST ===\n");
}

// Utility function to check if terminal supports colors
bool terminal_supports_color(void) {
    const char* term = getenv("TERM");
    if (!term) return false;
    
    // Simple heuristic - check for common terminal types that support color
    return (strstr(term, "color") != NULL ||
            strstr(term, "xterm") != NULL ||
            strstr(term, "screen") != NULL ||
            strstr(term, "tmux") != NULL);
}

// Debug function to print AST node statistics
void print_typed_ast_stats(TypedASTNode* root, FILE* output) {
    if (!root || !output) return;
    
    // TODO: Implement AST statistics (node counts, type distribution, etc.)
    fprintf(output, "=== TYPED AST STATISTICS ===\n");
    fprintf(output, "Root node type: %s\n", get_node_type_name(root->original->type));
    fprintf(output, "Type resolved: %s\n", root->typeResolved ? "yes" : "no");
    fprintf(output, "Has type error: %s\n", root->hasTypeError ? "yes" : "no");
    if (root->resolvedType) {
        fprintf(output, "Resolved type: %s\n", get_type_name(root->resolvedType));
    }
    fprintf(output, "=== END STATISTICS ===\n");
}