// Orus Language Project

#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "compiler/typed_ast.h"
#include <stdbool.h>
#include <stddef.h>

// Forward declarations for future implementation
typedef struct ConstantTable ConstantTable;
typedef struct UsageAnalysis UsageAnalysis;
typedef struct ExpressionCache ExpressionCache;
typedef struct Type Type;

typedef struct OptimizationContext OptimizationContext;

typedef struct OptimizationPassResult {
    bool success;
    int optimizations_applied;
    int nodes_eliminated;
    int constants_folded;
    int binary_expressions_folded;
} OptimizationPassResult;

typedef OptimizationPassResult (*OptimizationPassFunction)(TypedASTNode* node, OptimizationContext* ctx);

typedef struct OptimizationPass {
    const char* name;
    bool enabled;
    OptimizationPassFunction run;
} OptimizationPass;

typedef struct LoopTypeResidencyPlan {
    const TypedASTNode* loop_node;          // Loop statement associated with this plan
    const TypedASTNode* range_end_node;     // Range loop end expression
    const TypedASTNode* range_step_node;    // Range loop step expression
    const TypedASTNode* guard_left_node;    // While guard left operand
    const TypedASTNode* guard_right_node;   // While guard right operand
    bool range_end_prefers_typed;           // Keep range end typed
    bool range_end_requires_residency;      // Persist typed state for range end
    bool range_step_prefers_typed;          // Keep range step typed
    bool range_step_requires_residency;     // Persist typed state for range step
    bool guard_left_prefers_typed;          // Guard left operand prefers typed register
    bool guard_left_requires_residency;     // Guard left operand should persist typed state
    bool guard_right_prefers_typed;         // Guard right operand prefers typed register
    bool guard_right_requires_residency;    // Guard right operand should persist typed state
} LoopTypeResidencyPlan;

typedef struct LoopTypeAffinityBinding {
    const TypedASTNode* loop_node;            // Loop associated with this binding
    const Type* loop_variable_type;           // Dominant type for loop variable
    const Type* start_type;                   // Start expression type (range loops)
    const Type* end_type;                     // End expression type (range loops/guards)
    const Type* step_type;                    // Step expression type (range loops)
    bool start_prefers_typed;                 // Start prefers typed register usage
    bool end_prefers_typed;                   // End prefers typed register usage
    bool step_prefers_typed;                  // Step prefers typed register usage
    bool start_requires_residency;            // Start should persist typed residency
    bool end_requires_residency;              // End should persist typed residency
    bool step_requires_residency;             // Step should persist typed residency
    bool prefer_typed_registers;              // Loop overall prefers typed registers
    bool proven_numeric_bounds;               // Loop bounds proven numeric
    bool has_constant_start;                  // Start expression is constant
    bool has_constant_end;                    // End expression is constant
    bool has_constant_step;                   // Step expression is constant/default
    bool step_is_positive;                    // Step is positive (or default +1)
    bool step_is_negative;                    // Step is negative
    bool is_inclusive;                        // Range loop is inclusive
    bool is_range_loop;                       // Binding corresponds to range loop
    bool is_iterator_loop;                    // Binding corresponds to iterator loop
    bool is_while_loop;                       // Binding corresponds to while loop
    int loop_depth;                           // Nesting depth of loop when recorded
    const TypedASTNode* guard_left;           // Guard left operand (while loop)
    const TypedASTNode* guard_right;          // Guard right operand (while loop)
    const Type* guard_left_type;              // Guard left operand type
    const Type* guard_right_type;             // Guard right operand type
    const char* guard_operator;               // Comparison operator used in guard
    bool guard_prefers_typed;                 // Guard prefers typed evaluation
    bool guard_is_numeric;                    // Guard proven numeric
    bool guard_left_is_constant;              // Guard left operand constant
    bool guard_right_is_constant;             // Guard right operand constant
    bool guard_left_prefers_typed;            // Guard left prefers typed register
    bool guard_right_prefers_typed;           // Guard right prefers typed register
    bool guard_left_requires_residency;       // Guard left requires residency
    bool guard_right_requires_residency;      // Guard right requires residency
} LoopTypeAffinityBinding;

struct OptimizationContext {
    OptimizationPass* passes;
    size_t pass_count;
    size_t pass_capacity;

    // Analysis results (TODO: implement in advanced phases)
    ConstantTable* constants;          // Known constant values
    UsageAnalysis* usage;              // Variable usage tracking
    ExpressionCache* expressions;      // Common expressions

    // Statistics for reporting
    int optimizations_applied;
    int nodes_eliminated;
    int constants_folded;
    int binary_expressions_folded;      // Specific to constant folding

    // Debug information
    bool verbose_output;               // Enable detailed optimization logging

    // Loop residency analysis
    LoopTypeResidencyPlan* loop_residency_plans;
    size_t loop_residency_count;
    size_t loop_residency_capacity;

    // Loop affinity analysis
    LoopTypeAffinityBinding* loop_affinity_bindings;
    size_t loop_affinity_count;
    size_t loop_affinity_capacity;
};

// Core optimization functions
OptimizationContext* init_optimization_context(void);
TypedASTNode* optimize_typed_ast(TypedASTNode* input, OptimizationContext* ctx);
void free_optimization_context(OptimizationContext* ctx);

// Pass management helpers
bool set_optimization_pass_enabled(OptimizationContext* ctx, const char* name, bool enabled);
bool toggle_optimization_pass(OptimizationContext* ctx, const char* name);
bool is_optimization_pass_enabled(OptimizationContext* ctx, const char* name);

// Loop residency helpers
int optimization_add_loop_residency_plan(OptimizationContext* ctx, const LoopTypeResidencyPlan* plan);
const LoopTypeResidencyPlan* optimization_find_loop_residency_plan(const OptimizationContext* ctx,
                                                                   const TypedASTNode* loop_node);
void optimization_clear_loop_residency_plans(OptimizationContext* ctx);

// Loop affinity helpers
int optimization_add_loop_affinity(OptimizationContext* ctx, const LoopTypeAffinityBinding* binding);
const LoopTypeAffinityBinding* optimization_find_loop_affinity(const OptimizationContext* ctx,
                                                               const TypedASTNode* loop_node);
void optimization_clear_loop_affinities(OptimizationContext* ctx);

// Utility functions for optimization
bool is_constant_literal(TypedASTNode* node);
Value evaluate_constant_binary(const char* op, Value left, Value right);
TypedASTNode* create_constant_typed_node(Value value, Type* type);

// Statistics and reporting
void print_optimization_stats(OptimizationContext* ctx);
void reset_optimization_stats(OptimizationContext* ctx);

#endif // OPTIMIZER_H
