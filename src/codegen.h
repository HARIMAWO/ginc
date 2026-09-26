#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

// ASTを受け取ってLLVM IRを生成・出力する関数
void generate_code(ASTNode* root);

#endif