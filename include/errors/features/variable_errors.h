#ifndef VARIABLE_ERRORS_H
#define VARIABLE_ERRORS_H

#include "errors/error_interface.h"
#include "internal/error_reporting.h"

/**
 * Variable Error Reporting Module
 * 
 * This module provides comprehensive error reporting for all variable-related
 * errors in the Orus language, following the modular error architecture.
 * 
 * Error Categories Covered:
 * - Variable declaration errors
 * - Variable assignment errors
 * - Scope violations
 * - Mutability violations
 * - Variable naming issues
 * - Multiple declaration errors
 * - Loop variable errors
 */

// Initialize the variable error system
ErrorReportResult init_variable_errors(void);

// === Core Variable Errors ===

/**
 * Report undefined variable error
 * @param location Source location of the error
 * @param variable_name Name of the undefined variable
 */
ErrorReportResult report_undefined_variable(SrcLocation location, const char* variable_name);

/**
 * Report variable redefinition error
 * @param location Source location of the redefinition
 * @param variable_name Name of the variable being redefined
 * @param previous_line Line where variable was first defined
 */
ErrorReportResult report_variable_redefinition(SrcLocation location, const char* variable_name, int previous_line);

/**
 * Report scope violation error
 * @param location Source location of the violation
 * @param variable_name Name of the variable accessed outside scope
 * @param scope_context Description of where variable is accessible
 */
ErrorReportResult report_scope_violation(SrcLocation location, const char* variable_name, const char* scope_context);

// === Mutability Errors ===

/**
 * Report immutable assignment error
 * @param location Source location of the assignment
 * @param variable_name Name of the immutable variable
 */
ErrorReportResult report_immutable_variable_assignment(SrcLocation location, const char* variable_name);

/**
 * Report compound assignment on immutable variable
 * @param location Source location of the compound assignment
 * @param variable_name Name of the immutable variable
 * @param operator The compound operator (+=, -=, etc.)
 */
ErrorReportResult report_immutable_compound_assignment(SrcLocation location, const char* variable_name, const char* operator);

/**
 * Report mutable keyword required error
 * @param location Source location where mut is needed
 * @param variable_name Name of the variable
 * @param operation Description of the operation requiring mutability
 */
ErrorReportResult report_mutable_required(SrcLocation location, const char* variable_name, const char* operation);

// === Declaration Errors ===

/**
 * Report invalid variable name error
 * @param location Source location of the invalid name
 * @param variable_name The invalid variable name
 * @param reason Why the name is invalid
 */
ErrorReportResult report_invalid_variable_name(SrcLocation location, const char* variable_name, const char* reason);

/**
 * Report invalid multiple declaration error
 * @param location Source location of the error
 * @param variable_name Name of the variable with declaration issues
 * @param issue Description of the declaration issue
 */
ErrorReportResult report_invalid_multiple_declaration(SrcLocation location, const char* variable_name, const char* issue);

/**
 * Report variable not initialized error
 * @param location Source location where uninitialized variable is used
 * @param variable_name Name of the uninitialized variable
 */
ErrorReportResult report_variable_not_initialized(SrcLocation location, const char* variable_name);

// === Loop Variable Errors ===

/**
 * Report loop variable modification error
 * @param location Source location of the modification attempt
 * @param variable_name Name of the loop variable
 * @param loop_type Type of loop (for, while, etc.)
 */
ErrorReportResult report_loop_variable_modification(SrcLocation location, const char* variable_name, const char* loop_type);

// === Utility Functions ===

/**
 * Get variable-specific error suggestions
 * @param code Error code
 * @param context Additional context for suggestions
 * @return Suggestion string or NULL
 */
const char* get_variable_error_suggestion(ErrorCode code, const char* context);

/**
 * Check if error code is a variable error
 * @param code Error code to check
 * @return true if it's a variable error
 */
bool is_variable_error(ErrorCode code);

/**
 * Get variable scope information for error context
 * @param variable_name Name of the variable
 * @param current_scope Current scope depth
 * @return Scope description string
 */
const char* get_variable_scope_info(const char* variable_name, int current_scope);

/**
 * Suggest correct variable names for typos
 * @param wrong_name The incorrectly spelled name
 * @param available_vars Array of available variable names
 * @param count Number of available variables
 * @return Closest match or NULL
 */
const char* suggest_variable_name(const char* wrong_name, const char** available_vars, size_t count);

/**
 * Check if variable name follows Orus naming conventions
 * @param name Variable name to validate
 * @return true if valid, false otherwise
 */
bool is_valid_variable_name(const char* name);

/**
 * Get specific reason why a variable name is invalid
 * @param name Variable name to check
 * @return Reason string or NULL if valid
 */
const char* get_variable_name_violation_reason(const char* name);

#endif // VARIABLE_ERRORS_H