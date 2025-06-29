#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @file ast.c
 * @brief Helper constructors for Abstract Syntax Tree nodes.
 *
 * Each function in this file allocates and initializes one of the ASTNode
 * variants defined in `ast.h`.  These small wrappers keep the compiler logic
 * tidy and ensure every node starts in a consistent state.
 */

#include "ast.h"
#include "memory.h"

/**
 * Create a literal expression node.
 *
 * @param value Constant value to embed in the AST.
 * @return Newly allocated AST node.
 */
    ASTNode* createLiteralNode(Value value) {
    ASTNode* node = allocateASTNode();
    node->type = AST_LITERAL;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.literal = value;
    node->valueType = NULL;  // Set by type checker

    // Comment out debug print
    // fprintf(stderr, "DEBUG: Initializing AST_LITERAL node with value: ");
    // printValue(node->data.literal);
    // fprintf(stderr, "\n");

    return node;
}

/**
 * Create a binary operation node.
 *
 * @param operator Operator token.
 * @param left     Left operand.
 * @param right    Right operand.
 * @return Newly allocated AST node.
 */
ASTNode* createBinaryNode(Token operator, ASTNode* left, ASTNode* right) {
    ASTNode* node = allocateASTNode();
    node->type = AST_BINARY;
    node->left = left;
    node->right = right;
    node->next = NULL;
    node->data.operation.operator = operator;
    node->data.operation.arity = 2;
    node->data.operation.convertLeft = false;
    node->data.operation.convertRight = false;
    node->valueType = NULL;
    return node;
}

/**
 * Create a unary operation node.
 *
 * @param operator Operator token.
 * @param operand  Operand expression.
 * @return Newly allocated AST node.
 */
ASTNode* createUnaryNode(Token operator, ASTNode* operand) {
    ASTNode* node = allocateASTNode();
    node->type = AST_UNARY;
    node->left = operand;
    node->right = NULL;
    node->next = NULL;
    node->data.operation.operator = operator;
    node->data.operation.arity = 1;
    node->data.operation.convertLeft = false;
    node->data.operation.convertRight = false;
    node->valueType = NULL;
    return node;
}

/**
 * Create a variable reference node.
 *
 * @param name  Token naming the variable.
 * @param index Slot index resolved during compilation.
 * @return Newly allocated AST node.
 */
ASTNode* createVariableNode(Token name, uint8_t index) {
    ASTNode* node = allocateASTNode();
    node->type = AST_VARIABLE;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.variable.name = name;
    node->data.variable.index = index;
    node->data.variable.genericArgs = NULL;
    node->data.variable.genericArgCount = 0;
    node->valueType = NULL;
    return node;
}

/**
 * Create a `let` declaration node.
 *
 * @param name        Variable name token.
 * @param type        Declared type or NULL.
 * @param initializer Optional initializer expression.
 * @param isMutable   Whether the binding is mutable.
 * @param isPublic    Whether the binding is exported.
 * @return Newly allocated AST node.
 */
ASTNode* createLetNode(Token name, Type* type, ASTNode* initializer,
                       bool isMutable, bool isPublic) {
    ASTNode* node = allocateASTNode();
    node->type = AST_LET;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.let.name = name;
    node->data.let.type = type;
    node->data.let.initializer = initializer;
    node->data.let.index = 0;
    node->data.let.isMutable = isMutable;
    node->data.let.isPublic = isPublic;
    node->valueType = NULL;
    return node;
}

/**
 * Create a static variable declaration node.
 *
 * @param name        Variable name token.
 * @param type        Declared type.
 * @param initializer Initial value expression.
 * @param isMutable   Whether the variable is mutable.
 * @return Newly allocated AST node.
 */
ASTNode* createStaticNode(Token name, Type* type, ASTNode* initializer,
                          bool isMutable) {
    ASTNode* node = allocateASTNode();
    node->type = AST_STATIC;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.staticVar.name = name;
    node->data.staticVar.type = type;
    node->data.staticVar.initializer = initializer;
    node->data.staticVar.index = 0;
    node->data.staticVar.isMutable = isMutable;
    node->valueType = NULL;
    return node;
}

/**
 * Create a constant declaration node.
 *
 * @param name        Constant name token.
 * @param type        Declared type.
 * @param initializer Initial value expression.
 * @param isPublic    Whether the constant is exported.
 * @return Newly allocated AST node.
 */
ASTNode* createConstNode(Token name, Type* type, ASTNode* initializer,
                         bool isPublic) {
    ASTNode* node = allocateASTNode();
    node->type = AST_CONST;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.constant.name = name;
    node->data.constant.type = type;
    node->data.constant.initializer = initializer;
    node->data.constant.index = 0;
    node->data.constant.isPublic = isPublic;
    node->valueType = NULL;
    return node;
}

/**
 * Create a print statement node.
 *
 * @param format    Format string expression.
 * @param arguments Linked list of argument expressions.
 * @param argCount  Number of arguments.
 * @param newline   Whether to print a trailing newline.
 * @param line      Source line for diagnostics.
 * @return Newly allocated AST node.
 */
ASTNode* createPrintNode(ASTNode* format, ASTNode* arguments, int argCount,
                         bool newline, int line) {
    ASTNode* node = allocateASTNode();
    node->type = AST_PRINT;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.print.format = format;
    node->data.print.arguments = arguments;
    node->data.print.argCount = argCount;
    node->data.print.newline = newline;
    node->valueType = NULL;
    node->line = line;
    return node;
}

/**
 * Create an if/elif/else statement node.
 *
 * @param condition      Condition expression.
 * @param thenBranch     Block executed when condition is true.
 * @param elifConditions Linked list of elif conditions.
 * @param elifBranches   Linked list of elif bodies.
 * @param elseBranch     Optional else branch.
 * @return Newly allocated AST node.
 */
ASTNode* createIfNode(ASTNode* condition, ASTNode* thenBranch,
                      ASTNode* elifConditions, ASTNode* elifBranches,
                      ASTNode* elseBranch) {
    ASTNode* node = allocateASTNode();
    node->type = AST_IF;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.ifStmt.condition = condition;
    node->data.ifStmt.thenBranch = thenBranch;
    node->data.ifStmt.elifConditions = elifConditions;
    node->data.ifStmt.elifBranches = elifBranches;
    node->data.ifStmt.elseBranch = elseBranch;
    node->valueType = NULL;
    return node;
}

/**
 * Create a ternary conditional expression node.
 *
 * @param condition Condition expression.
 * @param thenExpr  Expression when condition is true.
 * @param elseExpr  Expression when condition is false.
 * @return Newly allocated AST node.
 */
ASTNode* createTernaryNode(ASTNode* condition, ASTNode* thenExpr,
                           ASTNode* elseExpr) {
    ASTNode* node = allocateASTNode();
    node->type = AST_TERNARY;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.ternary.condition = condition;
    node->data.ternary.thenExpr = thenExpr;
    node->data.ternary.elseExpr = elseExpr;
    node->valueType = NULL;
    return node;
}

/**
 * Create a block statement node.
 *
 * @param statements Linked list of statements in the block.
 * @param scoped     Whether the block introduces a new scope.
 * @return Newly allocated AST node.
 */
ASTNode* createBlockNode(ASTNode* statements, bool scoped) {
    ASTNode* node = allocateASTNode();
    node->type = AST_BLOCK;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.block.statements = statements;
    node->data.block.scoped = scoped;
    node->valueType = NULL;
    return node;
}

/**
 * Create an assignment statement node.
 *
 * @param name  Target variable token.
 * @param value Expression yielding the assigned value.
 * @return Newly allocated AST node.
 */
ASTNode* createAssignmentNode(Token name, ASTNode* value) {
    ASTNode* node = allocateASTNode();
    node->type = AST_ASSIGNMENT;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.variable.name = name;
    node->data.variable.index = 0;  // Will be resolved during compilation
    node->valueType = NULL;
    // Store the value expression in the left child
    node->left = value;
    return node;
}

/**
 * Create a while loop node.
 *
 * @param condition Loop condition expression.
 * @param body      Loop body block.
 * @return Newly allocated AST node.
 */
ASTNode* createWhileNode(ASTNode* condition, ASTNode* body) {
    ASTNode* node = allocateASTNode();
    node->type = AST_WHILE;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.whileStmt.condition = condition;
    node->data.whileStmt.body = body;
    node->valueType = NULL;
    return node;
}

/**
 * Create a for loop node.
 *
 * @param iteratorName Name of the loop variable.
 * @param startExpr    Start expression.
 * @param endExpr      End expression.
 * @param stepExpr     Step expression or NULL.
 * @param body         Loop body.
 * @return Newly allocated AST node.
 */
ASTNode* createForNode(Token iteratorName, ASTNode* startExpr, ASTNode* endExpr,
                       ASTNode* stepExpr, ASTNode* body) {
    ASTNode* node = allocateASTNode();
    node->type = AST_FOR;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.forStmt.iteratorName = iteratorName;
    node->data.forStmt.iteratorIndex =
        0;  // Will be resolved during compilation
    node->data.forStmt.startExpr = startExpr;
    node->data.forStmt.endExpr = endExpr;
    node->data.forStmt.stepExpr = stepExpr;
    node->data.forStmt.body = body;
    node->valueType = NULL;
    return node;
}

/**
 * Create a function declaration node.
 *
 * @param name         Function name token.
 * @param parameters   Linked list of parameter nodes.
 * @param returnType   Declared return type.
 * @param body         Function body block.
 * @param generics     Generic parameter names.
 * @param constraints  Constraints for generic parameters.
 * @param genericCount Number of generic parameters.
 * @param isPublic     Whether the function is exported.
 * @return Newly allocated AST node.
 */
ASTNode* createFunctionNode(Token name, ASTNode* parameters, Type* returnType,
                            ASTNode* body, ObjString** generics,
                            GenericConstraint* constraints, int genericCount,
                            bool isPublic) {
    ASTNode* node = allocateASTNode();
    node->type = AST_FUNCTION;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.function.name = name;
    node->data.function.parameters = parameters;
    node->data.function.returnType = returnType;
    node->data.function.body = body;
    node->data.function.index =
        UINT8_MAX;  // Will be resolved during compilation
    node->data.function.isMethod = false;
    node->data.function.implType = NULL;
    node->data.function.mangledName = NULL;
    node->data.function.genericParams = generics;
    node->data.function.genericConstraints = constraints;
    node->data.function.genericCount = genericCount;
    node->data.function.isPublic = isPublic;
    node->valueType = NULL;
    return node;
}

/**
 * Create a function call node.
 *
 * @param name            Callee name token.
 * @param arguments       Linked list of argument expressions.
 * @param argCount        Number of arguments.
 * @param staticType      Struct type when calling as Struct.fn.
 * @param genericArgs     Generic argument types.
 * @param genericArgCount Number of generic arguments.
 * @return Newly allocated AST node.
 */
ASTNode* createCallNode(Token name, ASTNode* arguments, int argCount,
                        Type* staticType, Type** genericArgs,
                        int genericArgCount) {
    ASTNode* node = allocateASTNode();
    node->type = AST_CALL;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.call.name = name;
    node->data.call.arguments = arguments;
    node->data.call.index = 0;  // Will be resolved during compilation
    node->data.call.convertArgs =
        NULL;  // Will be allocated during type checking
    node->data.call.argCount = argCount;
    node->data.call.staticType = staticType;
    node->data.call.mangledName = NULL;
    node->data.call.nativeIndex = -1;
    node->data.call.builtinOp = -1;
    node->data.call.genericArgs = genericArgs;
    node->data.call.genericArgCount = genericArgCount;
    node->valueType = NULL;
    return node;
}

/**
 * Create a return statement node.
 *
 * @param value Expression yielding the return value or NULL.
 * @return Newly allocated AST node.
 */
ASTNode* createReturnNode(ASTNode* value) {
    ASTNode* node = allocateASTNode();
    node->type = AST_RETURN;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.returnStmt.value = value;
    node->valueType = NULL;
    return node;
}

/**
 * Create an array literal node.
 *
 * @param elements     Linked list of element expressions.
 * @param elementCount Number of elements in the array.
 * @return Newly allocated AST node.
 */
ASTNode* createArrayNode(ASTNode* elements, int elementCount) {
    ASTNode* node = allocateASTNode();
    node->type = AST_ARRAY;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.array.elements = elements;
    node->data.array.elementCount = elementCount;
    node->valueType = NULL;
    return node;
}

/**
 * Create an array fill expression node.
 *
 * @param value  Expression producing the element value.
 * @param length Expression for the number of elements.
 * @return Newly allocated AST node.
 */
ASTNode* createArrayFillNode(ASTNode* value, ASTNode* length) {
    ASTNode* node = allocateASTNode();
    node->type = AST_ARRAY_FILL;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.arrayFill.value = value;
    node->data.arrayFill.length = length;
    node->data.arrayFill.lengthValue = -1;
    node->valueType = NULL;
    return node;
}

/**
 * Create an array element assignment node.
 *
 * @param array Array expression being modified.
 * @param index Index expression.
 * @param value Value expression to assign.
 * @return Newly allocated AST node.
 */
ASTNode* createArraySetNode(ASTNode* array, ASTNode* index, ASTNode* value) {
    ASTNode* node = allocateASTNode();
    node->type = AST_ARRAY_SET;
    node->left = value;
    node->right = array;
    node->next = NULL;
    node->data.arraySet.index = index;
    node->valueType = NULL;
    return node;
}

/**
 * Create an array slice expression node.
 *
 * @param array Array expression.
 * @param start Start index or NULL.
 * @param end   End index or NULL.
 * @return Newly allocated AST node.
 */
ASTNode* createSliceNode(ASTNode* array, ASTNode* start, ASTNode* end) {
    ASTNode* node = allocateASTNode();
    node->type = AST_SLICE;
    node->left = array;
    node->right = NULL;
    node->next = NULL;
    node->data.slice.start = start;
    node->data.slice.end = end;
    node->valueType = NULL;
    return node;
}

/**
 * Create a struct literal initialization node.
 *
 * @param name            Struct type name token.
 * @param values          Linked list of field value expressions.
 * @param fieldCount      Number of fields provided.
 * @param genericArgs     Generic argument types.
 * @param genericArgCount Number of generic arguments.
 * @return Newly allocated AST node.
 */
ASTNode* createStructLiteralNode(Token name, ASTNode* values, int fieldCount,
                                 Type** genericArgs, int genericArgCount) {
    ASTNode* node = allocateASTNode();
    node->type = AST_STRUCT_LITERAL;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.structLiteral.name = name;
    node->data.structLiteral.values = values;
    node->data.structLiteral.fieldCount = fieldCount;
    node->data.structLiteral.genericArgs = genericArgs;
    node->data.structLiteral.genericArgCount = genericArgCount;
    node->valueType = NULL;
    return node;
}

/**
 * Create a struct field access expression node.
 *
 * @param object Expression yielding the struct value.
 * @param name   Field name token.
 * @return Newly allocated AST node.
 */
ASTNode* createFieldAccessNode(ASTNode* object, Token name) {
    ASTNode* node = allocateASTNode();
    node->type = AST_FIELD;
    node->left = object;
    node->right = NULL;
    node->next = NULL;
    node->data.field.fieldName = name;
    node->data.field.index = -1;
    node->valueType = NULL;
    return node;
}

/**
 * Create a struct field assignment node.
 *
 * @param object Expression yielding the struct value.
 * @param name   Field name token.
 * @param value  Value expression to assign.
 * @return Newly allocated AST node.
 */
ASTNode* createFieldSetNode(ASTNode* object, Token name, ASTNode* value) {
    ASTNode* node = allocateASTNode();
    node->type = AST_FIELD_SET;
    node->left = value;
    node->right = object;
    node->next = NULL;
    node->data.fieldSet.fieldName = name;
    node->data.fieldSet.index = -1;
    node->valueType = NULL;
    return node;
}

/**
 * Create a break statement node.
 *
 * @return Newly allocated AST node.
 */
ASTNode* createBreakNode() {
    ASTNode* node = allocateASTNode();
    node->type = AST_BREAK;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->valueType = NULL;
    return node;
}

/**
 * Create a continue statement node.
 *
 * @return Newly allocated AST node.
 */
ASTNode* createContinueNode() {
    ASTNode* node = allocateASTNode();
    node->type = AST_CONTINUE;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->valueType = NULL;
    return node;
}

/**
 * Create an import statement node.
 *
 * @param path String literal token containing the module path.
 * @return Newly allocated AST node.
 */
ASTNode* createImportNode(Token path) {
    ASTNode* node = allocateASTNode();
    node->type = AST_IMPORT;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    const char* start = path.start + 1;
    int length = path.length - 2;
    node->data.importStmt.path = allocateString(start, length);
    node->valueType = NULL;
    return node;
}

/**
 * Create a use declaration node.
 *
 * @param data Resolved module use information.
 * @return Newly allocated AST node.
 */
ASTNode* createUseNode(UseData data) {
    ASTNode* node = allocateASTNode();
    node->type = AST_USE;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->data.useStmt = data;
    node->valueType = NULL;
    return node;
}

/**
 * Create an explicit type cast expression node.
 *
 * @param expr Expression being cast.
 * @param type Target type for the cast.
 * @return Newly allocated AST node.
 */
ASTNode* createCastNode(ASTNode* expr, Type* type) {
    ASTNode* node = allocateASTNode();
    node->type = AST_CAST;
    node->left = expr;
    node->right = NULL;
    node->next = NULL;
    node->data.cast.type = type;
    node->valueType = NULL;
    return node;
}

/**
 * Create a try/catch statement node.
 *
 * @param tryBlock   Block executed in the protected region.
 * @param errorName  Name of the caught error variable.
 * @param catchBlock Catch handler block.
 * @return Newly allocated AST node.
 */
ASTNode* createTryNode(ASTNode* tryBlock, Token errorName,
                       ASTNode* catchBlock) {
    ASTNode* node = allocateASTNode();
    node->type = AST_TRY;
    node->left = tryBlock;
    node->right = catchBlock;
    node->next = NULL;
    node->data.tryStmt.tryBlock = tryBlock;
    node->data.tryStmt.errorName = errorName;
    node->data.tryStmt.catchBlock = catchBlock;
    node->data.tryStmt.errorIndex = 0;
    node->valueType = NULL;
    return node;
}

/**
 * Free an AST node. The GC currently owns all nodes so this is a no-op.
 */
void freeASTNode(ASTNode* node) {
    (void)node;  // GC-managed
}

// void freeASTNode(ASTNode* node) {
//     if (node == NULL) return;

//     if (node->left) {
//         freeASTNode(node->left);
//     }
//     if (node->right) {
//         freeASTNode(node->right);
//     }

//     if (node->type == AST_LET && node->data.let.initializer) {
//         freeASTNode(node->data.let.initializer);
//     }
//     if (node->type == AST_PRINT && node->data.print.expr) {
//         freeASTNode(node->data.print.expr);
//     }

//     if (node->next) {
//         freeASTNode(node->next);
//     }

//     // Do NOT free node->valueType; it's managed by type.c
//     free(node);
// }