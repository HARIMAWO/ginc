#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

extern void generate_code(ASTNode* root);
extern int generate_code_ex(ASTNode* root, int verbose, int* out_exit_code);

#endif
