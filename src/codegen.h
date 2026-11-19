/*
 * Copyright 2026 HARIMAWO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

extern void generate_code(ASTNode* root);
extern int generate_code_ex(ASTNode* root, int verbose, int* out_exit_code);

#endif
