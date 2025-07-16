#ifndef ORUS_PARSER_H
#define ORUS_PARSER_H

#include "vm/vm.h"
#include "lexer.h"
#include "ast.h"

ASTNode* parseSource(const char* source);
void freeAST(ASTNode* node);

#endif // ORUS_PARSER_H
