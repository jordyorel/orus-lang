#ifndef SCOPE_ANALYSIS_H
#define SCOPE_ANALYSIS_H

#include "vm/vm.h"

// ---------------------------------------------------------------------------
// Compile-time Scope Analysis and Register Allocation Optimization
// ---------------------------------------------------------------------------

// Initialize scope analyzer
void initScopeAnalyzer(ScopeAnalyzer* analyzer);

// Free scope analyzer resources
void freeScopeAnalyzer(ScopeAnalyzer* analyzer);

// Free scope tree recursively
void freeScopeTree(ScopeInfo* scope);

// Scope management functions
ScopeInfo* createScope(ScopeAnalyzer* analyzer, int depth, int startInstruction, bool isLoopScope);
void enterScopeAnalysis(ScopeAnalyzer* analyzer, int startInstruction, bool isLoopScope);
void exitScopeAnalysis(ScopeAnalyzer* analyzer, int endInstruction);

// Variable management functions
ScopeVariable* addVariableToScope(ScopeAnalyzer* analyzer, const char* name, ValueType type, 
                                 int declarationPoint, uint8_t reg);
ScopeVariable* findVariableInScope(ScopeInfo* scope, const char* name);
void recordVariableUse(ScopeAnalyzer* analyzer, const char* name, int instructionPoint);

// Lifetime analysis functions
void analyzeVariableLifetimes(ScopeInfo* scope);
void optimizeScopeRegisterAllocation(ScopeInfo* scope);
void buildRegisterInterferenceGraph(ScopeInfo* scope);
void identifyRegisterCoalescing(ScopeInfo* scope);
void optimizeRegisterAllocation(ScopeInfo* scope);

// Register allocation optimization functions
void sortVariablesByPriority(ScopeInfo* scope);
void allocateRegistersOptimally(ScopeInfo* scope);
uint8_t findOptimalRegister(ScopeInfo* scope, ScopeVariable* var, uint8_t startReg);
bool canUseRegisterForVariable(ScopeInfo* scope, ScopeVariable* var, uint8_t reg);

// Cross-scope optimization functions
void performCrossScopeOptimization(ScopeAnalyzer* analyzer);
void analyzeScopeTree(ScopeInfo* scope, ScopeAnalyzer* analyzer);
void identifyHoistableVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer);
void buildGlobalRegisterInterferenceGraph(ScopeAnalyzer* analyzer);
void buildGlobalInterferenceFromScope(ScopeInfo* scope, ScopeAnalyzer* analyzer);
void performGlobalRegisterOptimization(ScopeAnalyzer* analyzer);

// Register coalescing functions
void applyRegisterCoalescing(ScopeAnalyzer* analyzer);
void coalesceRegisters(ScopeAnalyzer* analyzer, uint8_t targetReg, uint8_t sourceReg);
void updateVariableRegisters(ScopeInfo* scope, uint8_t oldReg, uint8_t newReg);

// Debug and analysis functions
void printScopeAnalysis(ScopeAnalyzer* analyzer);
void printScopeTree(ScopeInfo* scope, int indent);

// Integration functions for compiler
void initCompilerScopeAnalysis(Compiler* compiler);
void finalizeCompilerScopeAnalysis(Compiler* compiler);
void compilerEnterScope(Compiler* compiler, bool isLoopScope);
void compilerExitScope(Compiler* compiler);
void compilerDeclareVariable(Compiler* compiler, const char* name, ValueType type, uint8_t reg);
void compilerUseVariable(Compiler* compiler, const char* name);

// Internal integration helper functions
void applyScopeOptimizationsToCompiler(Compiler* compiler);
void updateRegisterAllocatorFromScopeAnalysis(Compiler* compiler, ScopeAnalyzer* analyzer);
void updateLocalVariablesFromScopeAnalysis(Compiler* compiler, ScopeAnalyzer* analyzer);
void updateTypeInformationFromScopeAnalysis(Compiler* compiler, ScopeAnalyzer* analyzer);
ScopeVariable* findVariableInScopeTree(ScopeInfo* scope, const char* name);
ScopeVariable* findVariableInScopeChain(ScopeInfo* scope, const char* name);
ScopeVariable* findVariableWithRegister(ScopeInfo* scope, uint8_t reg);

// ---------------------------------------------------------------------------
// Closure Capture Analysis Functions
// ---------------------------------------------------------------------------

// Analyze which variables are captured by closures
void analyzeClosureCapture(ScopeAnalyzer* analyzer);

// Identify variables that need to be captured as upvalues
void identifyCapturedVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer);

// Determine if a variable needs heap allocation for capture
bool needsHeapAllocation(ScopeVariable* var, ScopeAnalyzer* analyzer);

// Analyze upvalue access patterns for optimization
void analyzeUpvalueUsage(ScopeAnalyzer* analyzer);

// Mark variables as captured by nested functions
void markVariableAsCaptured(ScopeVariable* var, int captureDepth);

// Find all variables captured from parent scopes
void findCapturedVariablesInScope(ScopeInfo* scope, ScopeAnalyzer* analyzer);

// Optimize upvalue allocation strategy
void optimizeUpvalueAllocation(ScopeAnalyzer* analyzer);

// ---------------------------------------------------------------------------
// Dead Variable Elimination Functions
// ---------------------------------------------------------------------------

// Analyze and eliminate dead variables in complex scope hierarchies
void analyzeDeadVariables(ScopeAnalyzer* analyzer);

// Identify variables that are never used after declaration
void identifyDeadVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer);

// Identify variables that are only written to, never read
void identifyWriteOnlyVariables(ScopeInfo* scope, ScopeAnalyzer* analyzer);

// Eliminate dead variable assignments and declarations
void eliminateDeadVariables(ScopeAnalyzer* analyzer);

// Analyze complex variable lifetimes across scope boundaries
void analyzeComplexLifetimes(ScopeInfo* scope, ScopeAnalyzer* analyzer);

// Check if a variable is used in nested scopes
bool isVariableUsedInNestedScopes(ScopeVariable* var, ScopeInfo* scope);

// Perform conservative dead code elimination
void performConservativeElimination(ScopeAnalyzer* analyzer);

// Count savings from dead variable elimination
void calculateDeadVariableElimination(ScopeAnalyzer* analyzer);

// ---------------------------------------------------------------------------
// Advanced Analysis Functions
// ---------------------------------------------------------------------------

// Comprehensive analysis combining closure capture and dead variable elimination
void performAdvancedScopeAnalysis(ScopeAnalyzer* analyzer);

// Generate optimization report
void generateOptimizationReport(ScopeAnalyzer* analyzer);

// Validate analysis results for correctness
bool validateAnalysisResults(ScopeAnalyzer* analyzer);

#endif // SCOPE_ANALYSIS_H