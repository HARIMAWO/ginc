#include "codegen.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Support.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static LLVMContextRef context;
static LLVMModuleRef module;
static LLVMBuilderRef builder;

LLVMValueRef generate_node(ASTNode* node) {
    if (!node) return NULL;

    switch (node->type) {
        case AST_NUM:
            return LLVMConstInt(LLVMInt32TypeInContext(context), node->val, 0);

        case AST_STR:
            return LLVMBuildGlobalStringPtr(builder, node->var_name, "str_literal");

        case AST_STRING:
            return LLVMBuildGlobalStringPtr(builder, node->str_val, "strtmp");

        case AST_REGEX:
            return LLVMBuildGlobalStringPtr(builder, node->str_val, "regextmp");

        case AST_VAR: {
            LLVMValueRef var = LLVMGetNamedGlobal(module, node->var_name);
            if (!var) {
                fprintf(stderr, "Ga! (Codegen Error): Unknown variable '%s'\n", node->var_name);
                return NULL;
            }
            return LLVMBuildLoad2(builder, LLVMGlobalGetValueType(var), var, node->var_name);
        }

        case AST_VARDECL: {
            LLVMValueRef init_val = generate_node(node->right);
            if (!init_val) return NULL;

            char const* const var_name = node->var_name;
            if (LLVMGetNamedGlobal(module, var_name)) {
                fprintf(stderr, "Ga! (Codegen Error): Variable '%s' is already declared\n", var_name);
                return NULL;
            }

            LLVMTypeRef const val_type = LLVMTypeOf(init_val);
            LLVMValueRef const var = LLVMAddGlobal(module, val_type, var_name);
            LLVMSetInitializer(var, LLVMConstNull(val_type));

            LLVMBuildStore(builder, init_val, var);
            return init_val;
        }

        case AST_ARRAY_DECL: {
            char const* const array_name = node->var_name;
            if (LLVMGetNamedGlobal(module, array_name)) {
                fprintf(stderr, "Ga! (Codegen Error): Array '%s' is already declared\n", array_name);
                return NULL;
            }

            LLVMTypeRef elem_type = LLVMInt32TypeInContext(context);
            LLVMTypeRef array_type = LLVMArrayType(elem_type, node->val);
            LLVMValueRef array_var = LLVMAddGlobal(module, array_type, array_name);
            LLVMSetInitializer(array_var, LLVMConstNull(array_type));

            ASTNode* curr = node->right;
            int idx = 0;
            while (curr && idx < node->val) {
                LLVMValueRef elem_val = generate_node(curr->left);
                if (elem_val) {
                    LLVMValueRef indices[] = {
                        LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0),
                        LLVMConstInt(LLVMInt32TypeInContext(context), idx, 0)
                    };
                    LLVMValueRef gep = LLVMBuildGEP2(builder, array_type, array_var, indices, 2, "arrayidx");
                    LLVMBuildStore(builder, elem_val, gep);
                }
                curr = curr->right;
                idx++;
            }
            return array_var;
        }

        case AST_ARRAY_ACCESS: {
            char const* const array_name = node->var_name;
            LLVMValueRef array_var = LLVMGetNamedGlobal(module, array_name);
            if (!array_var) {
                fprintf(stderr, "Ga! (Codegen Error): Undeclared array '%s'\n", array_name);
                return NULL;
            }

            LLVMTypeRef array_type = LLVMGlobalGetValueType(array_var);
            LLVMValueRef idx_val = generate_node(node->left);
            if (!idx_val) return NULL;

            LLVMValueRef indices[] = {
                LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0),
                idx_val
            };
            LLVMValueRef gep = LLVMBuildGEP2(builder, array_type, array_var, indices, 2, "arrayidx");
            LLVMTypeRef elem_type = LLVMGetElementType(array_type);
            return LLVMBuildLoad2(builder, elem_type, gep, array_name);
        }

        case AST_ASSIGN: {
            LLVMValueRef rhs = generate_node(node->right);
            if (!rhs) return NULL;

            if (node->left->type != AST_VAR) {
                fprintf(stderr, "Ga! (Codegen Error): LHS of assignment must be a variable\n");
                return NULL;
            }

            char const* const var_name = node->left->var_name;
            LLVMValueRef const var = LLVMGetNamedGlobal(module, var_name);
            
            if (!var) {
                fprintf(stderr, "Ga! (Codegen Error): Undeclared variable '%s'\n", var_name);
                return NULL;
            }

            LLVMBuildStore(builder, rhs, var);
            return rhs;
        }

        case AST_ADD: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);

            if (LLVMGetTypeKind(LLVMTypeOf(lhs)) == LLVMPointerTypeKind) {
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                LLVMTypeRef param_types[] = { ptr_type, ptr_type };
                LLVMTypeRef concat_type = LLVMFunctionType(ptr_type, param_types, 2, 0);

                LLVMValueRef concat_func = LLVMGetNamedFunction(module, "ginc_str_concat");
                if (!concat_func) {
                    concat_func = LLVMAddFunction(module, "ginc_str_concat", concat_type);
                }

                LLVMValueRef args[] = { lhs, rhs };
                return LLVMBuildCall2(builder, concat_type, concat_func, args, 2, "concattmp");
            }

            return LLVMBuildAdd(builder, lhs, rhs, "addtmp");
        }

        case AST_SUB: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            return LLVMBuildSub(builder, lhs, rhs, "subtmp");
        }

        case AST_SEQ: {
            generate_node(node->left);
            generate_node(node->right);
            return NULL;
        }

        case AST_PRINT: {
            LLVMValueRef arg_val = generate_node(node->left);
            if (!arg_val) return NULL;

            LLVMTypeRef param_types[] = { LLVMPointerType(LLVMInt8TypeInContext(context), 0) };
            LLVMTypeRef printf_type = LLVMFunctionType(LLVMInt32TypeInContext(context), param_types, 1, 1);
            
            LLVMValueRef printf_func = LLVMGetNamedFunction(module, "printf");
            if (!printf_func) {
                printf_func = LLVMAddFunction(module, "printf", printf_type);
            }

            char const* fmt = "%d\n";
            if (LLVMGetTypeKind(LLVMTypeOf(arg_val)) == LLVMPointerTypeKind) {
                fmt = "%s\n";
            }

            LLVMValueRef format_str = LLVMBuildGlobalStringPtr(builder, fmt, "fmt");
            LLVMValueRef args[] = { format_str, arg_val };
            return LLVMBuildCall2(builder, printf_type, printf_func, args, 2, "printf_call");
        }

        case AST_BLOCK: {
            // Aiderがブロックの中身を left に入れたか then_body に入れたか不明なため、両対応
            if (node->left) generate_node(node->left);
            else if (node->then_body) generate_node(node->then_body);
            return NULL;
        }

        case AST_MATCH: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);

            LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMTypeRef param_types[] = { ptr_type, ptr_type };
            LLVMTypeRef match_func_type = LLVMFunctionType(LLVMInt32TypeInContext(context), param_types, 2, 0);

            LLVMValueRef match_func = LLVMGetNamedFunction(module, "ginc_regex_match");
            if (!match_func) {
                match_func = LLVMAddFunction(module, "ginc_regex_match", match_func_type);
            }

            LLVMValueRef args[] = { lhs, rhs };
            return LLVMBuildCall2(builder, match_func_type, match_func, args, 2, "matchtmp");
        }

        case AST_EQ:
        case AST_LT: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            LLVMIntPredicate pred = (node->type == AST_EQ) ? LLVMIntEQ : LLVMIntSLT;
            LLVMValueRef cmp = LLVMBuildICmp(builder, pred, lhs, rhs, "cmptmp");
            // ★超重要: 比較結果(i1)を必ず変数と同じ i32 にキャストして返す
            return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(context), "zexttmp");
        }

        case AST_IF: {
            LLVMValueRef cond_val = generate_node(node->cond);
            LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0);
            // ★超重要: i32同士を比較して、LLVMBuildCondBr用の純粋な i1 を生成する
            LLVMValueRef cond_i1 = LLVMBuildICmp(builder, LLVMIntNE, cond_val, zero, "ifcond");

            LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(context, func, "then");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(context, func, "ifcont");
            LLVMBasicBlockRef else_bb = NULL;

            if (node->else_body) {
                else_bb = LLVMAppendBasicBlockInContext(context, func, "else");
                LLVMBuildCondBr(builder, cond_i1, then_bb, else_bb);
            } else {
                LLVMBuildCondBr(builder, cond_i1, then_bb, merge_bb);
            }

            // then ブロックの生成
            LLVMPositionBuilderAtEnd(builder, then_bb);
            generate_node(node->then_body);
            LLVMBuildBr(builder, merge_bb);

            // else ブロックの生成
            if (node->else_body) {
                LLVMPositionBuilderAtEnd(builder, else_bb);
                generate_node(node->else_body);
                LLVMBuildBr(builder, merge_bb);
            }

            // 合流ブロックへ移動
            LLVMPositionBuilderAtEnd(builder, merge_bb);
            return NULL;
        }

        case AST_WHILE: {
            LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(context, func, "whilecond");
            LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(context, func, "whileloop");
            LLVMBasicBlockRef after_bb = LLVMAppendBasicBlockInContext(context, func, "whileafter");

            LLVMBuildBr(builder, cond_bb);

            // 条件判定ブロック
            LLVMPositionBuilderAtEnd(builder, cond_bb);
            LLVMValueRef cond_val = generate_node(node->cond);
            LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0);
            LLVMValueRef cond_i1 = LLVMBuildICmp(builder, LLVMIntNE, cond_val, zero, "whilecond_i1");
            LLVMBuildCondBr(builder, cond_i1, loop_bb, after_bb);

            // ループ本体ブロック
            LLVMPositionBuilderAtEnd(builder, loop_bb);
            generate_node(node->then_body);
            LLVMBuildBr(builder, cond_bb);

            // ループ脱出後
            LLVMPositionBuilderAtEnd(builder, after_bb);
            return NULL;
        }

        case AST_EXPR_LIST:
            return NULL;
    }
    return NULL;
}

void generate_code(ASTNode* root) {
    context = LLVMContextCreate();
    module = LLVMModuleCreateWithNameInContext("ginc_module", context);
    builder = LLVMCreateBuilderInContext(context);

    LLVMTypeRef main_func_type = LLVMFunctionType(LLVMInt32TypeInContext(context), NULL, 0, 0);
    LLVMValueRef main_func = LLVMAddFunction(module, "main", main_func_type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, main_func, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    generate_node(root);

    LLVMBuildRet(builder, LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0));

    char *error = NULL;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "Ga! (LLVM Verify Error): %s\n", error);
        LLVMDisposeMessage(error);
        return;
    }

    printf("\n--- LLVM IR Dump ---\n");
    char *ir_str = LLVMPrintModuleToString(module);
    if (ir_str) {
        printf("%s", ir_str);
        LLVMDisposeMessage(ir_str);
    }
    fflush(stdout);

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    char* ginc_str_concat(char const* s1, char const* s2);
    LLVMAddSymbol("ginc_str_concat", (void*)ginc_str_concat);

    int ginc_regex_match(char const* str, char const* pattern);
    LLVMAddSymbol("ginc_regex_match", (void*)ginc_regex_match);

    LLVMExecutionEngineRef engine;
    LLVMLinkInMCJIT();
    
    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        fprintf(stderr, "Ga! (JIT Error): %s\n", error);
        LLVMDisposeMessage(error);
        return;
    }

    uint64_t func_addr = LLVMGetFunctionAddress(engine, "main");
    if (func_addr == 0) {
        fprintf(stderr, "Ga! (JIT Error): 'main' function address not found.\n");
        return;
    }

    int (*main_fp)(void) = (int (*)(void))(intptr_t)func_addr;
    int res_val = main_fp();

    printf("\n=> JIT Execution Result: %d\n", res_val);

    LLVMDisposeExecutionEngine(engine);
    LLVMContextDispose(context);
}
