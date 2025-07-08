
```c
// CRITICAL BUG FIX: Function Parameter Resolution
// 
// Root Cause Analysis:
// The issue is in the NODE_IDENTIFIER case of compileExpressionToRegister().
// When resolving identifiers in function bodies, it falls back to allocateRegister()
// without proper error handling, causing parameter lookups to fail silently.

// IN FILE: compiler.c, around line 800-850 in compileExpressionToRegister()

case NODE_IDENTIFIER: {
    const char* name = node->identifier.name;
    
    int localIndex;
    if (symbol_table_get(&compiler->symbols, name, &localIndex)) {
        // SUCCESS PATH: Parameter found in function compiler's symbol table
        // Integrate with scope analysis - track variable usage
        compilerUseVariable(compiler, name);
        return compiler->locals[localIndex].reg;
    }
    
    // BUG IS HERE: This fallback should NOT happen for function parameters!
    // When symbol_table_get() fails, we silently allocate a new register
    // instead of reporting an error for undefined variables.
    
    // CURRENT BUGGY CODE:
    uint8_t reg = allocateRegister(compiler);  // This uses wrong compiler!
    emitConstant(compiler, reg, I32_VAL(0));   // Defaults to 0
    return reg;
    
    // The problem: This fallback masks the real issue - why isn't the
    // parameter found in the symbol table? 
}

// POTENTIAL FIXES TO INVESTIGATE:

// Fix 1: Better error handling
case NODE_IDENTIFIER: {
    const char* name = node->identifier.name;
    
    int localIndex;
    if (symbol_table_get(&compiler->symbols, name, &localIndex)) {
        compilerUseVariable(compiler, name);
        return compiler->locals[localIndex].reg;
    }
    
    // CHANGE: Report undefined variable instead of silent fallback
    fprintf(stderr, "Error: Undefined variable '%s'\n", name);
    compiler->hadError = true;
    return -1;  // Return error instead of silently creating register
}

// Fix 2: Debug the symbol table setup
// In NODE_FUNCTION case, let's verify the symbol table is set up correctly:

// CURRENT CODE in NODE_FUNCTION:
for (int i = 0; i < node->function.paramCount; i++) {
    char* paramName = node->function.params[i].name;
    functionCompiler.locals[functionCompiler.localCount].name = paramName;
    functionCompiler.locals[functionCompiler.localCount].reg = i;
    // ... other setup ...
    
    // Add to symbol table for lookup - CRITICAL: Use current localCount before incrementing
    symbol_table_set(&functionCompiler.symbols, paramName, functionCompiler.localCount);
    
    functionCompiler.localCount++;
}

// POTENTIAL BUG: Check if symbol_table_set is working correctly
// Add debugging to verify:

// DEBUGGING VERSION:
for (int i = 0; i < node->function.paramCount; i++) {
    char* paramName = node->function.params[i].name;
    functionCompiler.locals[functionCompiler.localCount].name = paramName;
    functionCompiler.locals[functionCompiler.localCount].reg = i;
    functionCompiler.locals[functionCompiler.localCount].isActive = true;
    functionCompiler.locals[functionCompiler.localCount].depth = 0;
    functionCompiler.locals[functionCompiler.localCount].isMutable = false;
    
    // Store localCount BEFORE incrementing
    int currentLocalIndex = functionCompiler.localCount;
    
    // Add to symbol table
    symbol_table_set(&functionCompiler.symbols, paramName, currentLocalIndex);
    
    // DEBUG: Verify the symbol was stored
    int testIndex;
    bool found = symbol_table_get(&functionCompiler.symbols, paramName, &testIndex);
    printf("DEBUG: Parameter '%s' -> localIndex %d, found=%s, testIndex=%d\n", 
           paramName, currentLocalIndex, found ? "true" : "false", testIndex);
    
    functionCompiler.localCount++;
}

// Fix 3: Verify compiler instance propagation
// The real issue might be that somewhere in the call chain, we're not
// properly passing the functionCompiler instance. Let's trace:

// 1. NODE_FUNCTION calls compileExpressionToRegister(node->function.body, &functionCompiler)
// 2. This might call other functions that don't properly forward the compiler
// 3. Check if any function calls use a global 'compiler' instead of the passed parameter

// CRITICAL INSPECTION POINTS:
// - Check all function calls within compileExpressionToRegister()
// - Look for global compiler usage instead of parameter usage
// - Verify that inferBinaryOpTypeWithCompiler() uses the right compiler
// - Check symbol_table_get implementation for corruption

// Fix 4: Symbol Table Implementation Check
// The bug might be in symbol_table_get itself. Verify implementation:

bool symbol_table_get(SymbolTable* table, const char* name, int* localIndex) {
    // Ensure this function is correctly implemented
    // Check for string comparison issues
    // Verify table structure isn't corrupted
    
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->entries[i].name, name) == 0) {
            *localIndex = table->entries[i].localIndex;
            return true;
        }
    }
    return false;
}

// LIKELY ROOT CAUSES (in order of probability):

// 1. Symbol table corruption during function compilation
// 2. Wrong compiler instance passed to inferBinaryOpTypeWithCompiler()
// 3. Global compiler variable being used instead of parameter
// 4. Memory corruption in functionCompiler setup
// 5. Register allocation using wrong compiler instance

// RECOMMENDED DEBUGGING STEPS:

// Step 1: Add comprehensive debug output to NODE_IDENTIFIER
case NODE_IDENTIFIER: {
    const char* name = node->identifier.name;
    
    printf("DEBUG: Looking up identifier '%s' in compiler with localCount=%d\n", 
           name, compiler->localCount);
    
    // Print all symbols in table
    printf("DEBUG: Symbol table contents:\n");
    for (int i = 0; i < compiler->symbols.count; i++) {
        printf("  [%d] '%s' -> localIndex %d\n", 
               i, compiler->symbols.entries[i].name, 
               compiler->symbols.entries[i].localIndex);
    }
    
    int localIndex;
    if (symbol_table_get(&compiler->symbols, name, &localIndex)) {
        printf("DEBUG: Found '%s' at localIndex %d, reg %d\n", 
               name, localIndex, compiler->locals[localIndex].reg);
        compilerUseVariable(compiler, name);
        return compiler->locals[localIndex].reg;
    }
    
    printf("ERROR: Undefined variable '%s' - this should not happen for parameters!\n", name);
    compiler->hadError = true;
    return -1;
}

// Step 2: Verify functionCompiler integrity before compilation
printf("DEBUG: About to compile function body with functionCompiler:\n");
printf("  localCount = %d\n", functionCompiler.localCount);
printf("  symbols.count = %d\n", functionCompiler.symbols.count);
for (int i = 0; i < functionCompiler.localCount; i++) {
    printf("  locals[%d]: name='%s', reg=%d, active=%s\n",
           i, functionCompiler.locals[i].name, functionCompiler.locals[i].reg,
           functionCompiler.locals[i].isActive ? "true" : "false");
}

// Compile function body in the new context
int resultReg = compileExpressionToRegister(node->function.body, &functionCompiler);

// This debug output will reveal:
// 1. Whether parameters are correctly stored in symbol table
// 2. Whether the right compiler instance is being used
// 3. Where the lookup is failing

// IMMEDIATE ACTION ITEM:
// Replace the silent fallback in NODE_IDENTIFIER with proper error reporting
// to identify exactly when and why parameter lookup fails.
```

```c
// CRITICAL FIX: Replace the NODE_IDENTIFIER case in compileExpressionToRegister()
// File: compiler.c, around line 800-850

case NODE_IDENTIFIER: {
    const char* name = node->identifier.name;
    
    int localIndex;
    if (symbol_table_get(&compiler->symbols, name, &localIndex)) {
        // SUCCESS: Found the variable in symbol table
        compilerUseVariable(compiler, name);
        
        // Verify the local is valid
        if (localIndex >= 0 && localIndex < compiler->localCount && 
            compiler->locals[localIndex].isActive) {
            return compiler->locals[localIndex].reg;
        } else {
            // Invalid local index - this shouldn't happen
            fprintf(stderr, "Internal error: Invalid local index %d for '%s'\n", 
                    localIndex, name);
            compiler->hadError = true;
            return -1;
        }
    }
    
    // CHANGE: Instead of silent fallback, report undefined variable error
    fprintf(stderr, "Error: Undefined variable '%s'\n", name);
    compiler->hadError = true;
    return -1;
    
    // OLD BUGGY CODE (REMOVE THIS):
    // uint8_t reg = allocateRegister(compiler);
    // emitConstant(compiler, reg, I32_VAL(0));
    // return reg;
}
```


```c
// COMPREHENSIVE DEBUG PATCH for Function Parameter Bug
// Apply this to the NODE_FUNCTION case in compileExpressionToRegister()

case NODE_FUNCTION: {
    // Create function object and store it in a register
    uint8_t reg = allocateRegister(compiler);
    
    // Check if we have room for another function
    if (vm.functionCount >= UINT8_COUNT) {
        return -1;
    }
    
    // Create a new chunk for the function body
    Chunk* functionChunk = (Chunk*)malloc(sizeof(Chunk));
    initChunk(functionChunk);
    
    // Create a new compiler context for the function
    Compiler functionCompiler;
    initCompiler(&functionCompiler, functionChunk, compiler->fileName, compiler->source);
    
    printf("=== FUNCTION COMPILATION DEBUG ===\n");
    printf("Main compiler localCount: %d\n", compiler->localCount);
    printf("Function compiler localCount: %d\n", functionCompiler.localCount);
    printf("Parameter count: %d\n", node->function.paramCount);
    
    // Set up function parameters as local variables
    for (int i = 0; i < node->function.paramCount; i++) {
        char* paramName = node->function.params[i].name;
        
        printf("Setting up parameter %d: '%s'\n", i, paramName);
        
        // Setup local variable
        functionCompiler.locals[functionCompiler.localCount].name = paramName;
        functionCompiler.locals[functionCompiler.localCount].reg = i;
        functionCompiler.locals[functionCompiler.localCount].isActive = true;
        functionCompiler.locals[functionCompiler.localCount].depth = 0;
        functionCompiler.locals[functionCompiler.localCount].isMutable = false;
        
        // Store current index before incrementing
        int currentLocalIndex = functionCompiler.localCount;
        
        // Add to symbol table
        symbol_table_set(&functionCompiler.symbols, paramName, currentLocalIndex);
        
        // Verify symbol table entry
        int testIndex;
        bool found = symbol_table_get(&functionCompiler.symbols, paramName, &testIndex);
        printf("  Symbol table test: '%s' -> found=%s, index=%d\n", 
               paramName, found ? "YES" : "NO", testIndex);
        
        functionCompiler.localCount++;
    }
    
    // Set next register to start after parameters
    functionCompiler.nextRegister = node->function.paramCount;
    
    printf("Function compiler state before body compilation:\n");
    printf("  localCount = %d\n", functionCompiler.localCount);
    printf("  nextRegister = %d\n", functionCompiler.nextRegister);
    printf("  symbols.count = %d\n", functionCompiler.symbols.count);
    
    for (int i = 0; i < functionCompiler.localCount; i++) {
        printf("  locals[%d]: name='%s', reg=%d, active=%s\n",
               i, functionCompiler.locals[i].name, 
               functionCompiler.locals[i].reg,
               functionCompiler.locals[i].isActive ? "true" : "false");
    }
    
    printf("About to compile function body...\n");
    
    // Compile function body in the new context
    int resultReg = compileExpressionToRegister(node->function.body, &functionCompiler);
    
    printf("Function body compilation result: %d\n", resultReg);
    printf("Function compiler localCount after: %d\n", functionCompiler.localCount);
    
    if (resultReg < 0) {
        printf("ERROR: Function body compilation failed!\n");
        freeChunk(functionChunk);
        free(functionChunk);
        return -1;
    }
    
    // Add implicit return for the last expression
    emitByte(&functionCompiler, OP_RETURN_R);
    emitByte(&functionCompiler, (uint8_t)resultReg);
    
    // Store function in VM functions array
    int functionIndex = vm.functionCount++;
    vm.functions[functionIndex].chunk = functionChunk;
    vm.functions[functionIndex].arity = node->function.paramCount;
    vm.functions[functionIndex].start = 0;
    
    // Store function index as constant
    Value funcValue = I32_VAL(functionIndex);
    emitConstant(compiler, reg, funcValue);
    
    printf("=== FUNCTION COMPILATION COMPLETE ===\n\n");
    
    return reg;
}

// ALSO ADD THIS DEBUG VERSION OF NODE_IDENTIFIER:

case NODE_IDENTIFIER: {
    const char* name = node->identifier.name;
    
    printf("IDENTIFIER DEBUG: Looking up '%s'\n", name);
    printf("  Compiler localCount: %d\n", compiler->localCount);
    printf("  Symbol table count: %d\n", compiler->symbols.count);
    
    // Print all symbols in table
    for (int i = 0; i < compiler->symbols.count; i++) {
        printf("  Symbol[%d]: '%s' -> localIndex %d\n", 
               i, compiler->symbols.entries[i].name, 
               compiler->symbols.entries[i].localIndex);
    }
    
    int localIndex;
    if (symbol_table_get(&compiler->symbols, name, &localIndex)) {
        printf("  FOUND: '%s' at localIndex %d\n", name, localIndex);
        
        if (localIndex >= 0 && localIndex < compiler->localCount && 
            compiler->locals[localIndex].isActive) {
            uint8_t reg = compiler->locals[localIndex].reg;
            printf("  Returning register: %d\n", reg);
            compilerUseVariable(compiler, name);
            return reg;
        } else {
            printf("  ERROR: Invalid local index %d\n", localIndex);
            compiler->hadError = true;
            return -1;
        }
    }
    
    printf("  NOT FOUND: '%s' is undefined\n", name);
    compiler->hadError = true;
    return -1;
}
```















Looking at your debugging analysis, you've correctly identified the core issue: **compiler instance aliasing during function compilation**. The function compiler context (`&functionCompiler`) is being created correctly, but somewhere in the call chain, the main compiler instance is being used instead.

Let me trace through the code and identify the likely cause:Now let me create a focused fix for the immediate issue:Now let me create a comprehensive debugging patch to identify the exact issue:## Summary

The bug is in the **NODE_IDENTIFIER case** of `compileExpressionToRegister()`. Here's what's happening:

### The Problem
When a function parameter isn't found in the symbol table, instead of reporting an error, the code silently falls back to:
```c
uint8_t reg = allocateRegister(compiler);
emitConstant(compiler, reg, I32_VAL(0));
return reg;
```

This masks the real issue - **why isn't the parameter found in the symbol table?**

### The Fix
1. **Replace the silent fallback** with proper error reporting
2. **Add comprehensive debugging** to see exactly where the lookup fails
3. **Verify symbol table integrity** during function compilation

### Debugging Steps
1. Apply the debug patch to see:
   - Whether parameters are correctly stored in the symbol table
   - Whether the right compiler instance is being used  
   - Where exactly the lookup fails

2. Run your test case:
   ```orus
   fn test(a: i32, b: i32) -> i32:
       b
   result = test(100, 200)
   print(result)
   ```

3. The debug output will reveal whether:
   - Parameters are stored correctly (`Symbol table test: 'b' -> found=YES`)
   - The right compiler is used (`Compiler localCount: 2` in identifier lookup)
   - Symbol table is corrupted between setup and lookup

### Most Likely Root Causes
1. **Symbol table corruption** during function compilation
2. **Wrong compiler instance** being passed somewhere in the call chain
3. **Memory corruption** in functionCompiler setup
4. **String comparison issues** in symbol_table_get()

Apply the debug patch and the error will become immediately visible. Once we see the debug output, we can pinpoint the exact cause and fix it definitively.