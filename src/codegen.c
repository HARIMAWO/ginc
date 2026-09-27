#include "codegen.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Support.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static LLVMContextRef context;
static LLVMModuleRef module;
static LLVMBuilderRef builder;

#define MAX_LOCALS 100
static LLVMValueRef local_vars[MAX_LOCALS];
static char* local_names[MAX_LOCALS];
static int local_count = 0;

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
            // 1. ローカル変数を後ろから探す
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], node->var_name) == 0) {
                    return LLVMBuildLoad2(builder, LLVMInt32TypeInContext(context), local_vars[i], node->var_name);
                }
            }
            // 2. なければグローバル変数
            LLVMValueRef var = LLVMGetNamedGlobal(module, node->var_name);
            if (!var) {
                fprintf(stderr, "Ga! (Codegen Error): Unknown variable '%s'\n", node->var_name);
                return NULL;
            }
            return LLVMBuildLoad2(builder, LLVMInt32TypeInContext(context), var, node->var_name);
        }

        case AST_VARDECL: {
            LLVMValueRef init_val = generate_node(node->right);
            if (!init_val) return NULL;
            if (local_count > 0) { // 関数内のローカル変数
                LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, LLVMInt32TypeInContext(context), node->var_name);
                local_names[local_count] = strdup(node->var_name);
                local_vars[local_count] = alloca_inst;
                local_count++;
                LLVMBuildStore(builder, init_val, alloca_inst);
                return init_val;
            } else { // グローバル変数
                LLVMValueRef var = LLVMAddGlobal(module, LLVMTypeOf(init_val), node->var_name);
                LLVMSetInitializer(var, LLVMConstNull(LLVMTypeOf(init_val)));
                LLVMBuildStore(builder, init_val, var);
                return init_val;
            }
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

            // 境界チェック (Bounds Checking)
            unsigned int const array_len = LLVMGetArrayLength(array_type);
            LLVMValueRef len_val = LLVMConstInt(LLVMInt32TypeInContext(context), array_len, 0);

            // 符号なし比較 UGE (unsigned greater than or equal) により、負数(2の補数)と上限オーバーの両方を判定
            LLVMValueRef is_oob = LLVMBuildICmp(builder, LLVMIntUGE, idx_val, len_val, "is_oob");

            LLVMValueRef parent_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(context, parent_func, "array_oob");
            LLVMBasicBlockRef inbounds_bb = LLVMAppendBasicBlockInContext(context, parent_func, "array_inbounds");

            LLVMBuildCondBr(builder, is_oob, oob_bb, inbounds_bb);

            // 範囲外 (Out of Bounds) ブロック
            LLVMPositionBuilderAtEnd(builder, oob_bb);
            LLVMTypeRef param_types[] = { LLVMInt32TypeInContext(context) };
            LLVMTypeRef throw_func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), param_types, 1, 0);

            LLVMValueRef throw_func = LLVMGetNamedFunction(module, "ginc_throw");
            if (!throw_func) {
                throw_func = LLVMAddFunction(module, "ginc_throw", throw_func_type);
            }

            LLVMValueRef err_code = LLVMConstInt(LLVMInt32TypeInContext(context), 101, 0);
            LLVMValueRef args[] = { err_code };
            LLVMBuildCall2(builder, throw_func_type, throw_func, args, 1, "");
            LLVMBuildUnreachable(builder);

            // 正常範囲内ブロック
            LLVMPositionBuilderAtEnd(builder, inbounds_bb);
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
            char const* const var_name = node->left->var_name;
            LLVMValueRef var = NULL;
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], var_name) == 0) {
                    var = local_vars[i]; break;
                }
            }
            if (!var) var = LLVMGetNamedGlobal(module, var_name);
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

        case AST_UNARY_MINUS: {
            LLVMValueRef val = generate_node(node->left);
            return LLVMBuildNeg(builder, val, "negtmp");
        }

        case AST_MUL: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            return LLVMBuildMul(builder, lhs, rhs, "multmp");
        }

        case AST_DIV: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            return LLVMBuildSDiv(builder, lhs, rhs, "divtmp");
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

            LLVMPositionBuilderAtEnd(builder, then_bb);
            generate_node(node->then_body);
            // ★重要: ブロックが既にreturn等で終わっている場合はBranchを追加しない
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildBr(builder, merge_bb);
            }

            if (node->else_body) {
                LLVMPositionBuilderAtEnd(builder, else_bb);
                generate_node(node->else_body);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                    LLVMBuildBr(builder, merge_bb);
                }
            }

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

        case AST_PARAM_DECL:
            return NULL;

        case AST_THROW: {
            LLVMValueRef val = generate_node(node->left);
            LLVMTypeRef param_types[] = { LLVMInt32TypeInContext(context) };
            LLVMTypeRef throw_func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), param_types, 1, 0);

            LLVMValueRef throw_func = LLVMGetNamedFunction(module, "ginc_throw");
            if (!throw_func) {
                throw_func = LLVMAddFunction(module, "ginc_throw", throw_func_type);
            }

            LLVMValueRef args[] = { val };
            LLVMBuildCall2(builder, throw_func_type, throw_func, args, 1, "");
            LLVMBuildUnreachable(builder);
            return NULL;
        }

        case AST_TRY: {
            LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMTypeRef i32_type = LLVMInt32TypeInContext(context);

            // jmp_buf (200バイト分のスタック領域) を確保
            LLVMTypeRef jmpbuf_type = LLVMArrayType(LLVMInt8TypeInContext(context), 200);
            LLVMValueRef jmpbuf_alloca = LLVMBuildAlloca(builder, jmpbuf_type, "jmpbuf");

            // _setjmp 宣言
            LLVMTypeRef setjmp_param_types[] = { ptr_type };
            LLVMTypeRef setjmp_func_type = LLVMFunctionType(i32_type, setjmp_param_types, 1, 0);
            LLVMValueRef setjmp_func = LLVMGetNamedFunction(module, "_setjmp");
            if (!setjmp_func) {
                setjmp_func = LLVMAddFunction(module, "_setjmp", setjmp_func_type);
            }

            // ginc_push_try 宣言
            LLVMTypeRef push_param_types[] = { ptr_type };
            LLVMTypeRef push_func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), push_param_types, 1, 0);
            LLVMValueRef push_func = LLVMGetNamedFunction(module, "ginc_push_try");
            if (!push_func) {
                push_func = LLVMAddFunction(module, "ginc_push_try", push_func_type);
            }

            // ginc_pop_try 宣言
            LLVMTypeRef pop_func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), NULL, 0, 0);
            LLVMValueRef pop_func = LLVMGetNamedFunction(module, "ginc_pop_try");
            if (!pop_func) {
                pop_func = LLVMAddFunction(module, "ginc_pop_try", pop_func_type);
            }

            // ginc_push_try(jmpbuf) 呼び出し
            LLVMValueRef push_args[] = { jmpbuf_alloca };
            LLVMBuildCall2(builder, push_func_type, push_func, push_args, 1, "");

            // _setjmp(jmpbuf) 呼び出し
            LLVMValueRef setjmp_args[] = { jmpbuf_alloca };
            LLVMValueRef setjmp_res = LLVMBuildCall2(builder, setjmp_func_type, setjmp_func, setjmp_args, 1, "setjmp_res");

            LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
            LLVMValueRef is_zero = LLVMBuildICmp(builder, LLVMIntEQ, setjmp_res, zero, "is_zero");

            LLVMValueRef parent_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef try_bb = LLVMAppendBasicBlockInContext(context, parent_func, "try_body");
            LLVMBasicBlockRef catch_bb = LLVMAppendBasicBlockInContext(context, parent_func, "catch_body");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(context, parent_func, "try_cont");

            LLVMBuildCondBr(builder, is_zero, try_bb, catch_bb);

            // try ブロック生成
            LLVMPositionBuilderAtEnd(builder, try_bb);
            generate_node(node->then_body);
            LLVMBuildCall2(builder, pop_func_type, pop_func, NULL, 0, "");
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildBr(builder, merge_bb);
            }

            // catch ブロック生成
            LLVMPositionBuilderAtEnd(builder, catch_bb);
            if (node->var_name) {
                LLVMValueRef catch_var_alloca = LLVMBuildAlloca(builder, i32_type, node->var_name);
                LLVMBuildStore(builder, setjmp_res, catch_var_alloca);

                if (local_count > 0 && local_count < MAX_LOCALS) {
                    local_names[local_count] = strdup(node->var_name);
                    local_vars[local_count] = catch_var_alloca;
                    local_count++;
                }
            }
            generate_node(node->else_body);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildBr(builder, merge_bb);
            }

            LLVMPositionBuilderAtEnd(builder, merge_bb);
            return NULL;
        }

        case AST_TRY_CATCH: {
            LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMTypeRef i32_type = LLVMInt32TypeInContext(context);

            // 1024バイトのjmpbuf領域を確保
            LLVMTypeRef jmpbuf_type = LLVMArrayType(LLVMInt8TypeInContext(context), 1024);
            LLVMValueRef jmpbuf_alloca = LLVMBuildAlloca(builder, jmpbuf_type, "jmpbuf");

            // 関数宣言 (_setjmp, ginc_push_try, ginc_pop_try)
            LLVMTypeRef setjmp_param_types[] = { ptr_type };
            LLVMTypeRef setjmp_func_type = LLVMFunctionType(i32_type, setjmp_param_types, 1, 0);
            LLVMValueRef setjmp_func = LLVMGetNamedFunction(module, "_setjmp");
            if (!setjmp_func) {
                setjmp_func = LLVMAddFunction(module, "_setjmp", setjmp_func_type);
            }

            LLVMTypeRef push_param_types[] = { ptr_type };
            LLVMTypeRef push_func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), push_param_types, 1, 0);
            LLVMValueRef push_func = LLVMGetNamedFunction(module, "ginc_push_try");
            if (!push_func) {
                push_func = LLVMAddFunction(module, "ginc_push_try", push_func_type);
            }

            LLVMTypeRef pop_func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), NULL, 0, 0);
            LLVMValueRef pop_func = LLVMGetNamedFunction(module, "ginc_pop_try");
            if (!pop_func) {
                pop_func = LLVMAddFunction(module, "ginc_pop_try", pop_func_type);
            }

            // ginc_push_try(jmpbuf) & _setjmp(jmpbuf)
            LLVMValueRef push_args[] = { jmpbuf_alloca };
            LLVMBuildCall2(builder, push_func_type, push_func, push_args, 1, "");

            LLVMValueRef setjmp_args[] = { jmpbuf_alloca };
            LLVMValueRef setjmp_res = LLVMBuildCall2(builder, setjmp_func_type, setjmp_func, setjmp_args, 1, "setjmp_res");

            LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
            LLVMValueRef is_zero = LLVMBuildICmp(builder, LLVMIntEQ, setjmp_res, zero, "is_zero");

            LLVMValueRef parent_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef try_bb = LLVMAppendBasicBlockInContext(context, parent_func, "try_body");
            LLVMBasicBlockRef catch_bb = LLVMAppendBasicBlockInContext(context, parent_func, "catch_body");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(context, parent_func, "try_cont");

            LLVMBuildCondBr(builder, is_zero, try_bb, catch_bb);

            // [try_bb]
            LLVMPositionBuilderAtEnd(builder, try_bb);
            generate_node(node->left);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildCall2(builder, pop_func_type, pop_func, NULL, 0, "");
                LLVMBuildBr(builder, merge_bb);
            }

            // [catch_bb]
            LLVMPositionBuilderAtEnd(builder, catch_bb);
            if (node->var_name) {
                LLVMValueRef catch_var_alloca = NULL;
                if (local_count > 0) { // 関数内
                    catch_var_alloca = LLVMBuildAlloca(builder, i32_type, node->var_name);
                    if (local_count < MAX_LOCALS) {
                        local_names[local_count] = strdup(node->var_name);
                        local_vars[local_count] = catch_var_alloca;
                        local_count++;
                    }
                } else { // トップレベル
                    catch_var_alloca = LLVMAddGlobal(module, i32_type, node->var_name);
                    LLVMSetInitializer(catch_var_alloca, LLVMConstNull(i32_type));
                }
                LLVMBuildStore(builder, setjmp_res, catch_var_alloca);
            }
            generate_node(node->right);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildBr(builder, merge_bb);
            }

            LLVMPositionBuilderAtEnd(builder, merge_bb);
            return NULL;
        }

        case AST_RETURN: {
            LLVMValueRef ret_val = generate_node(node->left);
            return LLVMBuildRet(builder, ret_val);
        }

        case AST_FUNC_CALL: {
            LLVMValueRef func = LLVMGetNamedFunction(module, node->var_name);
            if (!func) {
                fprintf(stderr, "Ga! (Codegen Error): Unknown function '%s'\n", node->var_name);
                return NULL;
            }
            // ★超重要: Opaque Pointer環境ではLLVMGlobalGetValueTypeで関数の実際の型を取得する
            LLVMTypeRef func_type = LLVMGlobalGetValueType(func);
            
            int arg_count = 0;
            ASTNode* curr_arg = node->left;
            while (curr_arg) { arg_count++; curr_arg = curr_arg->right; }
            
            LLVMValueRef* args = NULL;
            if (arg_count > 0) {
                args = malloc(sizeof(LLVMValueRef) * arg_count);
                curr_arg = node->left;
                int i = 0;
                while (curr_arg) {
                    args[i] = generate_node(curr_arg->left);
                    curr_arg = curr_arg->right; i++;
                }
            }
            LLVMValueRef call_res = LLVMBuildCall2(builder, func_type, func, args, arg_count, "calltmp");
            if (args) free(args);
            return call_res;
        }

        case AST_FUNC_DECL: {
            int param_count = 0;
            ASTNode* curr_param = node->left; // params (AST_EXPR_LIST)
            while (curr_param) { param_count++; curr_param = curr_param->right; }
            
            LLVMTypeRef* param_types = NULL;
            if (param_count > 0) {
                param_types = malloc(sizeof(LLVMTypeRef) * param_count);
                for (int i = 0; i < param_count; i++) param_types[i] = LLVMInt32TypeInContext(context);
            }
            
            LLVMTypeRef func_type = LLVMFunctionType(LLVMInt32TypeInContext(context), param_types, param_count, 0);
            LLVMValueRef func = LLVMAddFunction(module, node->var_name, func_type);
            if (param_types) free(param_types);
            
            LLVMBasicBlockRef old_bb = LLVMGetInsertBlock(builder);
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, func, "entry");
            LLVMPositionBuilderAtEnd(builder, entry);
            
            int old_local_count = local_count;
            local_count = 1; // ローカル変数は1から開始
            
            curr_param = node->left;
            int p_idx = 0;
            while (curr_param) {
                char* p_name = curr_param->left->var_name;
                LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, LLVMInt32TypeInContext(context), p_name);
                local_names[local_count] = strdup(p_name);
                local_vars[local_count] = alloca_inst;
                local_count++;
                
                LLVMValueRef arg_val = LLVMGetParam(func, p_idx);
                LLVMBuildStore(builder, arg_val, alloca_inst);
                
                curr_param = curr_param->right; p_idx++;
            }
            
            generate_node(node->body); // 関数本体の評価
            
            // ターミネータ(return等)が無ければダミーのreturnを追加して検証エラーを防ぐ
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildRet(builder, LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0));
            }
            
            local_count = old_local_count;
            LLVMPositionBuilderAtEnd(builder, old_bb);
            return NULL;
        }
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

    void ginc_push_try(void* jmpbuf);
    LLVMAddSymbol("ginc_push_try", (void*)ginc_push_try);

    void ginc_pop_try(void);
    LLVMAddSymbol("ginc_pop_try", (void*)ginc_pop_try);

    void ginc_throw(int val);
    LLVMAddSymbol("ginc_throw", (void*)ginc_throw);

    int _setjmp(void* env);
    LLVMAddSymbol("_setjmp", (void*)_setjmp);

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
