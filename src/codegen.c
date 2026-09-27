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
static LLVMTypeRef local_types[MAX_LOCALS];
static int local_count = 0;

LLVMValueRef generate_node(ASTNode* node) {
    if (!node) return NULL;

    // 現在のブロックがすでにターミネータ(returnやthrow等)を持っている場合は、
    // 後続のノード（デッドコード）のIR生成をスキップする
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
        return NULL;
    }

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
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], node->var_name) == 0) {
                    if (!local_vars[i] || !local_types[i]) return NULL;
                    return LLVMBuildLoad2(builder, local_types[i], local_vars[i], node->var_name);
                }
            }
            LLVMValueRef var = LLVMGetNamedGlobal(module, node->var_name);
            if (!var) {
                LLVMValueRef fn = LLVMGetNamedFunction(module, node->var_name);
                if (fn && LLVMIsAFunction(fn)) {
                    return fn;
                }
                fprintf(stderr, "Ga! (Codegen Error): Unknown variable '%s'\n", node->var_name);
                return NULL;
            }
            return LLVMBuildLoad2(builder, LLVMGlobalGetValueType(var), var, node->var_name);
        }

        case AST_VARDECL: {
            LLVMValueRef init_val = generate_node(node->right);
            if (!init_val) return NULL;

            if (LLVMGetTypeKind(LLVMTypeOf(init_val)) == LLVMPointerTypeKind && !LLVMIsAFunction(init_val)) {
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                LLVMTypeRef param_types[] = { ptr_type };
                LLVMTypeRef unwrap_type = LLVMFunctionType(LLVMInt32TypeInContext(context), param_types, 1, 0);
                LLVMValueRef unwrap_func = LLVMGetNamedFunction(module, "ginc_unwrap_int");
                if (!unwrap_func) {
                    unwrap_func = LLVMAddFunction(module, "ginc_unwrap_int", unwrap_type);
                }
                LLVMValueRef args[] = { init_val };
                init_val = LLVMBuildCall2(builder, unwrap_type, unwrap_func, args, 1, "unwrapped");
            }

            if (local_count > 0) { // 関数内のローカル変数
                LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, LLVMInt32TypeInContext(context), node->var_name);
                local_names[local_count] = strdup(node->var_name);
                local_vars[local_count] = alloca_inst;
                local_types[local_count] = LLVMInt32TypeInContext(context);
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

        case AST_CLASS_DEF:
            return NULL;

        case AST_HASH_DECL: {
            LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMTypeRef func_type = LLVMFunctionType(ptr_type, NULL, 0, 0);
            LLVMValueRef create_func = LLVMGetNamedFunction(module, "ginc_hash_create");
            if (!create_func) {
                create_func = LLVMAddFunction(module, "ginc_hash_create", func_type);
            }

            LLVMValueRef ht_ptr = LLVMBuildCall2(builder, func_type, create_func, NULL, 0, "ht");

            char const* const name = node->var_name;
            if (local_count > 0) {
                LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, ptr_type, name);
                local_names[local_count] = strdup(name);
                local_vars[local_count] = alloca_inst;
                local_types[local_count] = ptr_type;
                local_count++;
                LLVMBuildStore(builder, ht_ptr, alloca_inst);
            } else {
                LLVMValueRef var = LLVMAddGlobal(module, ptr_type, name);
                LLVMSetInitializer(var, LLVMConstNull(ptr_type));
                LLVMBuildStore(builder, ht_ptr, var);
            }
            return ht_ptr;
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
            LLVMValueRef idx_val = generate_node(node->left);
            if (!idx_val) return NULL;

            char const* const var_name = node->var_name;

            if (LLVMGetTypeKind(LLVMTypeOf(idx_val)) == LLVMPointerTypeKind) {
                // ハッシュアクセス (GincValue* を返す)
                LLVMValueRef ht_var = NULL;
                for (int i = local_count - 1; i >= 1; i--) {
                    if (local_names[i] && strcmp(local_names[i], var_name) == 0) {
                        ht_var = local_vars[i]; break;
                    }
                }
                if (!ht_var) ht_var = LLVMGetNamedGlobal(module, var_name);
                if (!ht_var) {
                    fprintf(stderr, "Ga! (Codegen Error): Unknown variable '%s'\n", var_name);
                    return NULL;
                }

                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                LLVMValueRef ht_ptr = LLVMBuildLoad2(builder, ptr_type, ht_var, var_name);
                if (!ht_ptr) return NULL;

                LLVMTypeRef param_types[] = { ptr_type, ptr_type };
                LLVMTypeRef func_type = LLVMFunctionType(ptr_type, param_types, 2, 0);
                LLVMValueRef get_func = LLVMGetNamedFunction(module, "ginc_hash_get");
                if (!get_func) {
                    get_func = LLVMAddFunction(module, "ginc_hash_get", func_type);
                }

                LLVMValueRef args[] = { ht_ptr, idx_val };
                return LLVMBuildCall2(builder, func_type, get_func, args, 2, "hashval");
            }

            // 配列アクセス
            LLVMValueRef array_var = LLVMGetNamedGlobal(module, var_name);
            if (!array_var) {
                fprintf(stderr, "Ga! (Codegen Error): Undeclared array '%s'\n", var_name);
                return NULL;
            }

            LLVMTypeRef array_type = LLVMGlobalGetValueType(array_var);
            if (LLVMGetTypeKind(array_type) != LLVMArrayTypeKind) {
                fprintf(stderr, "Ga! (Codegen Error): Variable '%s' is not an array\n", var_name);
                return NULL;
            }

            unsigned int const array_len = LLVMGetArrayLength(array_type);
            LLVMValueRef len_val = LLVMConstInt(LLVMInt32TypeInContext(context), array_len, 0);

            LLVMValueRef is_oob = LLVMBuildICmp(builder, LLVMIntUGE, idx_val, len_val, "is_oob");
            LLVMValueRef parent_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(context, parent_func, "array_oob");
            LLVMBasicBlockRef inbounds_bb = LLVMAppendBasicBlockInContext(context, parent_func, "array_inbounds");

            LLVMBuildCondBr(builder, is_oob, oob_bb, inbounds_bb);

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

            LLVMPositionBuilderAtEnd(builder, inbounds_bb);
            LLVMValueRef indices[] = {
                LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0),
                idx_val
            };
            LLVMValueRef gep = LLVMBuildGEP2(builder, array_type, array_var, indices, 2, "arrayidx");
            LLVMTypeRef elem_type = LLVMGetElementType(array_type);
            return LLVMBuildLoad2(builder, elem_type, gep, var_name);
        }

        case AST_INDEX_ASSIGN: {
            LLVMValueRef idx_val = generate_node(node->left);
            LLVMValueRef rhs_val = generate_node(node->right);
            if (!idx_val || !rhs_val) return NULL;

            char const* const var_name = node->var_name;

            if (LLVMGetTypeKind(LLVMTypeOf(idx_val)) == LLVMPointerTypeKind) {
                // ハッシュキー代入 (GincValue* へボクシングして格納)
                LLVMValueRef ht_var = NULL;
                for (int i = local_count - 1; i >= 1; i--) {
                    if (local_names[i] && strcmp(local_names[i], var_name) == 0) {
                        ht_var = local_vars[i]; break;
                    }
                }
                if (!ht_var) ht_var = LLVMGetNamedGlobal(module, var_name);
                if (!ht_var) {
                    fprintf(stderr, "Ga! (Codegen Error): Unknown variable '%s'\n", var_name);
                    return NULL;
                }

                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                LLVMValueRef ht_ptr = LLVMBuildLoad2(builder, ptr_type, ht_var, var_name);

                LLVMValueRef boxed_val = NULL;
                if (LLVMGetTypeKind(LLVMTypeOf(rhs_val)) == LLVMIntegerTypeKind) {
                    LLVMTypeRef param_types[] = { LLVMInt32TypeInContext(context) };
                    LLVMTypeRef mk_func_type = LLVMFunctionType(ptr_type, param_types, 1, 0);
                    LLVMValueRef mk_func = LLVMGetNamedFunction(module, "ginc_make_int");
                    if (!mk_func) {
                        mk_func = LLVMAddFunction(module, "ginc_make_int", mk_func_type);
                    }
                    LLVMValueRef args[] = { rhs_val };
                    boxed_val = LLVMBuildCall2(builder, mk_func_type, mk_func, args, 1, "boxed_int");
                } else if (LLVMIsAFunction(rhs_val)) {
                    LLVMTypeRef param_types[] = { ptr_type };
                    LLVMTypeRef mk_func_type = LLVMFunctionType(ptr_type, param_types, 1, 0);
                    LLVMValueRef mk_func = LLVMGetNamedFunction(module, "ginc_make_func");
                    if (!mk_func) {
                        mk_func = LLVMAddFunction(module, "ginc_make_func", mk_func_type);
                    }
                    LLVMValueRef args[] = { rhs_val };
                    boxed_val = LLVMBuildCall2(builder, mk_func_type, mk_func, args, 1, "boxed_func");
                } else {
                    LLVMTypeRef param_types[] = { ptr_type };
                    LLVMTypeRef mk_func_type = LLVMFunctionType(ptr_type, param_types, 1, 0);
                    LLVMValueRef mk_func = LLVMGetNamedFunction(module, "ginc_make_str");
                    if (!mk_func) {
                        mk_func = LLVMAddFunction(module, "ginc_make_str", mk_func_type);
                    }
                    LLVMValueRef args[] = { rhs_val };
                    boxed_val = LLVMBuildCall2(builder, mk_func_type, mk_func, args, 1, "boxed_str");
                }

                LLVMTypeRef set_param_types[] = { ptr_type, ptr_type, ptr_type };
                LLVMTypeRef set_func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), set_param_types, 3, 0);
                LLVMValueRef set_func = LLVMGetNamedFunction(module, "ginc_hash_set");
                if (!set_func) {
                    set_func = LLVMAddFunction(module, "ginc_hash_set", set_func_type);
                }

                LLVMValueRef args[] = { ht_ptr, idx_val, boxed_val };
                LLVMBuildCall2(builder, set_func_type, set_func, args, 3, "");
                return rhs_val;
            }

            // 配列インデックス代入
            LLVMValueRef array_var = LLVMGetNamedGlobal(module, var_name);
            if (!array_var) {
                fprintf(stderr, "Ga! (Codegen Error): Undeclared array '%s'\n", var_name);
                return NULL;
            }

            LLVMTypeRef array_type = LLVMGlobalGetValueType(array_var);
            LLVMValueRef indices[] = {
                LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0),
                idx_val
            };
            LLVMValueRef gep = LLVMBuildGEP2(builder, array_type, array_var, indices, 2, "arrayidx");
            LLVMBuildStore(builder, rhs_val, gep);
            return rhs_val;
        }

        case AST_ASSIGN: {
            LLVMValueRef rhs = generate_node(node->right);
            if (!rhs) return NULL;

            if (LLVMGetTypeKind(LLVMTypeOf(rhs)) == LLVMPointerTypeKind && !LLVMIsAFunction(rhs)) {
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                LLVMTypeRef param_types[] = { ptr_type };
                LLVMTypeRef unwrap_type = LLVMFunctionType(LLVMInt32TypeInContext(context), param_types, 1, 0);
                LLVMValueRef unwrap_func = LLVMGetNamedFunction(module, "ginc_unwrap_int");
                if (!unwrap_func) {
                    unwrap_func = LLVMAddFunction(module, "ginc_unwrap_int", unwrap_type);
                }
                LLVMValueRef args[] = { rhs };
                rhs = LLVMBuildCall2(builder, unwrap_type, unwrap_func, args, 1, "unwrapped");
            }

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

            if (LLVMGetTypeKind(LLVMTypeOf(arg_val)) == LLVMPointerTypeKind) {
                if (node->left->type == AST_ARRAY_ACCESS) {
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                    LLVMTypeRef param_types[] = { ptr_type };
                    LLVMTypeRef print_val_type = LLVMFunctionType(LLVMVoidTypeInContext(context), param_types, 1, 0);
                    LLVMValueRef print_val_func = LLVMGetNamedFunction(module, "ginc_print_val");
                    if (!print_val_func) {
                        print_val_func = LLVMAddFunction(module, "ginc_print_val", print_val_type);
                    }
                    LLVMValueRef args[] = { arg_val };
                    return LLVMBuildCall2(builder, print_val_type, print_val_func, args, 1, "");
                }

                LLVMTypeRef param_types[] = { LLVMPointerType(LLVMInt8TypeInContext(context), 0) };
                LLVMTypeRef printf_type = LLVMFunctionType(LLVMInt32TypeInContext(context), param_types, 1, 1);
                LLVMValueRef printf_func = LLVMGetNamedFunction(module, "printf");
                if (!printf_func) {
                    printf_func = LLVMAddFunction(module, "printf", printf_type);
                }
                LLVMValueRef format_str = LLVMBuildGlobalStringPtr(builder, "%s\n", "fmt");
                LLVMValueRef args[] = { format_str, arg_val };
                return LLVMBuildCall2(builder, printf_type, printf_func, args, 2, "printf_call");
            }

            LLVMTypeRef param_types[] = { LLVMPointerType(LLVMInt8TypeInContext(context), 0) };
            LLVMTypeRef printf_type = LLVMFunctionType(LLVMInt32TypeInContext(context), param_types, 1, 1);
            LLVMValueRef printf_func = LLVMGetNamedFunction(module, "printf");
            if (!printf_func) {
                printf_func = LLVMAddFunction(module, "printf", printf_type);
            }
            LLVMValueRef format_str = LLVMBuildGlobalStringPtr(builder, "%d\n", "fmt");
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
            if (!val) return NULL;
            if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
                LLVMTypeRef param_types[] = { LLVMInt32TypeInContext(context) };
                LLVMTypeRef func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), param_types, 1, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(module, "ginc_throw_int");
                if (!fn) {
                    fn = LLVMAddFunction(module, "ginc_throw_int", func_type);
                }
                LLVMBuildCall2(builder, func_type, fn, &val, 1, "");
            } else {
                LLVMTypeRef param_types[] = { LLVMPointerType(LLVMInt8TypeInContext(context), 0) };
                LLVMTypeRef func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), param_types, 1, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(module, "ginc_throw_obj");
                if (!fn) {
                    fn = LLVMAddFunction(module, "ginc_throw_obj", func_type);
                }
                LLVMBuildCall2(builder, func_type, fn, &val, 1, "");
            }
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
                    local_types[local_count] = i32_type;
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

            LLVMTypeRef jmpbuf_type = LLVMArrayType(LLVMInt8TypeInContext(context), 1024);
            LLVMValueRef jmpbuf_alloca = LLVMBuildAlloca(builder, jmpbuf_type, "jmpbuf");

            LLVMValueRef rethrow_val = LLVMBuildAlloca(builder, i32_type, "rethrow_val");
            LLVMBuildStore(builder, LLVMConstInt(i32_type, 0, 0), rethrow_val);

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

            LLVMValueRef push_args[] = { jmpbuf_alloca };
            LLVMBuildCall2(builder, push_func_type, push_func, push_args, 1, "");

            LLVMValueRef setjmp_args[] = { jmpbuf_alloca };
            LLVMValueRef setjmp_res = LLVMBuildCall2(builder, setjmp_func_type, setjmp_func, setjmp_args, 1, "setjmp_res");

            LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
            LLVMValueRef is_zero = LLVMBuildICmp(builder, LLVMIntEQ, setjmp_res, zero, "is_zero");

            LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef try_bb = LLVMAppendBasicBlockInContext(context, func, "try_body");
            LLVMBasicBlockRef catch_bb = LLVMAppendBasicBlockInContext(context, func, "catch_body");
            LLVMBasicBlockRef finally_bb = LLVMAppendBasicBlockInContext(context, func, "finally_body");
            LLVMBasicBlockRef rethrow_bb = LLVMAppendBasicBlockInContext(context, func, "rethrow_bb");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(context, func, "try_cont");

            LLVMBuildCondBr(builder, is_zero, try_bb, catch_bb);

            // [try_body]
            LLVMPositionBuilderAtEnd(builder, try_bb);
            generate_node(node->left);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMValueRef pop_fn = LLVMGetNamedFunction(module, "ginc_pop_try");
                LLVMBuildCall2(builder, LLVMGlobalGetValueType(pop_fn), pop_fn, NULL, 0, "");
                LLVMBuildBr(builder, finally_bb);
            }

            // [catch_body]
            LLVMPositionBuilderAtEnd(builder, catch_bb);
            LLVMBuildStore(builder, setjmp_res, rethrow_val);

            ASTNode* curr_catch = node->right; // catch_blocks (AST_EXPR_LIST)
            while (curr_catch) {
                ASTNode* catch_node = curr_catch->left; // AST_CATCH_BLOCK

                LLVMBasicBlockRef next_catch_bb = LLVMAppendBasicBlockInContext(context, func, "next_catch");
                LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(context, func, "catch_match");

                LLVMValueRef catch_type_val = LLVMConstInt(i32_type, catch_node->catch_type, 0);
                LLVMTypeRef check_param_types[] = { i32_type };
                LLVMTypeRef check_func_type = LLVMFunctionType(i32_type, check_param_types, 1, 0);
                LLVMValueRef check_fn = LLVMGetNamedFunction(module, "ginc_check_catch");
                if (!check_fn) {
                    check_fn = LLVMAddFunction(module, "ginc_check_catch", check_func_type);
                }
                LLVMValueRef check_res = LLVMBuildCall2(builder, check_func_type, check_fn, &catch_type_val, 1, "check_res");
                LLVMValueRef is_match = LLVMBuildICmp(builder, LLVMIntNE, check_res, zero, "is_match");
                LLVMBuildCondBr(builder, is_match, match_bb, next_catch_bb);

                // [match_bb]
                LLVMPositionBuilderAtEnd(builder, match_bb);
                LLVMBuildStore(builder, zero, rethrow_val);

                LLVMValueRef exc_val;
                if (catch_node->catch_type == TYPE_INT) {
                    LLVMTypeRef get_func_type = LLVMFunctionType(i32_type, NULL, 0, 0);
                    LLVMValueRef get_fn = LLVMGetNamedFunction(module, "ginc_get_exc_int");
                    if (!get_fn) {
                        get_fn = LLVMAddFunction(module, "ginc_get_exc_int", get_func_type);
                    }
                    exc_val = LLVMBuildCall2(builder, get_func_type, get_fn, NULL, 0, "exc_val");
                } else {
                    LLVMTypeRef get_func_type = LLVMFunctionType(ptr_type, NULL, 0, 0);
                    LLVMValueRef get_fn = LLVMGetNamedFunction(module, "ginc_get_exc_obj");
                    if (!get_fn) {
                        get_fn = LLVMAddFunction(module, "ginc_get_exc_obj", get_func_type);
                    }
                    exc_val = LLVMBuildCall2(builder, get_func_type, get_fn, NULL, 0, "exc_val");
                }

                if (catch_node->var_name && exc_val) {
                    int old_local_count = local_count;
                    if (local_count == 0) local_count = 1;

                    LLVMValueRef var_alloc = LLVMBuildAlloca(builder, LLVMTypeOf(exc_val), catch_node->var_name);
                    if (var_alloc) {
                        local_names[local_count] = strdup(catch_node->var_name);
                        local_vars[local_count] = var_alloc;
                        local_types[local_count] = LLVMTypeOf(exc_val);
                        local_count++;

                        LLVMBuildStore(builder, exc_val, var_alloc);
                    }

                    generate_node(catch_node->left);

                    local_count = old_local_count;
                } else {
                    generate_node(catch_node->left);
                }

                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                    LLVMBuildBr(builder, finally_bb);
                }

                LLVMPositionBuilderAtEnd(builder, next_catch_bb);
                curr_catch = curr_catch->right;
            }

            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildBr(builder, finally_bb);
            }

            // [finally_body]
            LLVMPositionBuilderAtEnd(builder, finally_bb);
            if (node->finally_body) {
                generate_node(node->finally_body);
            }

            LLVMValueRef rv = LLVMBuildLoad2(builder, i32_type, rethrow_val, "rv");
            LLVMValueRef is_rethrow = LLVMBuildICmp(builder, LLVMIntNE, rv, zero, "is_rethrow");
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildCondBr(builder, is_rethrow, rethrow_bb, merge_bb);
            }

            // [rethrow_bb]
            LLVMPositionBuilderAtEnd(builder, rethrow_bb);
            LLVMValueRef throw_fn = LLVMGetNamedFunction(module, "ginc_throw");
            if (!throw_fn) {
                LLVMTypeRef throw_param_types[] = { i32_type };
                LLVMTypeRef throw_func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), throw_param_types, 1, 0);
                throw_fn = LLVMAddFunction(module, "ginc_throw", throw_func_type);
            }
            LLVMBuildCall2(builder, LLVMGlobalGetValueType(throw_fn), throw_fn, &rv, 1, "");
            LLVMBuildUnreachable(builder);

            LLVMPositionBuilderAtEnd(builder, merge_bb);
            return NULL;
        }

        case AST_CATCH_BLOCK:
            return NULL;

        case AST_METHOD_CALL: {
            char const* const obj_name = node->var_name;
            char const* const method_name = node->str_val;

            LLVMValueRef ht_var = NULL;
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], obj_name) == 0) {
                    ht_var = local_vars[i]; break;
                }
            }
            if (!ht_var) ht_var = LLVMGetNamedGlobal(module, obj_name);
            if (!ht_var) {
                fprintf(stderr, "Ga! (Codegen Error): Unknown object '%s'\n", obj_name);
                return NULL;
            }

            LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMValueRef ht_ptr = LLVMBuildLoad2(builder, ptr_type, ht_var, obj_name);
            LLVMValueRef key_val = LLVMBuildGlobalStringPtr(builder, method_name, "method_key");

            LLVMTypeRef get_param_types[] = { ptr_type, ptr_type };
            LLVMTypeRef get_func_type = LLVMFunctionType(ptr_type, get_param_types, 2, 0);
            LLVMValueRef get_func = LLVMGetNamedFunction(module, "ginc_hash_get");
            if (!get_func) {
                get_func = LLVMAddFunction(module, "ginc_hash_get", get_func_type);
            }
            LLVMValueRef get_args[] = { ht_ptr, key_val };
            LLVMValueRef boxed_func = LLVMBuildCall2(builder, get_func_type, get_func, get_args, 2, "boxed_method");

            LLVMTypeRef unwrap_param_types[] = { ptr_type };
            LLVMTypeRef unwrap_func_type = LLVMFunctionType(ptr_type, unwrap_param_types, 1, 0);
            LLVMValueRef unwrap_func = LLVMGetNamedFunction(module, "ginc_unwrap_func");
            if (!unwrap_func) {
                unwrap_func = LLVMAddFunction(module, "ginc_unwrap_func", unwrap_func_type);
            }
            LLVMValueRef unwrap_args[] = { boxed_func };
            LLVMValueRef raw_func = LLVMBuildCall2(builder, unwrap_func_type, unwrap_func, unwrap_args, 1, "raw_method");

            LLVMValueRef args[16];
            LLVMTypeRef arg_types[16];
            int arg_count = 0;
            ASTNode* curr_arg = node->left;
            while (curr_arg && arg_count < 16) {
                LLVMValueRef arg_val = generate_node(curr_arg->left);
                args[arg_count] = arg_val;
                arg_types[arg_count] = LLVMTypeOf(arg_val);
                arg_count++;
                curr_arg = curr_arg->right;
            }

            LLVMTypeRef method_type = LLVMFunctionType(LLVMInt32TypeInContext(context), arg_types, arg_count, 0);
            return LLVMBuildCall2(builder, method_type, raw_func, args, arg_count, "method_res");
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
                local_types[local_count] = LLVMInt32TypeInContext(context);
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

    void* ginc_hash_create(void);
    LLVMAddSymbol("ginc_hash_create", (void*)ginc_hash_create);

    void* ginc_make_int(int v);
    LLVMAddSymbol("ginc_make_int", (void*)ginc_make_int);

    void* ginc_make_str(char const* v);
    LLVMAddSymbol("ginc_make_str", (void*)ginc_make_str);

    void* ginc_make_func(void* f);
    LLVMAddSymbol("ginc_make_func", (void*)ginc_make_func);

    void* ginc_unwrap_func(void* val);
    LLVMAddSymbol("ginc_unwrap_func", (void*)ginc_unwrap_func);

    void ginc_print_val(void* val);
    LLVMAddSymbol("ginc_print_val", (void*)ginc_print_val);

    void ginc_hash_set(void* ht, char const* key, void* val);
    LLVMAddSymbol("ginc_hash_set", (void*)ginc_hash_set);

    void* ginc_hash_get(void* ht, char const* key);
    LLVMAddSymbol("ginc_hash_get", (void*)ginc_hash_get);

    int ginc_unwrap_int(void* val);
    LLVMAddSymbol("ginc_unwrap_int", (void*)ginc_unwrap_int);

    void ginc_throw_int(int val);
    LLVMAddSymbol("ginc_throw_int", (void*)ginc_throw_int);

    void ginc_throw_obj(void* obj);
    LLVMAddSymbol("ginc_throw_obj", (void*)ginc_throw_obj);

    int ginc_check_catch(int target_type);
    LLVMAddSymbol("ginc_check_catch", (void*)ginc_check_catch);

    int ginc_get_exc_int(void);
    LLVMAddSymbol("ginc_get_exc_int", (void*)ginc_get_exc_int);

    void* ginc_get_exc_obj(void);
    LLVMAddSymbol("ginc_get_exc_obj", (void*)ginc_get_exc_obj);

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
