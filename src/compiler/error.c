#include "error.h"
#include "compiler.h"
#include "scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include "string_utils.h"
#include <string.h>
#include <ctype.h>

/**
 * @file error.c
 * @brief Diagnostic and error reporting helpers.
 *
 * Routines in this file build rich error messages used by the compiler. They
 * format source spans and suggestions to aid in debugging.
 */

/**
 * Fetch a specific line from a source file for display in a diagnostic.
 *
 * @param filePath Path to the source file.
 * @param lineNum  Line number to retrieve (1-based).
 * @return Pointer to a static buffer containing the line text or NULL.
 */
static const char* getSourceLine(const char* filePath, int lineNum) {
    FILE* file = fopen(filePath, "r");
    if (!file) return NULL;

    static char buffer[1024];
    int currentLine = 1;

    while (fgets(buffer, sizeof(buffer), file)) {
        if (currentLine == lineNum) {
            fclose(file);
            char* newline = strchr(buffer, '\n');
            if (newline) *newline = '\0';
            return buffer;
        }
        currentLine++;
    }

    fclose(file);
    return NULL;
}
/**
 * Suggest the closest symbol name for a potential typo.
 *
 * @param compiler Active compiler instance.
 * @param name     Identifier that failed to resolve.
 * @return Suggested symbol name or NULL.
 */
static const char* suggestClosestSymbol(Compiler* compiler, const char* name) {
    const char* best = NULL;
    int bestDist = 4;
    for (int i = 0; i < compiler->symbols.count; i++) {
        Symbol* sym = &compiler->symbols.symbols[i];
        if (!sym->active) continue;
        int dist = levenshteinDistance(name, sym->name);
        if (dist < bestDist) {
            bestDist = dist;
            best = sym->name;
        }
    }
    return best;
}


/**
 * Print a formatted diagnostic message with source context.
 *
 * @param diagnostic Populated diagnostic structure to display.
 */
void emitDiagnostic(Diagnostic* diagnostic) {
    // 1. Header with error code and message
    const char* category = "Compile error";
    if (diagnostic->code == (ErrorCode)ERROR_RUNTIME) {
        category = "Runtime error";
    } else if (diagnostic->code == (ErrorCode)ERROR_TYPE &&
               diagnostic->code != ERROR_PARSE) {
        category = "Runtime type error";
    } else if (diagnostic->code == (ErrorCode)ERROR_IO) {
        category = "Runtime I/O error";
    }

    printf("%s%s [E%04d]%s: %s\n",
           COLOR_RED, category, diagnostic->code, COLOR_RESET,
           diagnostic->text.message);

    // 2. File location
    printf("%s --> %s:%d:%d%s\n",
           COLOR_CYAN, diagnostic->primarySpan.filePath,
           diagnostic->primarySpan.line, diagnostic->primarySpan.column,
           COLOR_RESET);

    // 3. Grab line of source code
    const char* sourceLine = diagnostic->sourceText;
    if (!sourceLine && diagnostic->primarySpan.filePath) {
        sourceLine = getSourceLine(diagnostic->primarySpan.filePath,
                                   diagnostic->primarySpan.line);
    }

    if (sourceLine) {
        printf(" %s%4d |%s %s\n", COLOR_BLUE,
               diagnostic->primarySpan.line, COLOR_RESET, sourceLine);
        printf("      | ");
        
        // Calculate precise display column accounting for tabs
        const char* lineStart = findLineStart(diagnostic->sourceText ? diagnostic->sourceText : sourceLine, sourceLine);
        int displayCol = 1;
        for (int i = 0; i < diagnostic->primarySpan.column - 1; i++) {
            char c = (lineStart && i < strlen(lineStart)) ? lineStart[i] : ' ';
            if (c == '\t') {
                // Advance to next tab stop
                int nextTab = ((displayCol - 1) / 4 + 1) * 4 + 1;
                while (displayCol < nextTab) {
                    putchar(' ');
                    displayCol++;
                }
            } else {
                putchar(' ');
                displayCol++;
            }
        }
        
        printf("%s", COLOR_RED);
        for (int i = 0; i < diagnostic->primarySpan.length; i++) {
            putchar('^');
        }
        printf("%s\n", COLOR_RESET);
    }

    // 4. Secondary spans
    for (int i = 0; i < diagnostic->secondarySpanCount; i++) {
        SourceSpan* span = &diagnostic->secondarySpans[i];
        sourceLine = getSourceLine(span->filePath, span->line);
        if (sourceLine) {
            printf(" %s%4d |%s %s\n", COLOR_BLUE, span->line, COLOR_RESET, sourceLine);
            printf("      | ");
            
            // Calculate precise display column for secondary spans too
            const char* lineStart = findLineStart(sourceLine, sourceLine);
            int displayCol = 1;
            for (int j = 0; j < span->column - 1; j++) {
                char c = (lineStart && j < strlen(lineStart)) ? lineStart[j] : ' ';
                if (c == '\t') {
                    // Advance to next tab stop
                    int nextTab = ((displayCol - 1) / 4 + 1) * 4 + 1;
                    while (displayCol < nextTab) {
                        putchar(' ');
                        displayCol++;
                    }
                } else {
                    putchar(' ');
                    displayCol++;
                }
            }
            
            printf("%s", COLOR_CYAN);
            for (int j = 0; j < span->length; j++) putchar('^');
            printf("%s\n", COLOR_RESET);
        }
    }

    // 5. Help message
    if (diagnostic->text.help) {
        printf("%shelp%s: %s\n", COLOR_GREEN, COLOR_RESET, diagnostic->text.help);
    }

    // 6. Notes
    for (int i = 0; i < diagnostic->text.noteCount; i++) {
        printf("%snote%s: %s\n", COLOR_BLUE, COLOR_RESET,
               diagnostic->text.notes[i]);
    }

    printf("\n");
}

// Convenience helpers -------------------------------------------------------

// Emit an undefined variable error with an optional definition location.
void emitUndefinedVarError(Compiler* compiler,
                           Token* useToken,
                           Token* defToken,
                           const char* name) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = ERROR_UNDEFINED_VARIABLE;
    diagnostic.primarySpan.line = useToken->line;
    diagnostic.primarySpan.column = useToken->column;
    diagnostic.primarySpan.length = useToken->length;
    diagnostic.primarySpan.filePath = compiler->filePath;

    // Buffer for the help text and notes
    char helpBuffer[256];
    char* note = NULL;
    
    // Secondary span for definition location if available
    if (defToken) {
        diagnostic.secondarySpanCount = 1;
        diagnostic.secondarySpans = malloc(sizeof(SourceSpan));
        diagnostic.secondarySpans[0].line = defToken->line;
        diagnostic.secondarySpans[0].column = defToken->column;
        diagnostic.secondarySpans[0].length = defToken->length;
        diagnostic.secondarySpans[0].filePath = compiler->filePath;
        
        // Provide context-specific help based on the variable's location
        // Get an estimate of scope relationship by checking line numbers
        // This is approximate since we don't store scope depth in tokens directly
        if (defToken->line < useToken->line) {
            // Variable defined before use, likely an inner scope issue
            snprintf(helpBuffer, sizeof(helpBuffer),
                "variable `%s` was defined on line %d but is no longer accessible in this scope", 
                name, defToken->line);
            note = "variables declared inside blocks (between { }) are only accessible within that block. Try declaring the variable in a common outer scope, or restructure your code to use the variable within its original scope";
        } else {
            // Variable defined after use, likely a declaration order issue
            snprintf(helpBuffer, sizeof(helpBuffer),
                "variable `%s` is defined on line %d but used before its declaration", 
                name, defToken->line);
            note = "in Orus, variables must be declared before they are used. Move the declaration above this line, or check if you meant to use a different variable";
        }
    } else {
        // No definition found, the variable doesn't exist at all
        // Provide more helpful suggestions based on variable name patterns
        if (strstr(name, "_")) {
            snprintf(helpBuffer, sizeof(helpBuffer),
                "could not find a declaration of `%s` in this scope. If this is a parameter or local variable, declare it with `let %s = value` or add it as a function parameter", 
                name, name);
            note = "variables with underscores are often parameters or local variables. Check your function signature and variable declarations";
        } else if (strlen(name) == 1) {
            snprintf(helpBuffer, sizeof(helpBuffer),
                "could not find a declaration of `%s` in this scope. Single-letter variables need to be declared with `let %s = value` before use", 
                name, name);
            note = "if this is a loop counter, make sure you're inside a for loop. For regular variables, declare them with `let` first";
        } else {
            snprintf(helpBuffer, sizeof(helpBuffer),
                "could not find a declaration of `%s` in this scope. Declare it with `let %s = value` or check for typos", 
                name, name);
            note = "make sure the variable is declared before use, spelled correctly, and accessible in the current scope. Check if you need to import it from another module";
        }
    }

    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
             "cannot find variable `%s` in this scope", name);
    diagnostic.text.message = msgBuffer;

    diagnostic.text.help = strdup(helpBuffer);

    char suggestNote[64];
    char* notes[2];
    notes[0] = note;
    int noteCount = 1;
    const char* suggestion = suggestClosestSymbol(compiler, name);
    if (suggestion && strcmp(suggestion, name) != 0) {
        snprintf(suggestNote, sizeof(suggestNote),
                 "did you mean `%s`?", suggestion);
        notes[noteCount++] = suggestNote;
    }

    diagnostic.text.notes = notes;
    diagnostic.text.noteCount = noteCount;

    emitDiagnostic(&diagnostic);

    // Clean up allocated memory
    if (diagnostic.secondarySpans) free(diagnostic.secondarySpans);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

// Emit a type mismatch error between expected and actual types.
void emitTypeMismatchError(Compiler* compiler,
                           Token* token,
                           const char* expectedType,
                           const char* actualType) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = ERROR_TYPE_MISMATCH;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;

    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
             "expected type `%s`, found `%s`", expectedType, actualType);
    diagnostic.text.message = msgBuffer;

    char helpBuffer[512];
    char noteBuffer[256];
    char* note = NULL;
    
    // Provide specific help based on the mismatched types
    if (strstr(expectedType, "i32") && strstr(actualType, "f64")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "to convert float to integer, use explicit casting: `value as i32`. Example: `let x: i32 = 3.14 as i32` (result: 3)");
        snprintf(noteBuffer, sizeof(noteBuffer),
                "floating-point to integer conversions truncate the decimal part. For rounding, use math.round() first");
        note = noteBuffer;
    } 
    else if (strstr(expectedType, "f64") && (strstr(actualType, "i32") || strstr(actualType, "u32"))) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "to convert integer to float, use explicit casting: `value as f64`. Example: `let x: f64 = 42 as f64` (result: 42.0)");
        snprintf(noteBuffer, sizeof(noteBuffer),
                "integer to float conversions are always safe and preserve the exact value");
        note = noteBuffer;
    }
    else if (strstr(expectedType, "string") && (strstr(actualType, "i32") || strstr(actualType, "f64") || strstr(actualType, "bool"))) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "to convert %s to string, use string interpolation: `\"${value}\"` or explicit conversion. Example: `let s = \"${42}\"` or `let s = string(42)`",
                actualType);
        snprintf(noteBuffer, sizeof(noteBuffer),
                "string interpolation with ${} is the preferred way to convert values to strings in Orus");
        note = noteBuffer;
    }
    else if (strstr(actualType, "string") && (strstr(expectedType, "i32") || strstr(expectedType, "f64"))) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "to convert string to %s, use built-in functions: `int(\"123\")` for integers or `float(\"3.14\")` for floats",
                expectedType);
        snprintf(noteBuffer, sizeof(noteBuffer),
                "string conversion functions will throw runtime errors if the string is not a valid number");
        note = noteBuffer;
    }
    else if (strstr(expectedType, "bool")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "Orus requires explicit boolean conditions. Try comparisons like `value != 0`, `value == true`, or `value != nil`");
        snprintf(noteBuffer, sizeof(noteBuffer),
                "unlike some languages, Orus doesn't automatically convert values to booleans. This prevents common bugs");
        note = noteBuffer;
    }
    else if (strstr(actualType, "bool")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "booleans cannot be used as other types. Use conditional expressions: `if value { 1 } else { 0 }` or similar");
        snprintf(noteBuffer, sizeof(noteBuffer),
                "to convert boolean to string: `\"${value}\"`, to convert to number: use if/else expression");
        note = noteBuffer;
    }
    else if (strstr(expectedType, "array") || strstr(actualType, "array")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "array types must match exactly. Create a new array with correct type: `[element1, element2]` or convert elements individually");
        snprintf(noteBuffer, sizeof(noteBuffer),
                "arrays in Orus are statically typed. All elements must be the same type, and array types must match exactly");
        note = noteBuffer;
    }
    else if (strstr(expectedType, "u32") && strstr(actualType, "i32")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "to convert signed to unsigned integer, use explicit casting: `value as u32`. Example: `let x: u32 = 42 as u32`");
        snprintf(noteBuffer, sizeof(noteBuffer),
                "negative i32 values will wrap around when cast to u32. Check if value is non-negative first if needed");
        note = noteBuffer;
    }
    else if (strstr(expectedType, "i32") && strstr(actualType, "u32")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "to convert unsigned to signed integer, use explicit casting: `value as i32`. Example: `let x: i32 = 42u32 as i32`");
        snprintf(noteBuffer, sizeof(noteBuffer),
                "large u32 values (> 2147483647) will become negative when cast to i32. Check range if needed");
        note = noteBuffer;
    }
    else {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "types `%s` and `%s` are incompatible. Try explicit conversion with `as %s`, or check if you're using the correct variable/function",
                actualType, expectedType, expectedType);
        snprintf(noteBuffer, sizeof(noteBuffer),
                "Orus uses strict typing to prevent bugs. Most conversions require explicit casting or conversion functions");
        note = noteBuffer;
    }

    diagnostic.text.help = strdup(helpBuffer);
    
    if (note) {
        diagnostic.text.notes = &note;
        diagnostic.text.noteCount = 1;
    }

    emitDiagnostic(&diagnostic);
    
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

// Emit an error when a variable is redeclared in the same scope.
void emitRedeclarationError(Compiler* compiler, Token* token, const char* name) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = ERROR_SCOPE_ERROR;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;

    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
             "variable `%s` already declared in this scope", name);
    diagnostic.text.message = msgBuffer;

    // Generate a suggested alternative name
    char suggestedName[128];
    if (strlen(name) < 120) {
        // Create a suggestion by adding a number or 'new' prefix
        snprintf(suggestedName, sizeof(suggestedName), "%s2", name);
        
        char helpBuffer[256];
        snprintf(helpBuffer, sizeof(helpBuffer),
                "consider using a different name like `%s` or shadowing it in a new scope block",
                suggestedName);
        diagnostic.text.help = strdup(helpBuffer);
        
        char* note = "in Orus, each variable must have a unique name within its scope";
        diagnostic.text.notes = &note;
        diagnostic.text.noteCount = 1;
    } else {
        // Fall back to general advice for unusually long names
        diagnostic.text.help = strdup("rename the variable or remove the previous declaration");
    }

    emitDiagnostic(&diagnostic);
    
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

// Emit a generic type error with a custom message, help, and note.
void emitGenericTypeError(Compiler* compiler,
                         Token* token,
                         const char* message,
                         const char* help,
                         const char* note) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = ERROR_TYPE_MISMATCH; // Use type mismatch or define a new code if needed
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;

    diagnostic.text.message = message;
    diagnostic.text.help = help ? strdup(help) : NULL;
    if (note) {
        diagnostic.text.notes = (char**)&note;
        diagnostic.text.noteCount = 1;
    } else {
        diagnostic.text.notes = NULL;
        diagnostic.text.noteCount = 0;
    }

    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

// Emit an error when a function is not found.
void emitUndefinedFunctionError(Compiler* compiler, Token* token) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = ERROR_FUNCTION_CALL;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;

    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
        "cannot find function `%.*s` in this scope",
        token->length, token->start);
    diagnostic.text.message = msgBuffer;
    
    // Extract function name for more detailed analysis
    char nameBuf[64];
    int len = token->length < 63 ? token->length : 63;
    memcpy(nameBuf, token->start, len);
    nameBuf[len] = '\0';
    
    char helpBuffer[512];
    char baseNote[128];
    
    // Provide context-specific help based on function name patterns
    if (strstr(nameBuf, ".")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "function `%s` looks like a module function. Make sure to import the module with `use module.name` or check the module path",
                nameBuf);
        snprintf(baseNote, sizeof(baseNote),
                "module functions require importing the module first. Example: `use std.math` then `math.sqrt(value)`");
    } else if (strstr(nameBuf, "print") || strstr(nameBuf, "println")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "did you mean the built-in `print()` function? It's used like: `print(\"Hello\", variable)` for multiple values");
        snprintf(baseNote, sizeof(baseNote),
                "Orus has a built-in `print()` function that takes multiple arguments and automatically adds spaces between them");
    } else if (strstr(nameBuf, "len") || strstr(nameBuf, "length") || strstr(nameBuf, "size")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "for getting length of arrays or strings, use the built-in `len()` function: `len(array)` or `len(string)`");
        snprintf(baseNote, sizeof(baseNote),
                "the built-in `len()` function works with arrays and strings. For other containers, check if they have a `.length` field");
    } else if (strstr(nameBuf, "push") || strstr(nameBuf, "append") || strstr(nameBuf, "add")) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "for adding elements to arrays, use the built-in `push()` function: `push(array, element)` or array methods if available");
        snprintf(baseNote, sizeof(baseNote),
                "arrays have built-in functions like `push()` and `pop()`. For custom collections, define methods in impl blocks");
    } else if (strlen(nameBuf) <= 3 && islower(nameBuf[0])) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "short function name `%s` might be a typo. Check spelling, or if it's a custom function, make sure it's defined above this call",
                nameBuf);
        snprintf(baseNote, sizeof(baseNote),
                "functions must be defined before they're called. Move the function definition above this line, or check for typos");
    } else {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "function `%s` is not defined. Check for typos, ensure it's defined above this call, or import it if it's from another module",
                nameBuf);
        snprintf(baseNote, sizeof(baseNote),
                "functions must be defined before use. For external functions, use `use module_name` to import them");
    }
    
    diagnostic.text.help = strdup(helpBuffer);
    
    // Build notes array with typo suggestion if available
    char suggestNote[64];
    char* notes[2];
    notes[0] = baseNote;
    int noteCount = 1;
    
    const char* suggestion = suggestClosestSymbol(compiler, nameBuf);
    if (suggestion && strcmp(suggestion, nameBuf) != 0) {
        snprintf(suggestNote, sizeof(suggestNote), "did you mean `%s`?", suggestion);
        notes[noteCount++] = suggestNote;
    }
    diagnostic.text.notes = notes;
    diagnostic.text.noteCount = noteCount;

    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

void emitPrivateFunctionError(Compiler* compiler, Token* token) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = ERROR_PRIVATE_ACCESS;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;

    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
             "function `%.*s` is private", token->length, token->start);
    diagnostic.text.message = msgBuffer;
    diagnostic.text.help = strdup("mark the function with `pub` to allow access from other modules");
    const char* note = "only public items can be accessed from other modules";
    diagnostic.text.notes = (char**)&note;
    diagnostic.text.noteCount = 1;

    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

void emitPrivateVariableError(Compiler* compiler, Token* token) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = ERROR_PRIVATE_ACCESS;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;

    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
             "variable `%.*s` is private", token->length, token->start);
    diagnostic.text.message = msgBuffer;
    diagnostic.text.help = strdup("mark the variable with `pub` to allow access from other modules");
    const char* note = "only public items can be accessed from other modules";
    diagnostic.text.notes = (char**)&note;
    diagnostic.text.noteCount = 1;

    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

void emitImmutableAssignmentError(Compiler* compiler, Token* token, const char* name) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = ERROR_IMMUTABLE_ASSIGNMENT;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;

    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
             "cannot assign to immutable variable `%s`", name);
    diagnostic.text.message = msgBuffer;
    
    char helpBuffer[256];
    char noteBuffer[256];
    
    snprintf(helpBuffer, sizeof(helpBuffer),
            "to make `%s` mutable, declare it with `let mut %s = value` instead of `let %s = value`",
            name, name, name);
    
    snprintf(noteBuffer, sizeof(noteBuffer),
            "variables in Orus are immutable by default for safety. Use `mut` only when you need to modify the variable after declaration. This prevents many common bugs");
    
    diagnostic.text.help = strdup(helpBuffer);
    const char* note = noteBuffer;
    diagnostic.text.notes = (char**)&note;
    diagnostic.text.noteCount = 1;

    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

void emitStructFieldTypeMismatchError(Compiler* compiler, Token* token, const char* structName, const char* fieldName, const char* expectedType, const char* actualType) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;
    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));
    diagnostic.code = ERROR_TYPE_MISMATCH;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;
    char msgBuffer[256];
    snprintf(msgBuffer, sizeof(msgBuffer),
        "type mismatch for field `%s` in struct `%s`: expected `%s`, found `%s`",
        fieldName, structName, expectedType, actualType);
    diagnostic.text.message = msgBuffer;
    diagnostic.text.help = strdup("check the struct definition and the value assigned to this field");
    const char* note = "all struct fields must match their declared types";
    diagnostic.text.notes = (char**)&note;
    diagnostic.text.noteCount = 1;
    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

void emitFieldAccessNonStructError(Compiler* compiler, Token* token, const char* actualType) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;
    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));
    diagnostic.code = ERROR_TYPE_MISMATCH;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;
    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
        "can only access fields on structs, but found `%s`",
        actualType);
    diagnostic.text.message = msgBuffer;
    diagnostic.text.help = strdup("make sure you are accessing a struct instance");
    const char* note = "field access is only valid on struct types";
    diagnostic.text.notes = (char**)&note;
    diagnostic.text.noteCount = 1;
    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

void emitIsTypeSecondArgError(Compiler* compiler, Token* token, const char* actualType) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;
    
    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));
    diagnostic.code = ERROR_TYPE_MISMATCH;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;
    
    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
        "second argument to `is_type()` must be a string, found `%s`",
        actualType);
    diagnostic.text.message = msgBuffer;
    
    diagnostic.text.help = strdup("provide a string literal representing a type name, e.g., \"i32\", \"string\", etc.");
    
    const char* note = "is_type() checks if a value has the specified type, where the type name must be a string";
    diagnostic.text.notes = (char**)&note;
    diagnostic.text.noteCount = 1;
    
    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

void emitLenInvalidTypeError(Compiler* compiler, Token* token, const char* actualType) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;
    
    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));
    diagnostic.code = ERROR_TYPE_MISMATCH;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;
    
    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
        "`len()` expects an array or string, found `%s`",
        actualType);
    diagnostic.text.message = msgBuffer;
    
    const char* help = "provide an array or string as the argument to len()";
    diagnostic.text.help = strdup(help);
    
    const char* note = "the len() function can only be used with arrays or strings to determine their length";
    diagnostic.text.notes = (char**)&note;
    diagnostic.text.noteCount = 1;
    
    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

void emitBuiltinArgCountError(Compiler* compiler, Token* token,
                              const char* name, int expected, int actual) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));
    diagnostic.code = ERROR_FUNCTION_CALL;
    diagnostic.primarySpan.line = token->line;
    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length;
    diagnostic.primarySpan.filePath = compiler->filePath;

    char msgBuffer[128];
    snprintf(msgBuffer, sizeof(msgBuffer),
             "%s() expects %d argument%s but %d %s supplied",
             name, expected, expected == 1 ? "" : "s",
             actual, actual == 1 ? "was" : "were");
    diagnostic.text.message = msgBuffer;

    char helpBuffer[512];
    char noteBuffer[256];
    const char* note = NULL;

    // Special handling for built-in functions to provide better help messages with examples
    if (strcmp(name, "type_of") == 0) {
        if (expected == 1 && actual == 0) {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "provide a value to check its type: `type_of(variable)`. Example: `type_of(42)` returns \"i32\"");
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "type_of() returns a string representation of any value's type. Useful for debugging and type checking");
        } else {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "type_of() takes exactly one argument: `type_of(value)`. You provided %d arguments", actual);
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "type_of() only accepts a single value to examine");
        }
        note = noteBuffer;
    } 
    else if (strcmp(name, "is_type") == 0) {
        if (expected == 2 && actual < 2) {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "provide both a value and a type string: `is_type(value, \"type_name\")`. Example: `is_type(42, \"i32\")` returns true");
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "is_type() checks if a value matches the specified type string. Valid types: \"i32\", \"f64\", \"bool\", \"string\", \"array\"");
        } else {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "is_type() takes exactly 2 arguments: `is_type(value, \"type\")`. You provided %d arguments", actual);
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "the second argument must be a string literal with the type name");
        }
        note = noteBuffer;
    } 
    else if (strcmp(name, "substring") == 0) {
        if (expected == 3 && actual < 3) {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "provide a string, start index, and length: `substring(\"hello\", 1, 3)` returns \"ell\"");
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "substring() extracts characters from position 'start' for 'length' characters. Indices are 0-based");
        } else {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "substring() takes exactly 3 arguments: string, start, length. You provided %d arguments", actual);
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "all arguments are required: the string to extract from, starting position, and number of characters");
        }
        note = noteBuffer;
    }
    else if (strcmp(name, "len") == 0) {
        if (expected == 1 && actual == 0) {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "provide an array or string: `len([1, 2, 3])` returns 3, `len(\"hello\")` returns 5");
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "len() works with arrays and strings. For other types, check if they have a .length field or size method");
        } else {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "len() takes exactly one argument: `len(array_or_string)`. You provided %d arguments", actual);
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "len() only accepts arrays or strings");
        }
        note = noteBuffer;
    }
    else if (strcmp(name, "push") == 0) {
        if (expected == 2 && actual < 2) {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "provide an array and a value: `push([1, 2], 3)` adds 3 to the array. Example: `push(myArray, newElement)`");
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "push() modifies the array in-place and returns the new length. The element type must match the array type");
        } else {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "push() takes exactly 2 arguments: array and element. You provided %d arguments", actual);
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "the element being pushed must be compatible with the array's element type");
        }
        note = noteBuffer;
    }
    else if (strcmp(name, "pop") == 0) {
        if (expected == 1 && actual == 0) {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "provide an array: `pop([1, 2, 3])` removes and returns 3. Example: `let last = pop(myArray)`");
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "pop() removes the last element and returns it. Returns nil if the array is empty");
        } else {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "pop() takes exactly one argument: `pop(array)`. You provided %d arguments", actual);
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "pop() only works with arrays");
        }
        note = noteBuffer;
    }
    else if (strcmp(name, "print") == 0) {
        snprintf(helpBuffer, sizeof(helpBuffer),
                "print() takes any number of arguments: `print(\"Hello\")`, `print(\"Value:\", 42)`, `print(var1, var2, var3)`");
        snprintf(noteBuffer, sizeof(noteBuffer),
                "print() automatically adds spaces between arguments and a newline at the end. Use string interpolation for more control");
        note = noteBuffer;
    }
    else if (strcmp(name, "int") == 0 || strcmp(name, "float") == 0) {
        if (expected == 1 && actual == 0) {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "provide a string to convert: `%s(\"123\")`. Example: `%s(\"42\")` returns %s",
                    name, name, strcmp(name, "int") == 0 ? "42" : "42.0");
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "%s() converts string representations of numbers. Throws runtime error if string is not a valid number",
                    name);
        } else {
            snprintf(helpBuffer, sizeof(helpBuffer),
                    "%s() takes exactly one string argument. You provided %d arguments", name, actual);
            snprintf(noteBuffer, sizeof(noteBuffer),
                    "the argument must be a string containing a valid number");
        }
        note = noteBuffer;
    }
    else {
        // Default for other functions
        snprintf(helpBuffer, sizeof(helpBuffer),
                "function `%s()` expects %d argument%s but received %d. Check the function signature or documentation",
                name, expected, expected == 1 ? "" : "s", actual);
        snprintf(noteBuffer, sizeof(noteBuffer),
                "built-in functions have fixed signatures. Make sure you're providing the correct number and types of arguments");
        note = noteBuffer;
    }
    
    diagnostic.text.help = strdup(helpBuffer);

    if (note) {
        diagnostic.text.notes = (char**)&note;
        diagnostic.text.noteCount = 1;
    } else {
        diagnostic.text.notes = NULL;
        diagnostic.text.noteCount = 0;
    }

    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

// Emit a simple compiler error when no detailed context is available.
void emitSimpleError(Compiler* compiler, ErrorCode code, const char* message) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = code;
    diagnostic.text.message = message;
    diagnostic.primarySpan.filePath = compiler->filePath;
    diagnostic.primarySpan.line = compiler->currentLine > 0 ? compiler->currentLine : 1;
    diagnostic.primarySpan.column = 1;
    diagnostic.primarySpan.length = 1;

    diagnostic.text.help = strdup("refer to the Orus documentation for possible resolutions");
    const char* note = "a generic compiler error occurred";
    diagnostic.text.notes = (char**)&note;
    diagnostic.text.noteCount = 1;

    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

// Emit a compiler error at a specific token location so the diagnostic caret
// points to the offending part of the source code.
void emitTokenError(Compiler* compiler,
                    Token* token,
                    ErrorCode code,
                    const char* message) {
    if (compiler->panicMode) return;
    compiler->panicMode = true;

    Diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(Diagnostic));

    diagnostic.code = code;
    diagnostic.text.message = message;
    diagnostic.primarySpan.filePath = compiler->filePath;
    diagnostic.primarySpan.line = token->line;

    diagnostic.primarySpan.column = token->column;
    diagnostic.primarySpan.length = token->length > 0 ? token->length : 1;

    diagnostic.text.help = strdup("check the highlighted token for mistakes");
    const char* note = "the compiler encountered an unexpected token here";
    diagnostic.text.notes = (char**)&note;
    diagnostic.text.noteCount = 1;

    emitDiagnostic(&diagnostic);
    if (diagnostic.text.help) free(diagnostic.text.help);
    compiler->hadError = true;
}

