// Author: Jordy Orel KONDA
// Date: 2025-07-01
// Description: Compile-time scope analysis for the Orus language compiler



#define _POSIX_C_SOURCE 200809L
#include "vm/vm.h"
#include "compiler/compiler.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ANALYZER_VAR_CAPACITY 255
#define ANALYZER_LIFESPAN_CAPACITY 1024
#define ANALYZER_MAX_LIMIT 255

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void freeScopeTree(ScopeInfo* scope);
static void analyzeVariableLifetimes(ScopeInfo* scope);
static void optimizeScopeRegisterAllocation(ScopeInfo* scope);
static void buildRegisterInterferenceGraph(ScopeInfo* scope);
static void identifyRegisterCoalescing(ScopeInfo* scope);
static void optimizeRegisterAllocation(ScopeInfo* scope);
static void sortVariablesByPriority(ScopeInfo* scope);
static void allocateRegistersOptimally(ScopeInfo* scope);
static uint8_t findOptimalRegister(ScopeInfo* scope, ScopeVariable* var, uint8_t startReg);
static bool canUseRegisterForVariable(ScopeInfo* scope, ScopeVariable* var, uint8_t reg);
static void analyzeScopeTree(ScopeInfo* scope, ScopeAnalyzer* analyzer);
static void identifyHoistableVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer);
static void buildGlobalRegisterInterferenceGraph(ScopeAnalyzer* analyzer);
static void buildGlobalInterferenceFromScope(ScopeInfo* scope, ScopeAnalyzer* analyzer);
static void performGlobalRegisterOptimization(ScopeAnalyzer* analyzer);
static void applyRegisterCoalescing(ScopeAnalyzer* analyzer);
static void coalesceRegisters(ScopeAnalyzer* analyzer, uint8_t targetReg, uint8_t sourceReg);
static void updateVariableRegisters(ScopeInfo* scope, uint8_t oldReg, uint8_t newReg);
static void printScopeTree(ScopeInfo* scope, int indent);
static int getCurrentInstructionCount(Chunk* chunk);
static void applyScopeOptimizationsToCompiler(Compiler* compiler);
static void updateRegisterAllocatorFromScopeAnalysis(Compiler* compiler, ScopeAnalyzer* analyzer);
static void updateLocalVariablesFromScopeAnalysis(Compiler* compiler, ScopeAnalyzer* analyzer);
static void updateTypeInformationFromScopeAnalysis(Compiler* compiler, ScopeAnalyzer* analyzer);
ScopeVariable* findVariableInScopeTree(ScopeInfo* scope, const char* name);
static ScopeVariable* findVariableWithRegister(ScopeInfo* scope, uint8_t reg);

// Forward declarations for closure capture analysis
static void identifyCapturedVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer);
static void findCapturedVariablesInScope(ScopeInfo* scope, ScopeAnalyzer* analyzer);
static void markVariableAsCaptured(ScopeVariable* var, int captureDepth);
static bool needsHeapAllocation(ScopeVariable* var, ScopeAnalyzer* analyzer);
static void analyzeUpvalueUsage(ScopeAnalyzer* analyzer);
static void optimizeUpvalueAllocation(ScopeAnalyzer* analyzer);

// Forward declarations for dead variable elimination
static void identifyDeadVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer);
static void identifyWriteOnlyVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer);
static void analyzeComplexLifetimes(ScopeInfo* scope, ScopeAnalyzer* analyzer);
static bool isVariableUsedInNestedScopes(ScopeVariable* var, ScopeInfo* scope);
static void performConservativeElimination(ScopeAnalyzer* analyzer);
static void calculateDeadVariableElimination(ScopeAnalyzer* analyzer);

// Forward declarations for advanced analysis
static bool validateAnalysisResults(ScopeAnalyzer* analyzer);

// ---------------------------------------------------------------------------
// Compile-time Scope Analysis Implementation
// ---------------------------------------------------------------------------


// Initialize scope analyzer
void initScopeAnalyzer(ScopeAnalyzer* analyzer) {
    // Zero out the entire structure first for safety
    memset(analyzer, 0, sizeof(ScopeAnalyzer));
    
    analyzer->rootScope = NULL;
    analyzer->currentScope = NULL;
    analyzer->scopeStackCapacity = 32;
    analyzer->scopeStack = malloc(sizeof(ScopeInfo*) * analyzer->scopeStackCapacity);
    if (analyzer->scopeStack) {
        memset(analyzer->scopeStack, 0, sizeof(ScopeInfo*) * analyzer->scopeStackCapacity);
    }
    analyzer->scopeStackSize = 0;
    analyzer->totalScopes = 0;
    analyzer->maxNestingDepth = 0;
    analyzer->totalVariables = 0;
    
    // Initialize global register usage tracking
    analyzer->globalRegisterUsage = calloc(REGISTER_COUNT, sizeof(uint8_t));
    analyzer->registerInterference = calloc(REGISTER_COUNT * REGISTER_COUNT, sizeof(int));
    analyzer->canCoalesce = calloc(REGISTER_COUNT, sizeof(bool));
    
    // Initialize optimization tracking
    analyzer->hoistableVariables = malloc(sizeof(ScopeVariable*) * ANALYZER_VAR_CAPACITY);
    if (analyzer->hoistableVariables) {
        memset(analyzer->hoistableVariables, 0, sizeof(ScopeVariable*) * ANALYZER_VAR_CAPACITY);
    }
    analyzer->hoistableCount = 0;
    
    // Initialize lifetime analysis
    analyzer->variableLifespans = malloc(sizeof(int) * ANALYZER_LIFESPAN_CAPACITY);
    analyzer->shortLivedVars = malloc(sizeof(bool) * ANALYZER_LIFESPAN_CAPACITY);
    analyzer->longLivedVars = malloc(sizeof(bool) * ANALYZER_LIFESPAN_CAPACITY);
    if (analyzer->variableLifespans) {
        memset(analyzer->variableLifespans, 0, sizeof(int) * ANALYZER_LIFESPAN_CAPACITY);
    }
    if (analyzer->shortLivedVars) {
        memset(analyzer->shortLivedVars, 0, sizeof(bool) * ANALYZER_LIFESPAN_CAPACITY);
    }
    if (analyzer->longLivedVars) {
        memset(analyzer->longLivedVars, 0, sizeof(bool) * ANALYZER_LIFESPAN_CAPACITY);
    }
    
    // Initialize closure capture analysis
    analyzer->capturedVariables = malloc(sizeof(ScopeVariable*) * ANALYZER_VAR_CAPACITY);
    if (analyzer->capturedVariables) {
        memset(analyzer->capturedVariables, 0, sizeof(ScopeVariable*) * ANALYZER_VAR_CAPACITY);
    }
    analyzer->capturedCount = 0;
    analyzer->captureDepths = malloc(sizeof(int) * ANALYZER_VAR_CAPACITY);
    if (analyzer->captureDepths) {
        memset(analyzer->captureDepths, 0, sizeof(int) * ANALYZER_VAR_CAPACITY);
    }
    analyzer->hasNestedFunctions = false;
    
    // Initialize dead variable elimination
    analyzer->deadVariables = malloc(sizeof(ScopeVariable*) * ANALYZER_VAR_CAPACITY);
    if (analyzer->deadVariables) {
        memset(analyzer->deadVariables, 0, sizeof(ScopeVariable*) * ANALYZER_VAR_CAPACITY);
    }
    analyzer->deadCount = 0;
    analyzer->writeOnlyVariables = malloc(sizeof(ScopeVariable*) * ANALYZER_VAR_CAPACITY);
    if (analyzer->writeOnlyVariables) {
        memset(analyzer->writeOnlyVariables, 0, sizeof(ScopeVariable*) * ANALYZER_VAR_CAPACITY);
    }
    analyzer->writeOnlyCount = 0;
    analyzer->eliminatedInstructions = 0;
    analyzer->savedRegisters = 0;
}

// Free scope analyzer resources
void freeScopeAnalyzer(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL) return;
    
    // Free scope tree first to avoid accessing freed memory
    if (analyzer->rootScope) {
        freeScopeTree(analyzer->rootScope);
        analyzer->rootScope = NULL;
    }
    analyzer->currentScope = NULL;
    
    if (analyzer->scopeStack) {
        free(analyzer->scopeStack);
        analyzer->scopeStack = NULL;
    }
    if (analyzer->globalRegisterUsage) {
        free(analyzer->globalRegisterUsage);
        analyzer->globalRegisterUsage = NULL;
    }
    if (analyzer->registerInterference) {
        free(analyzer->registerInterference);
        analyzer->registerInterference = NULL;
    }
    if (analyzer->canCoalesce) {
        free(analyzer->canCoalesce);
        analyzer->canCoalesce = NULL;
    }
    if (analyzer->hoistableVariables) {
        free(analyzer->hoistableVariables);
        analyzer->hoistableVariables = NULL;
    }
    if (analyzer->variableLifespans) {
        free(analyzer->variableLifespans);
        analyzer->variableLifespans = NULL;
    }
    if (analyzer->shortLivedVars) {
        free(analyzer->shortLivedVars);
        analyzer->shortLivedVars = NULL;
    }
    if (analyzer->longLivedVars) {
        free(analyzer->longLivedVars);
        analyzer->longLivedVars = NULL;
    }
    
    // Free closure capture analysis data
    if (analyzer->capturedVariables) {
        free(analyzer->capturedVariables);
        analyzer->capturedVariables = NULL;
    }
    if (analyzer->captureDepths) {
        free(analyzer->captureDepths);
        analyzer->captureDepths = NULL;
    }
    
    // Free dead variable elimination data
    if (analyzer->deadVariables) {
        free(analyzer->deadVariables);
        analyzer->deadVariables = NULL;
    }
    if (analyzer->writeOnlyVariables) {
        free(analyzer->writeOnlyVariables);
        analyzer->writeOnlyVariables = NULL;
    }
    
    // Clear all fields
    analyzer->scopeStackSize = 0;
    analyzer->totalScopes = 0;
    analyzer->hoistableCount = 0;
    analyzer->capturedCount = 0;
    analyzer->deadCount = 0;
    analyzer->writeOnlyCount = 0;
}

// Free scope tree recursively
static void freeScopeTree(ScopeInfo* scope) {
    if (scope == NULL) return;
    
    // Free child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        ScopeInfo* next = child->sibling;
        freeScopeTree(child);
        child = next;
    }
    
    // Free variables in this scope
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        ScopeVariable* next = var->next;
        if (var->name) {
            free(var->name);
        }
        free(var);
        var = next;
    }
    
    // Free scope resources
    if (scope->usedRegisters) {
        free(scope->usedRegisters);
    }
    if (scope->variableLifetimes) {
        free(scope->variableLifetimes);
    }
    if (scope->canShareRegisters) {
        free(scope->canShareRegisters);
    }
    
    free(scope);
}

// Create a new scope
ScopeInfo* createScope(ScopeAnalyzer* analyzer, int depth, int startInstruction, bool isLoopScope) {
    ScopeInfo* scope = malloc(sizeof(ScopeInfo));
    scope->depth = depth;
    scope->startInstruction = startInstruction;
    scope->endInstruction = -1; // Still active
    scope->variables = NULL;
    scope->variableCount = 0;
    scope->isLoopScope = isLoopScope;
    scope->hasNestedScopes = false;
    
    // Initialize register allocation info
    scope->usedRegisters = calloc(REGISTER_COUNT, sizeof(uint8_t));
    scope->registerCount = 0;
    
    // Initialize lifetime analysis
    scope->variableLifetimes = malloc(sizeof(int) * ANALYZER_VAR_CAPACITY);
    scope->canShareRegisters = malloc(sizeof(bool) * ANALYZER_VAR_CAPACITY);
    
    // Initialize tree structure
    scope->parent = analyzer->currentScope;
    scope->children = NULL;
    scope->sibling = NULL;
    
    // Add to parent's children list
    if (analyzer->currentScope != NULL) {
        analyzer->currentScope->hasNestedScopes = true;
        if (analyzer->currentScope->children == NULL) {
            analyzer->currentScope->children = scope;
        } else {
            // Add as sibling to existing children
            ScopeInfo* child = analyzer->currentScope->children;
            while (child->sibling != NULL) {
                child = child->sibling;
            }
            child->sibling = scope;
        }
    }
    
    analyzer->totalScopes++;
    if (depth > analyzer->maxNestingDepth) {
        analyzer->maxNestingDepth = depth;
    }
    
    return scope;
}

// Enter a new scope
void enterScopeAnalysis(ScopeAnalyzer* analyzer, int startInstruction, bool isLoopScope) {
    int depth = analyzer->currentScope ? analyzer->currentScope->depth + 1 : 0;
    ScopeInfo* newScope = createScope(analyzer, depth, startInstruction, isLoopScope);
    
    // Expand scope stack if necessary
    if (analyzer->scopeStackSize >= analyzer->scopeStackCapacity) {
        analyzer->scopeStackCapacity *= 2;
        analyzer->scopeStack = realloc(analyzer->scopeStack, 
                                      sizeof(ScopeInfo*) * analyzer->scopeStackCapacity);
    }
    
    // Push onto scope stack
    analyzer->scopeStack[analyzer->scopeStackSize++] = newScope;
    analyzer->currentScope = newScope;
    
    // Set root scope if this is the first scope
    if (analyzer->rootScope == NULL) {
        analyzer->rootScope = newScope;
    }
}

// Exit current scope
void exitScopeAnalysis(ScopeAnalyzer* analyzer, int endInstruction) {
    if (analyzer->currentScope == NULL || analyzer->scopeStackSize == 0) {
        return;
    }
    
    // Mark scope as ended
    analyzer->currentScope->endInstruction = endInstruction;
    
    // Perform end-of-scope optimizations
    optimizeScopeRegisterAllocation(analyzer->currentScope);
    analyzeVariableLifetimes(analyzer->currentScope);
    
    // Pop from scope stack
    analyzer->scopeStackSize--;
    analyzer->currentScope = analyzer->scopeStackSize > 0 ? 
                            analyzer->scopeStack[analyzer->scopeStackSize - 1] : NULL;
}

// Add variable to current scope
ScopeVariable* addVariableToScope(ScopeAnalyzer* analyzer, const char* name, ValueType type, 
                                 int declarationPoint, uint8_t reg) {
    if (analyzer->currentScope == NULL) {
        return NULL;
    }
    
    ScopeVariable* var = malloc(sizeof(ScopeVariable));
    var->name = strdup(name);
    var->type = type;
    var->declarationPoint = declarationPoint;
    var->firstUse = -1;
    var->lastUse = -1;
    var->escapes = false;
    var->isLoopVar = analyzer->currentScope->isLoopScope;
    var->isLoopInvariant = false;
    var->crossesLoopBoundary = false;
    var->reg = reg;
    var->priority = 0;
    
    // Initialize closure capture analysis fields
    var->isCaptured = false;
    var->isUpvalue = false;
    var->captureDepth = -1;
    var->captureCount = 0;
    var->needsHeapAllocation = false;
    
    // Initialize dead variable elimination fields
    var->isDead = false;
    var->isWriteOnly = false;
    var->isReadOnly = false;
    var->useCount = 0;
    var->writeCount = 0;
    var->hasComplexLifetime = false;
    
    var->next = NULL;
    
    // Add to scope's variable list (simple linked list for now)
    if (analyzer->currentScope->variables == NULL) {
        analyzer->currentScope->variables = var;
    } else {
        var->next = analyzer->currentScope->variables;
        analyzer->currentScope->variables = var;
    }
    
    analyzer->currentScope->variableCount++;
    analyzer->totalVariables++;
    
    // Mark register as used in this scope
    analyzer->currentScope->usedRegisters[reg] = 1;
    analyzer->currentScope->registerCount++;
    
    // Update global register usage
    analyzer->globalRegisterUsage[reg] = 1;
    
    return var;
}

// Find variable in scope hierarchy
ScopeVariable* findVariableInScope(ScopeInfo* scope, const char* name) {
    if (scope == NULL) return NULL;
    
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        if (strcmp(var->name, name) == 0) {
            return var;
        }
        var = var->next;
    }
    
    // Search in parent scope
    return findVariableInScope(scope->parent, name);
}

// Record variable use
void recordVariableUse(ScopeAnalyzer* analyzer, const char* name, int instructionPoint) {
    ScopeVariable* var = findVariableInScope(analyzer->currentScope, name);
    if (var == NULL) return;
    
    // Update first and last use
    if (var->firstUse == -1) {
        var->firstUse = instructionPoint;
    }
    var->lastUse = instructionPoint;
    
    // Check if variable escapes current scope
    if (analyzer->currentScope->depth > 0) {
        ScopeInfo* parentScope = analyzer->currentScope->parent;
        while (parentScope != NULL) {
            if (findVariableInScope(parentScope, name) != NULL) {
                var->escapes = true;
                break;
            }
            parentScope = parentScope->parent;
        }
    }
    
    // Check if variable crosses loop boundaries
    if (analyzer->currentScope->isLoopScope && var->declarationPoint < analyzer->currentScope->startInstruction) {
        var->crossesLoopBoundary = true;
    }
}

// Analyze variable lifetimes in scope
static void analyzeVariableLifetimes(ScopeInfo* scope) {
    if (scope == NULL) return;
    
    ScopeVariable* var = scope->variables;
    int index = 0;
    
    while (var != NULL) {
        if (var->firstUse != -1 && var->lastUse != -1) {
            int lifespan = var->lastUse - var->firstUse;
            scope->variableLifetimes[index] = lifespan;
            
            // Classify variable lifespan
            if (lifespan < 10) {
                // Short-lived variable
                var->priority = 3; // High priority for register allocation
            } else if (lifespan < 100) {
                // Medium-lived variable
                var->priority = 2;
            } else {
                // Long-lived variable
                var->priority = 1; // Lower priority
            }
            
            // Check if variables can share registers
            if (index > 0) {
                for (int i = 0; i < index; i++) {
                    ScopeVariable* otherVar = scope->variables;
                    for (int j = 0; j < i; j++) {
                        otherVar = otherVar->next;
                    }
                    
                    // Variables can share registers if their lifetimes don't overlap
                    if (var->lastUse < otherVar->firstUse || otherVar->lastUse < var->firstUse) {
                        scope->canShareRegisters[index] = true;
                        break;
                    }
                }
            }
        }
        
        var = var->next;
        index++;
    }
}

// Optimize register allocation for scope
static void optimizeScopeRegisterAllocation(ScopeInfo* scope) {
    if (scope == NULL) return;
    
    // Build register interference graph
    buildRegisterInterferenceGraph(scope);
    
    // Identify register coalescing opportunities
    identifyRegisterCoalescing(scope);
    
    // Perform register allocation optimization
    optimizeRegisterAllocation(scope);
}

// Build register interference graph
static void buildRegisterInterferenceGraph(ScopeInfo* scope) {
    ScopeVariable* var1 = scope->variables;
    int index1 = 0;
    
    while (var1 != NULL) {
        ScopeVariable* var2 = scope->variables;
        int index2 = 0;
        
        while (var2 != NULL) {
            if (var1 != var2) {
                // Check if variables interfere (overlapping lifetimes)
                if (!(var1->lastUse < var2->firstUse || var2->lastUse < var1->firstUse)) {
                    // Variables interfere - mark in interference graph
                    // This is a simplified approach - in practice, we'd use a more sophisticated graph
                    scope->canShareRegisters[index1] = false;
                    scope->canShareRegisters[index2] = false;
                }
            }
            var2 = var2->next;
            index2++;
        }
        var1 = var1->next;
        index1++;
    }
}

// Identify register coalescing opportunities
static void identifyRegisterCoalescing(ScopeInfo* scope) {
    ScopeVariable* var = scope->variables;
    
    while (var != NULL) {
        // Look for variables that can be coalesced
        if (var->escapes == false && var->crossesLoopBoundary == false) {
            // This variable is a candidate for coalescing
            ScopeVariable* otherVar = var->next;
            
            while (otherVar != NULL) {
                if (otherVar->escapes == false && otherVar->crossesLoopBoundary == false) {
                    // Check if lifetimes are compatible
                    if (var->lastUse < otherVar->firstUse || otherVar->lastUse < var->firstUse) {
                        // Variables can potentially share the same register
                        // Mark for coalescing consideration
                        var->priority += 1; // Boost priority for coalescing
                    }
                }
                otherVar = otherVar->next;
            }
        }
        var = var->next;
    }
}

// Optimize register allocation
static void optimizeRegisterAllocation(ScopeInfo* scope) {
    // Sort variables by priority (higher priority first)
    sortVariablesByPriority(scope);
    
    // Allocate registers based on priority and interference
    allocateRegistersOptimally(scope);
}

// Sort variables by priority
static void sortVariablesByPriority(ScopeInfo* scope) {
    // Simple bubble sort for now - could be improved with quicksort
    if (scope->variableCount <= 1) return;
    
    ScopeVariable** vars = malloc(sizeof(ScopeVariable*) * scope->variableCount);
    ScopeVariable* var = scope->variables;
    int count = 0;
    
    // Convert linked list to array for easier sorting
    while (var != NULL) {
        vars[count++] = var;
        var = var->next;
    }
    
    // Bubble sort by priority (descending)
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (vars[j]->priority < vars[j + 1]->priority) {
                ScopeVariable* temp = vars[j];
                vars[j] = vars[j + 1];
                vars[j + 1] = temp;
            }
        }
    }
    
    // Rebuild linked list in sorted order
    scope->variables = vars[0];
    for (int i = 0; i < count - 1; i++) {
        vars[i]->next = vars[i + 1];
    }
    vars[count - 1]->next = NULL;
    
    free(vars);
}

// Allocate registers optimally
static void allocateRegistersOptimally(ScopeInfo* scope) {
    uint8_t nextAvailableReg = 1; // Start from register 1 (0 is reserved)
    ScopeVariable* var = scope->variables;
    
    while (var != NULL) {
        // Try to find an available register
        uint8_t assignedReg = findOptimalRegister(scope, var, nextAvailableReg);
        
        if (assignedReg != 0) {
            var->reg = assignedReg;
            scope->usedRegisters[assignedReg] = 1;
            if (assignedReg >= nextAvailableReg) {
                nextAvailableReg = assignedReg + 1;
            }
        } else {
            // No register available - mark for spilling
            var->reg = 0; // Special value indicating spilled
        }
        
        var = var->next;
    }
}

// Find optimal register for variable
static uint8_t findOptimalRegister(ScopeInfo* scope, ScopeVariable* var, uint8_t startReg) {
    // Try to find a register that doesn't interfere with this variable
    for (int reg = startReg; reg < REGISTER_COUNT; reg++) {
        if (scope->usedRegisters[reg] == 0) {
            // Check if this register can be used without interference
            if (canUseRegisterForVariable(scope, var, (uint8_t)reg)) {
                return (uint8_t)reg;
            }
        }
    }
    
    // Try to reuse registers from variables with non-overlapping lifetimes
    ScopeVariable* otherVar = scope->variables;
    while (otherVar != NULL) {
        if (otherVar != var && otherVar->reg != 0) {
            // Check if lifetimes don't overlap
            if (var->lastUse < otherVar->firstUse || otherVar->lastUse < var->firstUse) {
                return otherVar->reg;
            }
        }
        otherVar = otherVar->next;
    }
    
    return 0; // No register available
}

// Check if register can be used for variable
static bool canUseRegisterForVariable(ScopeInfo* scope, ScopeVariable* var, uint8_t reg) {
    // Check for interference with other variables using this register
    ScopeVariable* otherVar = scope->variables;
    
    while (otherVar != NULL) {
        if (otherVar != var && otherVar->reg == reg) {
            // Check if lifetimes overlap
            if (!(var->lastUse < otherVar->firstUse || otherVar->lastUse < var->firstUse)) {
                return false; // Interference detected
            }
        }
        otherVar = otherVar->next;
    }
    
    return true; // No interference
}

// Perform cross-scope optimization analysis
void performCrossScopeOptimization(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL || analyzer->rootScope == NULL) return;
    
    // Verify analyzer is properly initialized
    if (analyzer->globalRegisterUsage == NULL || 
        analyzer->registerInterference == NULL || 
        analyzer->canCoalesce == NULL) {
        return; // Not properly initialized
    }
    
    // Analyze the entire scope tree for optimization opportunities
    analyzeScopeTree(analyzer->rootScope, analyzer);
    
    // Identify variables that can be hoisted
    identifyHoistableVariables(analyzer->rootScope, analyzer);
    
    // Build global register interference graph
    buildGlobalRegisterInterferenceGraph(analyzer);
    
    // Perform global register allocation optimization
    performGlobalRegisterOptimization(analyzer);
}

// Analyze scope tree recursively
static void analyzeScopeTree(ScopeInfo* scope, ScopeAnalyzer* analyzer) {
    if (scope == NULL) return;
    
    // Analyze current scope
    analyzeVariableLifetimes(scope);
    optimizeScopeRegisterAllocation(scope);
    
    // Recursively analyze child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        analyzeScopeTree(child, analyzer);
        child = child->sibling;
    }
}

// Identify hoistable variables
static void identifyHoistableVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer) {
    if (scope == NULL) return;
    
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        // Check if variable can be hoisted to a parent scope
        if (var->isLoopInvariant && !var->crossesLoopBoundary && !var->escapes) {
            // Add to hoistable variables list
            if (analyzer->hoistableCount < ANALYZER_MAX_LIMIT) { // Arbitrary limit
                analyzer->hoistableVariables[analyzer->hoistableCount++] = var;
            }
        }
        var = var->next;
    }
    
    // Recursively check child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        identifyHoistableVariables(child, analyzer);
        child = child->sibling;
    }
}

// Build global register interference graph
static void buildGlobalRegisterInterferenceGraph(ScopeAnalyzer* analyzer) {
    // Reset interference graph
    memset(analyzer->registerInterference, 0, sizeof(int) * REGISTER_COUNT * REGISTER_COUNT);
    
    // Build interference graph from all scopes
    buildGlobalInterferenceFromScope(analyzer->rootScope, analyzer);
}

// Build global interference from scope tree
static void buildGlobalInterferenceFromScope(ScopeInfo* scope, ScopeAnalyzer* analyzer) {
    if (scope == NULL) return;
    
    // Check interference between variables in this scope
    ScopeVariable* var1 = scope->variables;
    while (var1 != NULL) {
        ScopeVariable* var2 = var1->next;
        while (var2 != NULL) {
            if (var1->reg != 0 && var2->reg != 0) {
                // Check if lifetimes overlap
                if (!(var1->lastUse < var2->firstUse || var2->lastUse < var1->firstUse)) {
                    // Mark interference in global graph
                    analyzer->registerInterference[var1->reg * REGISTER_COUNT + var2->reg] = 1;
                    analyzer->registerInterference[var2->reg * REGISTER_COUNT + var1->reg] = 1;
                }
            }
            var2 = var2->next;
        }
        var1 = var1->next;
    }
    
    // Recursively process child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        buildGlobalInterferenceFromScope(child, analyzer);
        child = child->sibling;
    }
}

// Perform global register optimization
static void performGlobalRegisterOptimization(ScopeAnalyzer* analyzer) {
    // Identify registers that can be coalesced globally
    for (int i = 1; i < REGISTER_COUNT; i++) {
        for (int j = i + 1; j < REGISTER_COUNT; j++) {
            if (analyzer->registerInterference[i * REGISTER_COUNT + j] == 0) {
                // Registers i and j don't interfere - they can potentially be coalesced
                analyzer->canCoalesce[i] = true;
                analyzer->canCoalesce[j] = true;
            }
        }
    }
    
    // Apply coalescing optimizations
    applyRegisterCoalescing(analyzer);
}

// Apply register coalescing
static void applyRegisterCoalescing(ScopeAnalyzer* analyzer) {
    // This is a simplified coalescing implementation
    // In practice, this would be more sophisticated
    
    for (int i = 1; i < REGISTER_COUNT; i++) {
        if (analyzer->canCoalesce[i] && analyzer->globalRegisterUsage[i]) {
            // Look for a register to coalesce with
            for (int j = i + 1; j < REGISTER_COUNT; j++) {
                if (analyzer->canCoalesce[j] && analyzer->globalRegisterUsage[j]) {
                    if (analyzer->registerInterference[i * REGISTER_COUNT + j] == 0) {
                        // Coalesce register j into register i
                        coalesceRegisters(analyzer, i, j);
                        break;
                    }
                }
            }
        }
    }
}

// Coalesce two registers
static void coalesceRegisters(ScopeAnalyzer* analyzer, uint8_t targetReg, uint8_t sourceReg) {
    // Update all variables using sourceReg to use targetReg
    updateVariableRegisters(analyzer->rootScope, sourceReg, targetReg);
    
    // Update global usage
    analyzer->globalRegisterUsage[sourceReg] = 0;
    
    // Update interference graph
    for (int i = 0; i < REGISTER_COUNT; i++) {
        analyzer->registerInterference[sourceReg * REGISTER_COUNT + i] = 0;
        analyzer->registerInterference[i * REGISTER_COUNT + sourceReg] = 0;
    }
}

// Update variable registers in scope tree
static void updateVariableRegisters(ScopeInfo* scope, uint8_t oldReg, uint8_t newReg) {
    if (scope == NULL) return;
    
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        if (var->reg == oldReg) {
            var->reg = newReg;
        }
        var = var->next;
    }
    
    // Update scope register usage
    if (scope->usedRegisters[oldReg]) {
        scope->usedRegisters[oldReg] = 0;
        scope->usedRegisters[newReg] = 1;
    }
    
    // Recursively update child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        updateVariableRegisters(child, oldReg, newReg);
        child = child->sibling;
    }
}

// Print scope analysis results (for debugging)
void printScopeAnalysis(ScopeAnalyzer* analyzer) {
    printf("=== Scope Analysis Results ===\n");
    printf("Total scopes: %d\n", analyzer->totalScopes);
    printf("Max nesting depth: %d\n", analyzer->maxNestingDepth);
    printf("Total variables: %d\n", analyzer->totalVariables);
    printf("Hoistable variables: %d\n", analyzer->hoistableCount);
    
    printf("\nScope tree:\n");
    printScopeTree(analyzer->rootScope, 0);
    
    printf("\nGlobal register usage:\n");
    for (int i = 0; i < REGISTER_COUNT; i++) {
        if (analyzer->globalRegisterUsage[i]) {
            printf("Register %d: used\n", i);
        }
    }
}

// Print scope tree recursively
static void printScopeTree(ScopeInfo* scope, int indent) {
    if (scope == NULL) return;
    
    for (int i = 0; i < indent; i++) printf("  ");
    printf("Scope depth %d [%d-%d] %s (%d variables, %d registers)\n",
           scope->depth, scope->startInstruction, scope->endInstruction,
           scope->isLoopScope ? "(loop)" : "",
           scope->variableCount, scope->registerCount);
    
    // Print variables in this scope
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        for (int i = 0; i < indent + 1; i++) printf("  ");
        printf("Variable: %s, reg=%d, lifetime=[%d-%d], priority=%d\n",
               var->name, var->reg, var->firstUse, var->lastUse, var->priority);
        var = var->next;
    }
    
    // Print child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        printScopeTree(child, indent + 1);
        child = child->sibling;
    }
}

// ---------------------------------------------------------------------------
// Integration functions for compiler
// ---------------------------------------------------------------------------

// Initialize scope analysis for compiler
void initCompilerScopeAnalysis(Compiler* compiler) {
    initScopeAnalyzer(&compiler->scopeAnalyzer);
    
    // Create root scope
    enterScopeAnalysis(&compiler->scopeAnalyzer, 0, false);
}

// Finalize scope analysis for compiler
void finalizeCompilerScopeAnalysis(Compiler* compiler) {
    if (compiler == NULL) return;
    
    ScopeAnalyzer* analyzer = &compiler->scopeAnalyzer;
    
    // Check if scope analyzer was properly initialized
    if (analyzer->scopeStack == NULL || analyzer->rootScope == NULL) {
        return; // Not initialized or already cleaned up
    }
    
    // Disable optimizations during cleanup to avoid potential issues
    // Skip cross-scope optimizations for now to prevent segfaults
    // TODO: Re-enable when optimization code is more stable
    
    // Safely exit any remaining scopes without optimizations
    while (analyzer->currentScope != NULL && analyzer->scopeStackSize > 0) {
        // Simple scope cleanup without complex optimizations
        analyzer->scopeStackSize--;
        if (analyzer->scopeStackSize > 0) {
            analyzer->currentScope = analyzer->scopeStack[analyzer->scopeStackSize - 1];
        } else {
            analyzer->currentScope = NULL;
        }
    }
}

// Enter scope in compiler
void compilerEnterScope(Compiler* compiler, bool isLoopScope) {
    if (compiler == NULL || compiler->scopeAnalyzer.scopeStack == NULL) return;
    int currentInstruction = getCurrentInstructionCount(compiler->chunk);
    enterScopeAnalysis(&compiler->scopeAnalyzer, currentInstruction, isLoopScope);
}

// Exit scope in compiler
void compilerExitScope(Compiler* compiler) {
    if (compiler == NULL || compiler->scopeAnalyzer.scopeStack == NULL) return;
    int currentInstruction = getCurrentInstructionCount(compiler->chunk);
    exitScopeAnalysis(&compiler->scopeAnalyzer, currentInstruction);
}

// Declare variable in compiler scope analysis
void compilerDeclareVariable(Compiler* compiler, const char* name, ValueType type, uint8_t reg) {
    if (compiler == NULL || name == NULL || compiler->scopeAnalyzer.scopeStack == NULL) return;
    int currentInstruction = getCurrentInstructionCount(compiler->chunk);
    addVariableToScope(&compiler->scopeAnalyzer, name, type, currentInstruction, reg);
}

// Record variable use in compiler scope analysis
void compilerUseVariable(Compiler* compiler, const char* name) {
    if (compiler == NULL || name == NULL || compiler->scopeAnalyzer.scopeStack == NULL) return;
    int currentInstruction = getCurrentInstructionCount(compiler->chunk);
    recordVariableUse(&compiler->scopeAnalyzer, name, currentInstruction);
}

// Helper function to get current instruction count
static int getCurrentInstructionCount(Chunk* chunk) {
    return chunk ? chunk->count : 0;
}


// Apply scope optimizations to compiler
// TODO: This function is currently unused but should be integrated in the future
// when scope optimizations are re-enabled for stability
__attribute__((unused)) static void applyScopeOptimizationsToCompiler(Compiler* compiler) {
    ScopeAnalyzer* analyzer = &compiler->scopeAnalyzer;
    
    // Update register allocator with optimization results
    updateRegisterAllocatorFromScopeAnalysis(compiler, analyzer);
    
    // Update local variables with optimized register assignments
    updateLocalVariablesFromScopeAnalysis(compiler, analyzer);
    
    // Update type information based on scope analysis
    updateTypeInformationFromScopeAnalysis(compiler, analyzer);
}

// Update register allocator from scope analysis
static void updateRegisterAllocatorFromScopeAnalysis(Compiler* compiler, ScopeAnalyzer* analyzer) {
    RegisterAllocator* regAlloc = &compiler->regAlloc;
    
    // Update live ranges based on scope analysis
    for (int i = 0; i < regAlloc->count; i++) {
        LiveRange* range = &regAlloc->ranges[i];
        
        // Find corresponding variable in scope analysis
        ScopeVariable* var = findVariableInScopeTree(analyzer->rootScope, range->name);
        if (var != NULL) {
            // Update lifetime information
            range->firstUse = var->firstUse;
            range->lastUse = var->lastUse;
            range->escapes = var->escapes;
            range->crossesLoopBoundary = var->crossesLoopBoundary;
            range->isLoopInvariant = var->isLoopInvariant;
            range->priority = var->priority;
            
            // Update register assignment if optimized
            if (var->reg != range->reg && var->reg != 0) {
                range->reg = var->reg;
            }
        }
    }
    
    // Apply register coalescing results
    for (int i = 1; i < REGISTER_COUNT; i++) {
        if (analyzer->canCoalesce[i]) {
            // Mark register as available for coalescing in future allocations
            regAlloc->lastUse[i] = -1; // Reset last use
        }
    }
}

// Update local variables from scope analysis
static void updateLocalVariablesFromScopeAnalysis(Compiler* compiler, ScopeAnalyzer* analyzer) {
    for (int i = 0; i < compiler->localCount; i++) {
        if (compiler->locals[i].name != NULL) {
            ScopeVariable* var = findVariableInScopeTree(analyzer->rootScope, compiler->locals[i].name);
            if (var != NULL) {
                // Update register assignment if optimized
                if (var->reg != 0 && var->reg != compiler->locals[i].reg) {
                    compiler->locals[i].reg = var->reg;
                }
                
                // Update type information if available
                if (var->type != VAL_NIL) {
                    compiler->locals[i].type = var->type;
                    compiler->locals[i].hasKnownType = true;
                    compiler->locals[i].knownType = var->type;
                }
            }
        }
    }
}

// Update type information from scope analysis
static void updateTypeInformationFromScopeAnalysis(Compiler* compiler, ScopeAnalyzer* analyzer) {
    // Update register type tracking
    for (int i = 0; i < REGISTER_COUNT; i++) {
        ScopeVariable* var = findVariableWithRegister(analyzer->rootScope, i);
        if (var != NULL && var->type != VAL_NIL) {
            compiler->registerTypes[i] = var->type;
        }
    }
}

// Helper function to find variable in scope tree
ScopeVariable* findVariableInScopeTree(ScopeInfo* scope, const char* name) {
    if (scope == NULL) return NULL;
    
    // Search in current scope
    ScopeVariable* var = findVariableInScope(scope, name);
    if (var != NULL) return var;
    
    // Search in child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        var = findVariableInScopeTree(child, name);
        if (var != NULL) return var;
        child = child->sibling;
    }
    
    return NULL;
}

// Helper function to find variable in scope chain (upwards search)
ScopeVariable* findVariableInScopeChain(ScopeInfo* scope, const char* name) {
    if (scope == NULL) return NULL;
    
    // Search in current scope first
    ScopeVariable* var = findVariableInScope(scope, name);
    if (var != NULL) return var;
    
    // Search in parent scopes (upward traversal)
    return findVariableInScopeChain(scope->parent, name);
}

// Helper function to find variable with specific register
static ScopeVariable* findVariableWithRegister(ScopeInfo* scope, uint8_t reg) {
    if (scope == NULL) return NULL;
    
    // Search in current scope
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        if (var->reg == reg) return var;
        var = var->next;
    }
    
    // Search in child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        var = findVariableWithRegister(child, reg);
        if (var != NULL) return var;
        child = child->sibling;
    }
    
    return NULL;
}

// ---------------------------------------------------------------------------
// Closure Capture Analysis Implementation
// ---------------------------------------------------------------------------

// Analyze which variables are captured by closures
void analyzeClosureCapture(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL || analyzer->rootScope == NULL) return;
    
    // First pass: identify all nested functions and potential captures
    identifyCapturedVariables(analyzer->rootScope, analyzer);
    
    // Second pass: analyze upvalue usage patterns
    analyzeUpvalueUsage(analyzer);
    
    // Third pass: optimize upvalue allocation
    optimizeUpvalueAllocation(analyzer);
}

// Identify variables that need to be captured as upvalues
static void identifyCapturedVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer) {
    if (scope == NULL) return;
    
    // Check each variable in this scope to see if it's accessed by nested scopes
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        findCapturedVariablesInScope(scope, analyzer);
        var = var->next;
    }
    
    // Recursively analyze child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        identifyCapturedVariables(child, analyzer);
        child = child->sibling;
    }
}

// Find all variables captured from parent scopes
static void findCapturedVariablesInScope(ScopeInfo* scope, ScopeAnalyzer* analyzer) {
    if (scope == NULL || scope->parent == NULL) return;
    
    // Look for variables used in this scope that are declared in parent scopes
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        // Check if this variable is actually defined in a parent scope
        ScopeInfo* parentScope = scope->parent;
        while (parentScope != NULL) {
            ScopeVariable* parentVar = findVariableInScope(parentScope, var->name);
            if (parentVar != NULL) {
                // This variable is captured from a parent scope
                markVariableAsCaptured(parentVar, scope->depth - parentScope->depth);
                var->isUpvalue = true;
                var->captureDepth = parentScope->depth;
                
                // Add to captured variables list
                if (analyzer->capturedCount < ANALYZER_MAX_LIMIT) {
                    analyzer->capturedVariables[analyzer->capturedCount] = parentVar;
                    analyzer->captureDepths[analyzer->capturedCount] = scope->depth - parentScope->depth;
                    analyzer->capturedCount++;
                }
                
                // Determine if heap allocation is needed
                if (needsHeapAllocation(parentVar, analyzer)) {
                    parentVar->needsHeapAllocation = true;
                }
                
                analyzer->hasNestedFunctions = true;
                break;
            }
            parentScope = parentScope->parent;
        }
        var = var->next;
    }
}

// Mark variables as captured by nested functions
static void markVariableAsCaptured(ScopeVariable* var, int captureDepth) {
    if (var == NULL) return;
    
    var->isCaptured = true;
    var->captureDepth = captureDepth;
    var->captureCount++;
    var->escapes = true; // Captured variables always escape their scope
}

// Determine if a variable needs heap allocation for capture
static bool needsHeapAllocation(ScopeVariable* var, ScopeAnalyzer* analyzer) {
    if (var == NULL) return false;
    (void)analyzer; // Suppress unused parameter warning
    
    // Variables captured across multiple scope levels need heap allocation
    if (var->captureDepth > 1) return true;
    
    // Variables with complex lifetimes need heap allocation
    if (var->hasComplexLifetime) return true;
    
    // Variables captured multiple times need heap allocation
    if (var->captureCount > 1) return true;
    
    // Variables that live beyond their declaring scope need heap allocation
    if (var->lastUse > var->declarationPoint + 100) return true; // Heuristic
    
    return false;
}

// Analyze upvalue access patterns for optimization
static void analyzeUpvalueUsage(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL) return;
    
    for (int i = 0; i < analyzer->capturedCount; i++) {
        ScopeVariable* var = analyzer->capturedVariables[i];
        if (var == NULL) continue;
        
        // Analyze read/write patterns
        if (var->useCount > var->writeCount) {
            var->isReadOnly = true;
        } else if (var->writeCount > var->useCount) {
            var->isWriteOnly = true;
        }
        
        // Determine priority for optimization
        var->priority = var->useCount + (var->captureCount * 10);
    }
}

// Optimize upvalue allocation strategy
static void optimizeUpvalueAllocation(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL || analyzer->capturedCount == 0) return;
    
    // Sort captured variables by priority
    for (int i = 0; i < analyzer->capturedCount - 1; i++) {
        for (int j = i + 1; j < analyzer->capturedCount; j++) {
            ScopeVariable* a = analyzer->capturedVariables[i];
            ScopeVariable* b = analyzer->capturedVariables[j];
            if (a != NULL && b != NULL && a->priority < b->priority) {
                // Swap
                analyzer->capturedVariables[i] = b;
                analyzer->capturedVariables[j] = a;
                int tempDepth = analyzer->captureDepths[i];
                analyzer->captureDepths[i] = analyzer->captureDepths[j];
                analyzer->captureDepths[j] = tempDepth;
            }
        }
    }
    
    // Assign optimal registers to high-priority captured variables
    for (int i = 0; i < analyzer->capturedCount && i < 32; i++) {
        ScopeVariable* var = analyzer->capturedVariables[i];
        if (var != NULL && !var->needsHeapAllocation) {
            // Try to keep high-priority captures in registers
            if (var->reg == 0 || analyzer->globalRegisterUsage[var->reg] > 1) {
                // Find a better register
                for (int reg = 1; reg < REGISTER_COUNT; reg++) {
                    if (analyzer->globalRegisterUsage[reg] == 0) {
                        var->reg = reg;
                        analyzer->globalRegisterUsage[reg] = 1;
                        break;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Dead Variable Elimination Implementation
// ---------------------------------------------------------------------------

// Analyze and eliminate dead variables in complex scope hierarchies
void analyzeDeadVariables(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL || analyzer->rootScope == NULL) return;
    
    // First pass: identify dead variables
    identifyDeadVariables(analyzer->rootScope, analyzer);
    
    // Second pass: identify write-only variables
    identifyWriteOnlyVariables(analyzer->rootScope, analyzer);
    
    // Third pass: analyze complex lifetimes
    analyzeComplexLifetimes(analyzer->rootScope, analyzer);
    
    // Fourth pass: perform conservative elimination
    performConservativeElimination(analyzer);
    
    // Calculate savings
    calculateDeadVariableElimination(analyzer);
}

// Identify variables that are never used after declaration
static void identifyDeadVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer) {
    if (scope == NULL) return;
    
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        // A variable is dead if it's never used (firstUse == -1)
        if (var->firstUse == -1 && var->useCount == 0) {
            var->isDead = true;
            
            // Add to dead variables list
            if (analyzer->deadCount < ANALYZER_MAX_LIMIT) {
                analyzer->deadVariables[analyzer->deadCount] = var;
                analyzer->deadCount++;
            }
        }
        
        // Check if variable is used in nested scopes
        if (!var->isDead && !isVariableUsedInNestedScopes(var, scope)) {
            // Variable is only declared but never used anywhere
            if (var->useCount == 0 && var->writeCount <= 1) {
                var->isDead = true;
                
                if (analyzer->deadCount < ANALYZER_MAX_LIMIT) {
                    analyzer->deadVariables[analyzer->deadCount] = var;
                    analyzer->deadCount++;
                }
            }
        }
        
        var = var->next;
    }
    
    // Recursively analyze child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        identifyDeadVariables(child, analyzer);
        child = child->sibling;
    }
}

// Identify variables that are only written to, never read
static void identifyWriteOnlyVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer) {
    if (scope == NULL) return;
    
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        // A variable is write-only if it's written to but never read
        if (var->writeCount > 0 && var->useCount == var->writeCount) {
            // Check if it's really never read (excluding the writes)
            if (var->firstUse == -1 || var->lastUse == var->declarationPoint) {
                var->isWriteOnly = true;
                
                // Add to write-only variables list
                if (analyzer->writeOnlyCount < ANALYZER_MAX_LIMIT) {
                    analyzer->writeOnlyVariables[analyzer->writeOnlyCount] = var;
                    analyzer->writeOnlyCount++;
                }
            }
        }
        
        var = var->next;
    }
    
    // Recursively analyze child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        identifyWriteOnlyVariables(child, analyzer);
        child = child->sibling;
    }
}

// Analyze complex variable lifetimes across scope boundaries
static void analyzeComplexLifetimes(ScopeInfo* scope, ScopeAnalyzer* analyzer) {
    if (scope == NULL) return;
    
    ScopeVariable* var = scope->variables;
    while (var != NULL) {
        // Check if variable has complex lifetime patterns
        if (var->crossesLoopBoundary || var->escapes || var->isCaptured) {
            var->hasComplexLifetime = true;
        }
        
        // Check lifetime span
        if (var->lastUse - var->firstUse > 50) { // Heuristic for long lifetime
            var->hasComplexLifetime = true;
        }
        
        // Variables with complex lifetimes are less likely to be dead
        if (var->hasComplexLifetime && var->isDead) {
            // Be more conservative - only mark as dead if truly unused
            if (var->useCount > 0 || var->isCaptured) {
                var->isDead = false;
                // Remove from dead list if added
                for (int i = 0; i < analyzer->deadCount; i++) {
                    if (analyzer->deadVariables[i] == var) {
                        // Shift remaining elements
                        for (int j = i; j < analyzer->deadCount - 1; j++) {
                            analyzer->deadVariables[j] = analyzer->deadVariables[j + 1];
                        }
                        analyzer->deadCount--;
                        break;
                    }
                }
            }
        }
        
        var = var->next;
    }
    
    // Recursively analyze child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        analyzeComplexLifetimes(child, analyzer);
        child = child->sibling;
    }
}

// Check if a variable is used in nested scopes
static bool isVariableUsedInNestedScopes(ScopeVariable* var, ScopeInfo* scope) {
    if (var == NULL || scope == NULL) return false;
    
    // Check all child scopes
    ScopeInfo* child = scope->children;
    while (child != NULL) {
        // Look for variables with the same name in child scopes
        ScopeVariable* childVar = findVariableInScope(child, var->name);
        if (childVar != NULL && childVar != var) {
            // Found a reference in a child scope
            if (childVar->useCount > 0 || childVar->isUpvalue) {
                return true;
            }
        }
        
        // Recursively check nested child scopes
        if (isVariableUsedInNestedScopes(var, child)) {
            return true;
        }
        
        child = child->sibling;
    }
    
    return false;
}

// Perform conservative dead code elimination
static void performConservativeElimination(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL) return;
    
    // Only eliminate variables that are clearly dead and safe to remove
    for (int i = 0; i < analyzer->deadCount; i++) {
        ScopeVariable* var = analyzer->deadVariables[i];
        if (var == NULL) continue;
        
        // Conservative checks before elimination
        if (var->isDead && !var->isCaptured && !var->escapes && 
            !var->hasComplexLifetime && var->useCount == 0) {
            
            // This variable can be safely eliminated
            // Mark register as available for reuse
            if (analyzer->globalRegisterUsage[var->reg] > 0) {
                analyzer->globalRegisterUsage[var->reg]--;
                analyzer->savedRegisters++;
            }
            
            // Count instructions that can be eliminated
            analyzer->eliminatedInstructions += (var->writeCount + 1); // +1 for declaration
        } else {
            // Too risky to eliminate, remove from dead list
            var->isDead = false;
        }
    }
    
    // Similar conservative elimination for write-only variables
    for (int i = 0; i < analyzer->writeOnlyCount; i++) {
        ScopeVariable* var = analyzer->writeOnlyVariables[i];
        if (var == NULL) continue;
        
        if (var->isWriteOnly && !var->isCaptured && !var->escapes) {
            // Can eliminate the writes to this variable
            analyzer->eliminatedInstructions += var->writeCount;
        }
    }
}

// Count savings from dead variable elimination
static void calculateDeadVariableElimination(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL) return;
    
    int actuallyDeadCount = 0;
    int actuallyWriteOnlyCount = 0;
    
    // Count actually eliminated variables
    for (int i = 0; i < analyzer->deadCount; i++) {
        if (analyzer->deadVariables[i] != NULL && analyzer->deadVariables[i]->isDead) {
            actuallyDeadCount++;
        }
    }
    
    for (int i = 0; i < analyzer->writeOnlyCount; i++) {
        if (analyzer->writeOnlyVariables[i] != NULL && analyzer->writeOnlyVariables[i]->isWriteOnly) {
            actuallyWriteOnlyCount++;
        }
    }
    
    // Update counts to reflect actual eliminations
    analyzer->deadCount = actuallyDeadCount;
    analyzer->writeOnlyCount = actuallyWriteOnlyCount;
}

// ---------------------------------------------------------------------------
// Advanced Analysis Functions
// ---------------------------------------------------------------------------

// Comprehensive analysis combining closure capture and dead variable elimination
void performAdvancedScopeAnalysis(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL) return;
    
    // First, perform closure capture analysis
    analyzeClosureCapture(analyzer);
    
    // Then, perform dead variable elimination (must be after capture analysis)
    analyzeDeadVariables(analyzer);
    
    // Validate results
    if (!validateAnalysisResults(analyzer)) {
        // Rollback aggressive optimizations if validation fails
        analyzer->eliminatedInstructions = 0;
        analyzer->savedRegisters = 0;
        for (int i = 0; i < analyzer->deadCount; i++) {
            if (analyzer->deadVariables[i] != NULL) {
                analyzer->deadVariables[i]->isDead = false;
            }
        }
        analyzer->deadCount = 0;
    }
}

// Generate optimization report
void generateOptimizationReport(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL) return;
    
    printf("\n=== Scope Analysis and Optimization Report ===\n");
    printf("Total scopes analyzed: %d\n", analyzer->totalScopes);
    printf("Maximum nesting depth: %d\n", analyzer->maxNestingDepth);
    printf("Total variables: %d\n", analyzer->totalVariables);
    
    printf("\n--- Closure Capture Analysis ---\n");
    printf("Captured variables: %d\n", analyzer->capturedCount);
    printf("Has nested functions: %s\n", analyzer->hasNestedFunctions ? "Yes" : "No");
    if (analyzer->capturedCount > 0) {
        printf("Variables requiring heap allocation: ");
        int heapCount = 0;
        for (int i = 0; i < analyzer->capturedCount; i++) {
            if (analyzer->capturedVariables[i] != NULL && 
                analyzer->capturedVariables[i]->needsHeapAllocation) {
                heapCount++;
            }
        }
        printf("%d\n", heapCount);
    }
    
    printf("\n--- Dead Variable Elimination ---\n");
    printf("Dead variables eliminated: %d\n", analyzer->deadCount);
    printf("Write-only variables: %d\n", analyzer->writeOnlyCount);
    printf("Instructions eliminated: %d\n", analyzer->eliminatedInstructions);
    printf("Registers saved: %d\n", analyzer->savedRegisters);
    
    if (analyzer->eliminatedInstructions > 0) {
        printf("Estimated size reduction: ~%d%% \n", 
               (analyzer->eliminatedInstructions * 100) / (analyzer->totalVariables * 3));
    }
    
    printf("==============================================\n\n");
}

// Validate analysis results for correctness
static bool validateAnalysisResults(ScopeAnalyzer* analyzer) {
    if (analyzer == NULL) return false;
    
    // Check for internal consistency
    if (analyzer->deadCount > analyzer->totalVariables) return false;
    if (analyzer->capturedCount > analyzer->totalVariables) return false;
    if (analyzer->savedRegisters > REGISTER_COUNT) return false;
    
    // Validate captured variables
    for (int i = 0; i < analyzer->capturedCount; i++) {
        ScopeVariable* var = analyzer->capturedVariables[i];
        if (var == NULL) continue;
        
        // Captured variables should be marked as escaped
        if (var->isCaptured && !var->escapes) return false;
        
        // Upvalues should have valid capture depths
        if (var->isUpvalue && var->captureDepth < 0) return false;
    }
    
    // Validate dead variables
    for (int i = 0; i < analyzer->deadCount; i++) {
        ScopeVariable* var = analyzer->deadVariables[i];
        if (var == NULL) continue;
        
        // Dead variables should not be captured or escaped
        if (var->isDead && (var->isCaptured || var->escapes)) return false;
        
        // Dead variables should have zero use count
        if (var->isDead && var->useCount > 0) return false;
    }
    
    return true;
}