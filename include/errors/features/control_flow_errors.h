#ifndef CONTROL_FLOW_ERRORS_H
#define CONTROL_FLOW_ERRORS_H

#include "errors/error_interface.h"

// Control Flow Error Codes (E1401-E1409)
// These are defined in include/internal/error_reporting.h

// Break/Continue Error Reporting
ErrorReportResult report_break_outside_loop(SrcLocation location);
ErrorReportResult report_continue_outside_loop(SrcLocation location);
ErrorReportResult report_labeled_break_not_found(SrcLocation location, const char* label);
ErrorReportResult report_labeled_continue_not_found(SrcLocation location, const char* label);

// Condition Error Reporting
ErrorReportResult report_non_boolean_condition(SrcLocation location, const char* condition_type, const char* statement_type);
ErrorReportResult report_empty_condition(SrcLocation location, const char* statement_type);
ErrorReportResult report_unreachable_condition(SrcLocation location, const char* reason);

// Loop Range Error Reporting
ErrorReportResult report_invalid_range_syntax(SrcLocation location, const char* range_text, const char* issue);
ErrorReportResult report_descending_range_without_step(SrcLocation location, int start, int end);
ErrorReportResult report_zero_step_range(SrcLocation location);
ErrorReportResult report_infinite_range(SrcLocation location, const char* range_text);
ErrorReportResult report_range_overflow(SrcLocation location, const char* range_text);

// Syntax Error Reporting
ErrorReportResult report_missing_colon(SrcLocation location, const char* statement_type);
ErrorReportResult report_missing_condition(SrcLocation location, const char* statement_type);
ErrorReportResult report_invalid_indentation(SrcLocation location, const char* statement_type, int expected, int found);
ErrorReportResult report_empty_block(SrcLocation location, const char* statement_type);

// Loop Variable Error Reporting
ErrorReportResult report_invalid_loop_variable(SrcLocation location, const char* variable_name, const char* issue);
ErrorReportResult report_loop_variable_redeclaration(SrcLocation location, const char* variable_name);
ErrorReportResult report_loop_variable_type_mismatch(SrcLocation location, const char* variable_name, const char* expected_type, const char* found_type);

// Flow Control Analysis
ErrorReportResult report_unreachable_code(SrcLocation location, const char* reason);
ErrorReportResult report_dead_code_after_break(SrcLocation location);
ErrorReportResult report_dead_code_after_continue(SrcLocation location);
ErrorReportResult report_infinite_loop_warning(SrcLocation location, const char* loop_type);

// Control Flow Validation
ErrorReportResult report_missing_else_branch(SrcLocation location, const char* suggestion);
ErrorReportResult report_duplicate_else_clause(SrcLocation location);
ErrorReportResult report_elif_after_else(SrcLocation location);
ErrorReportResult report_standalone_else(SrcLocation location);

// Advanced Control Flow Errors
ErrorReportResult report_nested_loop_depth_exceeded(SrcLocation location, int max_depth);
ErrorReportResult report_complex_condition_warning(SrcLocation location, const char* suggestion);
ErrorReportResult report_loop_performance_warning(SrcLocation location, const char* issue, const char* suggestion);

// Validation Helpers
bool is_valid_loop_context(void);
bool is_valid_break_continue_context(void);
bool is_boolean_expression_type(const char* type_name);
bool is_valid_range_bounds(int start, int end, int step);
const char* get_control_flow_suggestion(const char* error_type);

#endif // CONTROL_FLOW_ERRORS_H