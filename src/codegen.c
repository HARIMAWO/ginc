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
static char* local_classes[MAX_LOCALS];
static int local_is_const[MAX_LOCALS];
static int local_ptr_targets[MAX_LOCALS];
static LLVMTypeRef local_fn_types[MAX_LOCALS];
static int local_count = 0;

static char* global_var_names[100];
static char* global_classes[100];
static int global_is_const[100];
static int global_ptr_targets[100];
static LLVMTypeRef global_fn_types[100];
static int global_var_count = 0;

static int lambda_counter = 0;

#define MAX_ENUM_CONSTS 256
static char* enum_names[MAX_ENUM_CONSTS];
static int enum_vals[MAX_ENUM_CONSTS];
static int enum_count = 0;

#define MAX_LABELS 64
static char* label_names[MAX_LABELS];
static LLVMBasicBlockRef label_bbs[MAX_LABELS];
static int label_count = 0;

static LLVMBasicBlockRef get_or_create_label_bb(char const* name) {
	for (int i = 0; i < label_count; i++) {
		if (label_names[i] && strcmp(label_names[i], name) == 0) {
			return label_bbs[i];
		}
	}
	LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
	LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(context, func, name);
	if (label_count < MAX_LABELS) {
		label_names[label_count] = strdup(name);
		label_bbs[label_count] = bb;
		label_count++;
	}
	return bb;
}

static int lookup_enum(char const* name, int* out_val) {
	for (int i = enum_count - 1; i >= 0; i--) {
		if (enum_names[i] && strcmp(enum_names[i], name) == 0) {
			if (out_val) *out_val = enum_vals[i];
			return 1;
		}
	}
	return 0;
}

static int is_const_var(char const* name) {
	for (int i = local_count - 1; i >= 1; i--) {
		if (local_names[i] && strcmp(local_names[i], name) == 0) {
			return local_is_const[i];
		}
	}
	for (int i = 0; i < global_var_count; i++) {
		if (global_var_names[i] && strcmp(global_var_names[i], name) == 0) {
			return global_is_const[i];
		}
	}
	return 0;
}

static int get_ptr_target_type(char const* name) {
	for (int i = local_count - 1; i >= 1; i--) {
		if (local_names[i] && strcmp(local_names[i], name) == 0) {
			return local_ptr_targets[i];
		}
	}
	for (int i = 0; i < global_var_count; i++) {
		if (global_var_names[i] && strcmp(global_var_names[i], name) == 0) {
			return global_ptr_targets[i];
		}
	}
	return -1;
}

static int infer_array_elem_type(char const* name) {
	LLVMValueRef arr = NULL;
	LLVMTypeRef at = NULL;
	for (int i = local_count - 1; i >= 1; i--) {
		if (local_names[i] && strcmp(local_names[i], name) == 0) {
			arr = local_vars[i]; at = local_types[i]; break;
		}
	}
	if (!arr) {
		arr = LLVMGetNamedGlobal(module, name);
		if (arr) at = LLVMGlobalGetValueType(arr);
	}
	if (arr && at && LLVMGetTypeKind(at) == LLVMArrayTypeKind) {
		LLVMTypeRef et = LLVMGetElementType(at);
		LLVMTypeKind ek = LLVMGetTypeKind(et);
		if (ek == LLVMFloatTypeKind) return TYPE_FLOAT;
		if (ek == LLVMDoubleTypeKind) return TYPE_DOUBLE;
		if (ek == LLVMPointerTypeKind) return TYPE_STR;
		if (ek == LLVMIntegerTypeKind) {
			unsigned w = LLVMGetIntTypeWidth(et);
			if (w == 8) return TYPE_CHAR;
			if (w == 16) return TYPE_SHORT;
			if (w == 64) return TYPE_LONG;
			return TYPE_INT;
		}
	}
	return -1;
}

static int infer_ptr_target_type(ASTNode* expr) {
	if (!expr) return -1;
	if (expr->type == AST_VAR) {
		int pt = get_ptr_target_type(expr->var_name);
		if (pt >= 0) return pt;
		return infer_array_elem_type(expr->var_name);
	}
	if (expr->type == AST_ADDR_OF) {
		if (expr->left && expr->left->type == AST_ARRAY_ACCESS) {
			return infer_array_elem_type(expr->left->var_name);
		}
		if (expr->left && expr->left->type == AST_VAR) {
			for (int i = local_count - 1; i >= 1; i--) {
				if (local_names[i] && strcmp(local_names[i], expr->left->var_name) == 0) {
					LLVMTypeRef vt = local_types[i];
					if (LLVMGetTypeKind(vt) == LLVMArrayTypeKind) {
						return infer_array_elem_type(expr->left->var_name);
					}
					if (LLVMGetTypeKind(vt) == LLVMFloatTypeKind) return TYPE_FLOAT;
					if (LLVMGetTypeKind(vt) == LLVMDoubleTypeKind) return TYPE_DOUBLE;
					if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind) {
						unsigned w = LLVMGetIntTypeWidth(vt);
						if (w == 8) return TYPE_CHAR;
						if (w == 16) return TYPE_SHORT;
						if (w == 64) return TYPE_LONG;
					}
					return TYPE_INT;
				}
			}
			LLVMValueRef gv = LLVMGetNamedGlobal(module, expr->left->var_name);
			if (gv) {
				LLVMTypeRef vt = LLVMGlobalGetValueType(gv);
				if (LLVMGetTypeKind(vt) == LLVMArrayTypeKind) {
					return infer_array_elem_type(expr->left->var_name);
				}
				if (LLVMGetTypeKind(vt) == LLVMFloatTypeKind) return TYPE_FLOAT;
				if (LLVMGetTypeKind(vt) == LLVMDoubleTypeKind) return TYPE_DOUBLE;
				if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind) {
					unsigned w = LLVMGetIntTypeWidth(vt);
					if (w == 8) return TYPE_CHAR;
					if (w == 16) return TYPE_SHORT;
					if (w == 64) return TYPE_LONG;
				}
				return TYPE_INT;
			}
		}
		return TYPE_INT;
	}
	if (expr->type == AST_ADD || expr->type == AST_SUB) {
		int lt = infer_ptr_target_type(expr->left);
		if (lt >= 0) return lt;
		return infer_ptr_target_type(expr->right);
	}
	if (expr->type == AST_POST_INC || expr->type == AST_POST_DEC) {
		return infer_ptr_target_type(expr->left);
	}
	if (expr->type == AST_STRING || expr->type == AST_STR) {
		return TYPE_CHAR;
	}
	return -1;
}

static int get_llvm_elem_size(LLVMTypeRef t) {
	LLVMTypeKind k = LLVMGetTypeKind(t);
	if (k == LLVMFloatTypeKind) return 4;
	if (k == LLVMDoubleTypeKind) return 8;
	if (k == LLVMPointerTypeKind) return 8;
	if (k == LLVMIntegerTypeKind) {
		unsigned w = LLVMGetIntTypeWidth(t);
		if (w <= 8) return 1;
		if (w <= 16) return 2;
		if (w <= 32) return 4;
		return 8;
	}
	return 4;
}

static char current_codegen_class[256] = "";

typedef struct {
	char* class_name;
	char* method_keys[50];
	char* method_funcs[50];
	int method_count;
	char* keys[100];
	int access[100]; // 0: private, 1: public
	int count;
} ClassMeta;

static ClassMeta class_meta[50];
static int class_meta_count = 0;

typedef struct {
	char* func_name;
	int param_count;
	int param_types[20];
	int is_variadic;
} FuncMeta;

static FuncMeta func_meta[100];
static int func_meta_count = 0;

#define MAX_CONTROL_DEPTH 64
static LLVMBasicBlockRef break_target_stack[MAX_CONTROL_DEPTH];
static int break_depth = 0;
static LLVMBasicBlockRef continue_target_stack[MAX_CONTROL_DEPTH];
static int continue_depth = 0;

static void check_access(char const* target_class, char const* member_name) {
	for (int i = 0; i < class_meta_count; i++) {
		if (strcmp(class_meta[i].class_name, target_class) == 0) {
			for (int m = 0; m < class_meta[i].count; m++) {
				if (strcmp(class_meta[i].keys[m], member_name) == 0) {
					if (class_meta[i].access[m] == 0 && strcmp(current_codegen_class, target_class) != 0) {
						fprintf(stderr, "Ga! (Compile Error): '%s' is a private member of '%s'\n", member_name, target_class);
						exit(1);
					}
					return;
				}
			}
		}
	}
}

static char* get_class_of_var(char const* var_name) {
	for (int i = local_count - 1; i >= 1; i--) {
		if (local_names[i] && strcmp(local_names[i], var_name) == 0) return local_classes[i];
	}
	for (int i = 0; i < global_var_count; i++) {
		if (global_var_names[i] && strcmp(global_var_names[i], var_name) == 0) return global_classes[i];
	}
	return NULL;
}

static LLVMValueRef auto_unwrap(LLVMValueRef val) {
	if (!val) return NULL;
	if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind) {
		LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
		LLVMTypeRef param_types[] = { ptr_type };
		LLVMTypeRef unwrap_type = LLVMFunctionType(LLVMInt32TypeInContext(context), param_types, 1, 0);
		LLVMValueRef unwrap_func = LLVMGetNamedFunction(module, "ginc_unwrap_int");
		if (!unwrap_func) unwrap_func = LLVMAddFunction(module, "ginc_unwrap_int", unwrap_type);
		LLVMValueRef args[] = { val };
		return LLVMBuildCall2(builder, unwrap_type, unwrap_func, args, 1, "unwrapped");
	}
	return val;
}

static LLVMTypeRef ginc_type_to_llvm(GincType t) {
	switch (t) {
		case TYPE_CHAR:
		case TYPE_BOOL:
			return LLVMInt8TypeInContext(context);
		case TYPE_SHORT:
			return LLVMInt16TypeInContext(context);
		case TYPE_LONG:
			return LLVMInt64TypeInContext(context);
		case TYPE_FLOAT:
			return LLVMFloatTypeInContext(context);
		case TYPE_DOUBLE:
			return LLVMDoubleTypeInContext(context);
		case TYPE_VOID:
			return LLVMVoidTypeInContext(context);
		case TYPE_FILE:
		case TYPE_STR:
		case TYPE_VAR:
		case TYPE_PTR:
			return LLVMPointerType(LLVMInt8TypeInContext(context), 0);
		case TYPE_VA_LIST:
			return LLVMArrayType(LLVMInt8TypeInContext(context), 32);
		case TYPE_INT:
		case TYPE_UINT:
		default:
			return LLVMInt32TypeInContext(context);
	}
}

static LLVMTypeRef build_llvm_func_type(GincType ret_type, ASTNode* params) {
	int pcnt = 0;
	for (ASTNode* c = params; c != NULL; c = c->right) pcnt++;
	LLVMTypeRef* ptypes = NULL;
	if (pcnt > 0) {
		ptypes = malloc(sizeof(LLVMTypeRef) * (size_t)pcnt);
		int i = 0;
		for (ASTNode* c = params; c != NULL; c = c->right) {
			ptypes[i++] = ginc_type_to_llvm(c->left->var_type);
		}
	}
	LLVMTypeRef ft = LLVMFunctionType(ginc_type_to_llvm(ret_type), ptypes, (unsigned)pcnt, 0);
	if (ptypes) free(ptypes);
	return ft;
}

static int is_fp_type(LLVMTypeRef t) {
	LLVMTypeKind const k = LLVMGetTypeKind(t);
	return (k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind);
}

static LLVMValueRef coerce_value(LLVMValueRef val, LLVMTypeRef target_type, int is_bool) {
	if (!val || !target_type) return val;
	LLVMTypeKind const dst_kind = LLVMGetTypeKind(target_type);
	if (dst_kind != LLVMPointerTypeKind && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind && !LLVMIsAFunction(val)) {
		val = auto_unwrap(val);
	}
	if (is_bool) {
		LLVMTypeRef vt = LLVMTypeOf(val);
		LLVMValueRef cmp = NULL;
		if (is_fp_type(vt)) {
			cmp = LLVMBuildFCmp(builder, LLVMRealONE, val, LLVMConstReal(vt, 0.0), "bool_fcmp");
		} else {
			cmp = LLVMBuildICmp(builder, LLVMIntNE, val, LLVMConstInt(vt, 0, 0), "bool_icmp");
		}
		return LLVMBuildZExt(builder, cmp, target_type, "bool_zext");
	}
	LLVMTypeRef src_type = LLVMTypeOf(val);
	if (src_type == target_type) return val;

	LLVMTypeKind const src_kind = LLVMGetTypeKind(src_type);
	if (dst_kind == LLVMPointerTypeKind && src_kind == LLVMIntegerTypeKind) {
		if (LLVMIsAConstantInt(val) && LLVMConstIntGetZExtValue(val) == 0) {
			return LLVMConstNull(target_type);
		}
		return LLVMBuildIntToPtr(builder, val, target_type, "inttoptr");
	}
	if (src_kind == LLVMIntegerTypeKind && dst_kind == LLVMIntegerTypeKind) {
		unsigned const sw = LLVMGetIntTypeWidth(src_type);
		unsigned const dw = LLVMGetIntTypeWidth(target_type);
		if (sw < dw) return LLVMBuildSExt(builder, val, target_type, "sexttmp");
		if (sw > dw) return LLVMBuildTrunc(builder, val, target_type, "trunctmp");
		return val;
	}
	if (is_fp_type(src_type) && is_fp_type(target_type)) {
		if (src_kind == LLVMFloatTypeKind && dst_kind == LLVMDoubleTypeKind) {
			return LLVMBuildFPExt(builder, val, target_type, "fpexttmp");
		}
		if (src_kind == LLVMDoubleTypeKind && dst_kind == LLVMFloatTypeKind) {
			return LLVMBuildFPTrunc(builder, val, target_type, "fptrunctmp");
		}
		return val;
	}
	if (src_kind == LLVMIntegerTypeKind && is_fp_type(target_type)) {
		return LLVMBuildSIToFP(builder, val, target_type, "sitofptmp");
	}
	if (is_fp_type(src_type) && dst_kind == LLVMIntegerTypeKind) {
		return LLVMBuildFPToSI(builder, val, target_type, "fptositmp");
	}
	return val;
}

static void promote_arithmetic(LLVMValueRef* lhs, LLVMValueRef* rhs) {
	*lhs = auto_unwrap(*lhs);
	*rhs = auto_unwrap(*rhs);
	LLVMTypeRef lt = LLVMTypeOf(*lhs);
	LLVMTypeRef rt = LLVMTypeOf(*rhs);
	LLVMTypeKind lk = LLVMGetTypeKind(lt);
	LLVMTypeKind rk = LLVMGetTypeKind(rt);
	if (lk == LLVMDoubleTypeKind || rk == LLVMDoubleTypeKind) {
		LLVMTypeRef dt = LLVMDoubleTypeInContext(context);
		*lhs = coerce_value(*lhs, dt, 0);
		*rhs = coerce_value(*rhs, dt, 0);
		return;
	}
	if (lk == LLVMFloatTypeKind || rk == LLVMFloatTypeKind) {
		LLVMTypeRef ft = LLVMFloatTypeInContext(context);
		*lhs = coerce_value(*lhs, ft, 0);
		*rhs = coerce_value(*rhs, ft, 0);
		return;
	}
	if (lk == LLVMIntegerTypeKind && rk == LLVMIntegerTypeKind) {
		unsigned lw = LLVMGetIntTypeWidth(lt);
		unsigned rw = LLVMGetIntTypeWidth(rt);
		unsigned w = (lw > rw) ? lw : rw;
		if (w < 32) w = 32;
		LLVMTypeRef it = LLVMIntTypeInContext(context, w);
		*lhs = coerce_value(*lhs, it, 0);
		*rhs = coerce_value(*rhs, it, 0);
	}
}

static LLVMValueRef call_ptr_make(LLVMValueRef base, LLVMValueRef index, LLVMValueRef length, int elem_size, int kind) {
	LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
	LLVMTypeRef i64_type = LLVMInt64TypeInContext(context);
	LLVMTypeRef i32_type = LLVMInt32TypeInContext(context);
	LLVMTypeRef param_types[] = { ptr_type, i64_type, i64_type, i32_type, i32_type };
	LLVMTypeRef fn_type = LLVMFunctionType(ptr_type, param_types, 5, 0);
	LLVMValueRef fn = LLVMGetNamedFunction(module, "ginc_ptr_make");
	if (!fn) fn = LLVMAddFunction(module, "ginc_ptr_make", fn_type);
	LLVMValueRef args[] = {
		base,
		coerce_value(index, i64_type, 0),
		coerce_value(length, i64_type, 0),
		LLVMConstInt(i32_type, (unsigned long long)elem_size, 1),
		LLVMConstInt(i32_type, (unsigned long long)kind, 1)
	};
	return LLVMBuildCall2(builder, fn_type, fn, args, 5, "fatptr");
}

static LLVMValueRef call_ptr_make_cstr(LLVMValueRef cstr_val) {
	LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
	LLVMTypeRef param_types[] = { ptr_type };
	LLVMTypeRef fn_type = LLVMFunctionType(ptr_type, param_types, 1, 0);
	LLVMValueRef fn = LLVMGetNamedFunction(module, "ginc_ptr_make_cstr");
	if (!fn) fn = LLVMAddFunction(module, "ginc_ptr_make_cstr", fn_type);
	LLVMValueRef args[] = { cstr_val };
	return LLVMBuildCall2(builder, fn_type, fn, args, 1, "cstrptr");
}

static LLVMValueRef call_ptr_elem_addr(LLVMValueRef fat_ptr, LLVMValueRef offset) {
	LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
	LLVMTypeRef i64_type = LLVMInt64TypeInContext(context);
	LLVMTypeRef param_types[] = { ptr_type, i64_type };
	LLVMTypeRef fn_type = LLVMFunctionType(ptr_type, param_types, 2, 0);
	LLVMValueRef fn = LLVMGetNamedFunction(module, "ginc_ptr_elem_addr");
	if (!fn) fn = LLVMAddFunction(module, "ginc_ptr_elem_addr", fn_type);
	LLVMValueRef args[] = { fat_ptr, coerce_value(offset, i64_type, 0) };
	return LLVMBuildCall2(builder, fn_type, fn, args, 2, "elemaddr");
}

static LLVMValueRef call_ptr_add(LLVMValueRef fat_ptr, LLVMValueRef offset) {
	LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
	LLVMTypeRef i64_type = LLVMInt64TypeInContext(context);
	LLVMTypeRef param_types[] = { ptr_type, i64_type };
	LLVMTypeRef fn_type = LLVMFunctionType(ptr_type, param_types, 2, 0);
	LLVMValueRef fn = LLVMGetNamedFunction(module, "ginc_ptr_add");
	if (!fn) fn = LLVMAddFunction(module, "ginc_ptr_add", fn_type);
	LLVMValueRef args[] = { fat_ptr, coerce_value(offset, i64_type, 0) };
	return LLVMBuildCall2(builder, fn_type, fn, args, 2, "ptradd");
}

LLVMValueRef generate_node(ASTNode* node) {
    if (!node) return NULL;

    // 現在のブロックがすでにターミネータ(returnやthrow等)を持っている場合は、
    // 後続のノード（デッドコード）のIR生成をスキップする
    // ただし AST_LABEL と AST_SEQ（内部のラベル到達のため）は通過させる
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)) &&
        node->type != AST_LABEL && node->type != AST_SEQ) {
        return NULL;
    }

    switch (node->type) {
        case AST_NUM:
            if (node->val > INT32_MAX || node->val < INT32_MIN) {
                return LLVMConstInt(LLVMInt64TypeInContext(context), (unsigned long long)node->val, 1);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)node->val, 1);

        case AST_FLOAT:
            return LLVMConstReal(LLVMDoubleTypeInContext(context), node->float_val);

        case AST_NULL: {
            LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMTypeRef func_type = LLVMFunctionType(ptr_type, NULL, 0, 0);
            LLVMValueRef null_func = LLVMGetNamedFunction(module, "ginc_make_null");
            if (!null_func) null_func = LLVMAddFunction(module, "ginc_make_null", func_type);
            return LLVMBuildCall2(builder, func_type, null_func, NULL, 0, "boxed_null");
        }

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
                    if (LLVMGetTypeKind(local_types[i]) == LLVMArrayTypeKind) {
                        unsigned const alen = LLVMGetArrayLength(local_types[i]);
                        int const esize = get_llvm_elem_size(LLVMGetElementType(local_types[i]));
                        return call_ptr_make(local_vars[i],
                            LLVMConstInt(LLVMInt64TypeInContext(context), 0, 0),
                            LLVMConstInt(LLVMInt64TypeInContext(context), alen, 0),
                            esize, 1);
                    }
                    return LLVMBuildLoad2(builder, local_types[i], local_vars[i], node->var_name);
                }
            }
            LLVMValueRef var = LLVMGetNamedGlobal(module, node->var_name);
            if (!var) {
                int eval = 0;
                if (lookup_enum(node->var_name, &eval)) {
                    return LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)eval, 1);
                }
                LLVMValueRef fn = LLVMGetNamedFunction(module, node->var_name);
                if (fn && LLVMIsAFunction(fn)) {
                    return fn;
                }
                fprintf(stderr, "Ga! (Codegen Error): Unknown variable '%s'\n", node->var_name);
                return NULL;
            }
            LLVMTypeRef gtype = LLVMGlobalGetValueType(var);
            if (LLVMGetTypeKind(gtype) == LLVMArrayTypeKind) {
                unsigned const alen = LLVMGetArrayLength(gtype);
                int const esize = get_llvm_elem_size(LLVMGetElementType(gtype));
                return call_ptr_make(var,
                    LLVMConstInt(LLVMInt64TypeInContext(context), 0, 0),
                    LLVMConstInt(LLVMInt64TypeInContext(context), alen, 0),
                    esize, 1);
            }
            return LLVMBuildLoad2(builder, gtype, var, node->var_name);
        }

        case AST_VARDECL: {
            LLVMTypeRef var_llvm_type = ginc_type_to_llvm(node->var_type);
            LLVMValueRef init_val = NULL;
            if (node->right) {
                init_val = generate_node(node->right);
                if (init_val) {
                    if (node->var_type == TYPE_PTR && node->ptr_target_type == TYPE_CHAR &&
                        (node->right->type == AST_STRING || node->right->type == AST_STR)) {
                        init_val = call_ptr_make_cstr(init_val);
                    } else if (node->var_type == TYPE_PTR && node->ptr_target_type != TYPE_VOID &&
                               LLVMGetTypeKind(LLVMTypeOf(init_val)) == LLVMPointerTypeKind &&
                               node->right->type == AST_FUNC_CALL) {
                        /* malloc() 等から返された void* ポインタをターゲット型の要素サイズに再設定 */
                        int const esize = get_llvm_elem_size(ginc_type_to_llvm(node->ptr_target_type));
                        LLVMTypeRef ptr_t = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                        LLVMTypeRef rt_ptypes[] = { ptr_t, LLVMInt32TypeInContext(context) };
                        LLVMTypeRef rt_ftype = LLVMFunctionType(ptr_t, rt_ptypes, 2, 0);
                        LLVMValueRef rt_fn = LLVMGetNamedFunction(module, "ginc_ptr_retype");
                        if (!rt_fn) rt_fn = LLVMAddFunction(module, "ginc_ptr_retype", rt_ftype);
                        LLVMValueRef rargs[] = { init_val, LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)esize, 0) };
                        init_val = LLVMBuildCall2(builder, rt_ftype, rt_fn, rargs, 2, "retyped");
                    } else {
                        init_val = coerce_value(init_val, var_llvm_type, node->var_type == TYPE_BOOL);
                    }
                }
            }

            if (!init_val) {
                init_val = LLVMConstNull(var_llvm_type);
            }

            int const ptr_t = (node->var_type == TYPE_PTR) ? (int)node->ptr_target_type : -1;
            LLVMTypeRef fn_t = node->is_func_ptr ? build_llvm_func_type(node->fn_ret_type, node->left) : NULL;
            if (local_count > 0 && node->is_static) {
                LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
                char const* const fn_name = LLVMGetValueName(cur_fn);
                char static_name[512];
                snprintf(static_name, sizeof(static_name), "__static_%s_%s", fn_name ? fn_name : "fn", node->var_name);
                LLVMValueRef gvar = LLVMAddGlobal(module, var_llvm_type, static_name);
                if (init_val && LLVMIsConstant(init_val)) {
                    LLVMSetInitializer(gvar, init_val);
                } else {
                    LLVMSetInitializer(gvar, LLVMConstNull(var_llvm_type));
                }
                local_names[local_count] = strdup(node->var_name);
                local_vars[local_count] = gvar;
                local_types[local_count] = var_llvm_type;
                local_classes[local_count] = NULL;
                local_is_const[local_count] = node->is_const;
                local_ptr_targets[local_count] = ptr_t;
                local_fn_types[local_count] = fn_t;
                local_count++;
                return init_val;
            } else if (local_count > 0) { // 関数内のローカル変数
                LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, var_llvm_type, node->var_name);
                local_names[local_count] = strdup(node->var_name);
                local_vars[local_count] = alloca_inst;
                local_types[local_count] = var_llvm_type;
                local_classes[local_count] = NULL;
                local_is_const[local_count] = node->is_const;
                local_ptr_targets[local_count] = ptr_t;
                local_fn_types[local_count] = fn_t;
                local_count++;
                if (node->var_type != TYPE_VA_LIST) {
                    LLVMBuildStore(builder, init_val, alloca_inst);
                }
                return init_val;
            } else { // グローバル変数
                LLVMValueRef var = LLVMAddGlobal(module, var_llvm_type, node->var_name);
                LLVMSetInitializer(var, LLVMConstNull(var_llvm_type));
                LLVMBuildStore(builder, init_val, var);
                global_var_names[global_var_count] = strdup(node->var_name);
                global_classes[global_var_count] = NULL;
                global_is_const[global_var_count] = node->is_const;
                global_ptr_targets[global_var_count] = ptr_t;
                global_fn_types[global_var_count] = fn_t;
                global_var_count++;
                return init_val;
            }
        }

        case AST_TYPEDEF:
        case AST_ENUM_ITEM:
            return NULL;

        case AST_VA_START: {
            LLVMValueRef va_ptr = NULL;
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], node->var_name) == 0) {
                    va_ptr = local_vars[i]; break;
                }
            }
            if (!va_ptr) return NULL;
            LLVMTypeRef ptr_t = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMTypeRef ptypes[] = { ptr_t };
            LLVMTypeRef fn_t = LLVMFunctionType(LLVMVoidTypeInContext(context), ptypes, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(module, "llvm.va_start.p0");
            if (!fn) fn = LLVMAddFunction(module, "llvm.va_start.p0", fn_t);
            LLVMValueRef args[] = { va_ptr };
            return LLVMBuildCall2(builder, fn_t, fn, args, 1, "");
        }

        case AST_VA_END: {
            LLVMValueRef va_ptr = NULL;
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], node->var_name) == 0) {
                    va_ptr = local_vars[i]; break;
                }
            }
            if (!va_ptr) return NULL;
            LLVMTypeRef ptr_t = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMTypeRef ptypes[] = { ptr_t };
            LLVMTypeRef fn_t = LLVMFunctionType(LLVMVoidTypeInContext(context), ptypes, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(module, "llvm.va_end.p0");
            if (!fn) fn = LLVMAddFunction(module, "llvm.va_end.p0", fn_t);
            LLVMValueRef args[] = { va_ptr };
            return LLVMBuildCall2(builder, fn_t, fn, args, 1, "");
        }

        case AST_VA_ARG: {
            LLVMValueRef va_ptr = NULL;
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], node->var_name) == 0) {
                    va_ptr = local_vars[i]; break;
                }
            }
            if (!va_ptr) return NULL;
            return LLVMBuildVAArg(builder, va_ptr, ginc_type_to_llvm(node->var_type), "vaarg");
        }

        case AST_COMMA: {
            generate_node(node->left);
            return generate_node(node->right);
        }

        case AST_GOTO: {
            LLVMBasicBlockRef target_bb = get_or_create_label_bb(node->var_name);
            LLVMBuildBr(builder, target_bb);
            return NULL;
        }

        case AST_LABEL: {
            LLVMBasicBlockRef label_bb = get_or_create_label_bb(node->var_name);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildBr(builder, label_bb);
            }
            LLVMPositionBuilderAtEnd(builder, label_bb);
            if (node->left) {
                generate_node(node->left);
            }
            return NULL;
        }

        case AST_SIZEOF_TYPE: {
            int sz = 4;
            if (node->var_type == TYPE_PTR) {
                sz = 8;
            } else {
                sz = get_llvm_elem_size(ginc_type_to_llvm(node->var_type));
            }
            return LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)sz, 0);
        }

        case AST_SIZEOF_EXPR: {
            int sz = 4;
            ASTNode* ex = node->left;
            if (ex && ex->type == AST_VAR) {
                LLVMValueRef v = NULL;
                LLVMTypeRef vt = NULL;
                for (int i = local_count - 1; i >= 1; i--) {
                    if (local_names[i] && strcmp(local_names[i], ex->var_name) == 0) {
                        v = local_vars[i]; vt = local_types[i]; break;
                    }
                }
                if (!v) {
                    v = LLVMGetNamedGlobal(module, ex->var_name);
                    if (v) vt = LLVMGlobalGetValueType(v);
                }
                if (v && vt) {
                    if (LLVMGetTypeKind(vt) == LLVMArrayTypeKind) {
                        LLVMTypeRef et = LLVMGetElementType(vt);
                        if (LLVMGetTypeKind(et) == LLVMArrayTypeKind) {
                            sz = (int)LLVMGetArrayLength(vt) * (int)LLVMGetArrayLength(et) * get_llvm_elem_size(LLVMGetElementType(et));
                        } else {
                            sz = (int)LLVMGetArrayLength(vt) * get_llvm_elem_size(et);
                        }
                    } else {
                        sz = get_llvm_elem_size(vt);
                    }
                    return LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)sz, 0);
                }
            } else if (ex && ex->type == AST_ARRAY_ACCESS) {
                int et = infer_array_elem_type(ex->var_name);
                if (et >= 0) {
                    sz = get_llvm_elem_size(ginc_type_to_llvm((GincType)et));
                    return LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)sz, 0);
                }
            } else if (ex && ex->type == AST_FLOAT) {
                sz = 8;
                return LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)sz, 0);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)sz, 0);
        }

        case AST_CAST: {
            LLVMValueRef val = generate_node(node->left);
            if (!val) return NULL;
            return coerce_value(val, ginc_type_to_llvm(node->var_type), node->var_type == TYPE_BOOL);
        }

        case AST_ADDR_OF: {
            if (!node->left) return NULL;
            if (node->left->type == AST_VAR) {
                char const* const vname = node->left->var_name;
                LLVMValueRef var = NULL;
                LLVMTypeRef vtype = NULL;
                for (int i = local_count - 1; i >= 1; i--) {
                    if (local_names[i] && strcmp(local_names[i], vname) == 0) {
                        var = local_vars[i]; vtype = local_types[i]; break;
                    }
                }
                if (!var) {
                    var = LLVMGetNamedGlobal(module, vname);
                    if (var) vtype = LLVMGlobalGetValueType(var);
                }
                if (!var || !vtype) {
                    fprintf(stderr, "Ga! (Codegen Error): Unknown variable '%s' in '&'\n", vname);
                    return NULL;
                }
                if (LLVMGetTypeKind(vtype) == LLVMArrayTypeKind) {
                    unsigned const alen = LLVMGetArrayLength(vtype);
                    int const esize = get_llvm_elem_size(LLVMGetElementType(vtype));
                    return call_ptr_make(var,
                        LLVMConstInt(LLVMInt64TypeInContext(context), 0, 0),
                        LLVMConstInt(LLVMInt64TypeInContext(context), alen, 0),
                        esize, 1);
                }
                int const esize = get_llvm_elem_size(vtype);
                return call_ptr_make(var,
                    LLVMConstInt(LLVMInt64TypeInContext(context), (unsigned long long)-1, 1),
                    LLVMConstInt(LLVMInt64TypeInContext(context), 0, 0),
                    esize, 0);
            }
            if (node->left->type == AST_ARRAY_ACCESS) {
                char const* const vname = node->left->var_name;
                LLVMValueRef idx_val = generate_node(node->left->left);
                LLVMValueRef arr = NULL;
                LLVMTypeRef atype = NULL;
                for (int i = local_count - 1; i >= 1; i--) {
                    if (local_names[i] && strcmp(local_names[i], vname) == 0) {
                        arr = local_vars[i]; atype = local_types[i]; break;
                    }
                }
                if (!arr) {
                    arr = LLVMGetNamedGlobal(module, vname);
                    if (arr) atype = LLVMGlobalGetValueType(arr);
                }
                if (!arr || !atype || LLVMGetTypeKind(atype) != LLVMArrayTypeKind) {
                    fprintf(stderr, "Ga! (Codegen Error): Invalid '&' target '%s'\n", vname);
                    return NULL;
                }
                unsigned const alen = LLVMGetArrayLength(atype);
                int const esize = get_llvm_elem_size(LLVMGetElementType(atype));
                return call_ptr_make(arr, idx_val,
                    LLVMConstInt(LLVMInt64TypeInContext(context), alen, 0),
                    esize, 1);
            }
            fprintf(stderr, "Ga! (Codegen Error): Invalid operand for '&'\n");
            return NULL;
        }

        case AST_DEREF: {
            LLVMValueRef ptr_val = generate_node(node->left);
            if (!ptr_val) return NULL;
            int const pt = infer_ptr_target_type(node->left);
            LLVMTypeRef elem_t = (pt >= 0) ? ginc_type_to_llvm((GincType)pt) : LLVMInt32TypeInContext(context);
            LLVMValueRef addr = call_ptr_elem_addr(ptr_val, LLVMConstInt(LLVMInt64TypeInContext(context), 0, 0));
            return LLVMBuildLoad2(builder, elem_t, addr, "deref");
        }

        case AST_DEREF_ASSIGN: {
            LLVMValueRef ptr_val = generate_node(node->left);
            LLVMValueRef rhs_val = generate_node(node->right);
            if (!ptr_val || !rhs_val) return NULL;
            int const pt = infer_ptr_target_type(node->left);
            LLVMTypeRef elem_t = (pt >= 0) ? ginc_type_to_llvm((GincType)pt) : LLVMInt32TypeInContext(context);
            LLVMValueRef addr = call_ptr_elem_addr(ptr_val, LLVMConstInt(LLVMInt64TypeInContext(context), 0, 0));
            rhs_val = coerce_value(rhs_val, elem_t, pt == TYPE_BOOL);
            LLVMBuildStore(builder, rhs_val, addr);
            return rhs_val;
        }

        case AST_ENUM_DEF: {
            int next_val = 0;
            ASTNode* curr = node->left;
            while (curr) {
                if (curr->left && curr->left->type == AST_ENUM_ITEM) {
                    ASTNode* item = curr->left;
                    int val = next_val;
                    if (item->left) {
                        val = item->left->val;
                    }
                    if (enum_count < MAX_ENUM_CONSTS) {
                        enum_names[enum_count] = strdup(item->var_name);
                        enum_vals[enum_count] = val;
                        enum_count++;
                    }
                    next_val = val + 1;
                }
                curr = curr->right;
            }
            return NULL;
        }

        case AST_CLASS_DEF: {
            ClassMeta meta;
            meta.class_name = strdup(node->var_name);
            meta.method_count = 0;
            meta.count = 0;
            ASTNode* curr = node->left; // class_member_list
            while (curr) {
                if (curr->left) {
                    if (curr->left->type == AST_FUNC_DECL || curr->left->type == AST_PARAM_DECL) {
                        char const* const mkey = (curr->left->type == AST_FUNC_DECL) ? curr->left->str_val : curr->left->var_name;
                        meta.keys[meta.count] = strdup(mkey);
                        meta.access[meta.count] = curr->left->access;
                        meta.count++;
                    }
                    if (curr->left->type == AST_FUNC_DECL) {
                        meta.method_keys[meta.method_count] = strdup(curr->left->str_val);
                        meta.method_funcs[meta.method_count] = strdup(curr->left->var_name);
                        meta.method_count++;
                        generate_node(curr->left);
                    }
                }
                curr = curr->right;
            }
            class_meta[class_meta_count++] = meta;
            return NULL;
        }

        case AST_CLASS_VAR_DECL: {
            LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMTypeRef func_type = LLVMFunctionType(ptr_type, NULL, 0, 0);
            LLVMValueRef create_func = LLVMGetNamedFunction(module, "ginc_hash_create");
            if (!create_func) create_func = LLVMAddFunction(module, "ginc_hash_create", func_type);
            LLVMValueRef ht_ptr = LLVMBuildCall2(builder, func_type, create_func, NULL, 0, "ht");

            char const* const name = node->var_name;
            if (local_count > 0) {
                LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, ptr_type, name);
                local_names[local_count] = strdup(name);
                local_vars[local_count] = alloca_inst;
                local_types[local_count] = ptr_type;
                local_classes[local_count] = strdup(node->str_val);
                local_count++;
                LLVMBuildStore(builder, ht_ptr, alloca_inst);
            } else {
                LLVMValueRef var = LLVMAddGlobal(module, ptr_type, name);
                LLVMSetInitializer(var, LLVMConstNull(ptr_type));
                LLVMBuildStore(builder, ht_ptr, var);

                global_var_names[global_var_count] = strdup(name);
                global_classes[global_var_count] = strdup(node->str_val);
                global_var_count++;
            }

            for (int i = 0; i < class_meta_count; i++) {
                if (strcmp(class_meta[i].class_name, node->str_val) == 0) {
                    for (int m = 0; m < class_meta[i].method_count; m++) {
                        LLVMValueRef method_fn = LLVMGetNamedFunction(module, class_meta[i].method_funcs[m]);
                        if (!method_fn) continue;
                        
                        LLVMTypeRef mk_param_types[] = { ptr_type };
                        LLVMTypeRef mk_func_type = LLVMFunctionType(ptr_type, mk_param_types, 1, 0);
                        LLVMValueRef mk_func = LLVMGetNamedFunction(module, "ginc_make_func");
                        if (!mk_func) mk_func = LLVMAddFunction(module, "ginc_make_func", mk_func_type);
                        
                        LLVMValueRef boxed_func = LLVMBuildCall2(builder, mk_func_type, mk_func, &method_fn, 1, "boxed_func");
                        
                        LLVMValueRef key_str = LLVMBuildGlobalStringPtr(builder, class_meta[i].method_keys[m], "method_key");
                        LLVMTypeRef set_param_types[] = { ptr_type, ptr_type, ptr_type };
                        LLVMTypeRef set_func_type = LLVMFunctionType(LLVMVoidTypeInContext(context), set_param_types, 3, 0);
                        LLVMValueRef set_func = LLVMGetNamedFunction(module, "ginc_hash_set");
                        if (!set_func) set_func = LLVMAddFunction(module, "ginc_hash_set", set_func_type);
                        
                        LLVMValueRef args[] = { ht_ptr, key_str, boxed_func };
                        LLVMBuildCall2(builder, set_func_type, set_func, args, 3, "");
                    }
                    break;
                }
            }
            return ht_ptr;
        }

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
            int array_len = node->val;
            if (array_len < 0) {
                array_len = 0;
                ASTNode* cnt = node->right;
                while (cnt) {
                    array_len++;
                    cnt = cnt->right;
                }
                if (array_len <= 0) {
                    fprintf(stderr, "Ga! (Compile Error): Array '%s' has empty initializer\n", array_name);
                    exit(1);
                }
            }

            LLVMTypeRef elem_type = ginc_type_to_llvm(node->var_type);

            LLVMTypeRef array_type = (node->dim2 > 0)
                ? LLVMArrayType(LLVMArrayType(elem_type, (unsigned)node->dim2), (unsigned)array_len)
                : LLVMArrayType(elem_type, (unsigned)array_len);
            LLVMValueRef array_var = NULL;

            if (local_count > 0) {
                array_var = LLVMBuildAlloca(builder, array_type, array_name);
                local_names[local_count] = strdup(array_name);
                local_vars[local_count] = array_var;
                local_types[local_count] = array_type;
                local_classes[local_count] = NULL;
                local_is_const[local_count] = 0;
                local_ptr_targets[local_count] = -1;
                local_fn_types[local_count] = NULL;
                local_count++;
                /* 初期化リストがない要素もゼロで埋める */
                LLVMBuildStore(builder, LLVMConstNull(array_type), array_var);
            } else {
                if (LLVMGetNamedGlobal(module, array_name)) {
                    fprintf(stderr, "Ga! (Codegen Error): Array '%s' is already declared\n", array_name);
                    return NULL;
                }
                array_var = LLVMAddGlobal(module, array_type, array_name);
                LLVMSetInitializer(array_var, LLVMConstNull(array_type));
            }

            if (node->dim2 > 0) {
                ASTNode* row_curr = node->right;
                int r = 0;
                while (row_curr && r < array_len) {
                    ASTNode* col_curr = row_curr->left;
                    int c = 0;
                    while (col_curr && c < node->dim2) {
                        LLVMValueRef elem_val = generate_node(col_curr->left);
                        if (elem_val) {
                            elem_val = coerce_value(elem_val, elem_type, node->var_type == TYPE_BOOL);
                            LLVMValueRef indices[] = {
                                LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0),
                                LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)r, 0),
                                LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)c, 0)
                            };
                            LLVMValueRef gep = LLVMBuildGEP2(builder, array_type, array_var, indices, 3, "arr2didx");
                            LLVMBuildStore(builder, elem_val, gep);
                        }
                        col_curr = col_curr->right;
                        c++;
                    }
                    row_curr = row_curr->right;
                    r++;
                }
                return array_var;
            }

            ASTNode* curr = node->right;
            int idx = 0;
            while (curr && idx < array_len) {
                LLVMValueRef elem_val = generate_node(curr->left);
                if (elem_val) {
                    elem_val = coerce_value(elem_val, elem_type, node->var_type == TYPE_BOOL);
                    LLVMValueRef indices[] = {
                        LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0),
                        LLVMConstInt(LLVMInt32TypeInContext(context), (unsigned long long)idx, 0)
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

            char const* target_class = get_class_of_var(var_name);
            if (target_class) {
                if (node->left && node->left->type == AST_STR) {
                    check_access(target_class, node->left->var_name);
                } else if (node->left && node->left->type == AST_STRING) {
                    check_access(target_class, node->left->str_val);
                }
            }

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

            // ポインタ変数への添字アクセス p[i]
            int const ptr_t = get_ptr_target_type(var_name);
            if (ptr_t >= 0) {
                LLVMValueRef pvar = NULL;
                LLVMTypeRef ptype = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                for (int i = local_count - 1; i >= 1; i--) {
                    if (local_names[i] && strcmp(local_names[i], var_name) == 0) {
                        pvar = local_vars[i]; break;
                    }
                }
                if (!pvar) pvar = LLVMGetNamedGlobal(module, var_name);
                if (!pvar) return NULL;
                LLVMValueRef pval = LLVMBuildLoad2(builder, ptype, pvar, var_name);
                LLVMValueRef addr = call_ptr_elem_addr(pval, idx_val);
                return LLVMBuildLoad2(builder, ginc_type_to_llvm((GincType)ptr_t), addr, "ptridx");
            }

            // 配列アクセス
            LLVMValueRef array_var = NULL;
            LLVMTypeRef array_type = NULL;
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], var_name) == 0) {
                    array_var = local_vars[i];
                    array_type = local_types[i];
                    break;
                }
            }
            if (!array_var) {
                array_var = LLVMGetNamedGlobal(module, var_name);
                if (array_var) {
                    array_type = LLVMGlobalGetValueType(array_var);
                }
            }
            if (!array_var || !array_type) {
                fprintf(stderr, "Ga! (Codegen Error): Undeclared array '%s'\n", var_name);
                return NULL;
            }
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
            if (node->cond) {
                LLVMValueRef idx2_val = generate_node(node->cond);
                LLVMValueRef indices2d[] = {
                    LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0),
                    idx_val,
                    idx2_val
                };
                LLVMValueRef gep = LLVMBuildGEP2(builder, array_type, array_var, indices2d, 3, "arr2didx");
                LLVMTypeRef elem_type = LLVMGetElementType(LLVMGetElementType(array_type));
                return LLVMBuildLoad2(builder, elem_type, gep, var_name);
            }
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

            char const* target_class = get_class_of_var(var_name);
            if (target_class) {
                if (node->left && node->left->type == AST_STR) {
                    check_access(target_class, node->left->var_name);
                } else if (node->left && node->left->type == AST_STRING) {
                    check_access(target_class, node->left->str_val);
                }
            }

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
                    if (!mk_func) mk_func = LLVMAddFunction(module, "ginc_make_int", mk_func_type);
                    LLVMValueRef args[] = { rhs_val };
                    boxed_val = LLVMBuildCall2(builder, mk_func_type, mk_func, args, 1, "boxed_int");
                } else if (LLVMGetTypeKind(LLVMTypeOf(rhs_val)) == LLVMDoubleTypeKind) {
                    LLVMTypeRef param_types[] = { LLVMDoubleTypeInContext(context) };
                    LLVMTypeRef mk_func_type = LLVMFunctionType(ptr_type, param_types, 1, 0);
                    LLVMValueRef mk_func = LLVMGetNamedFunction(module, "ginc_make_double");
                    if (!mk_func) mk_func = LLVMAddFunction(module, "ginc_make_double", mk_func_type);
                    LLVMValueRef args[] = { rhs_val };
                    boxed_val = LLVMBuildCall2(builder, mk_func_type, mk_func, args, 1, "boxed_double");
                } else if (LLVMGetTypeKind(LLVMTypeOf(rhs_val)) == LLVMPointerTypeKind && LLVMIsAFunction(rhs_val)) {
                    LLVMTypeRef param_types[] = { ptr_type };
                    LLVMTypeRef mk_func_type = LLVMFunctionType(ptr_type, param_types, 1, 0);
                    LLVMValueRef mk_func = LLVMGetNamedFunction(module, "ginc_make_func");
                    if (!mk_func) mk_func = LLVMAddFunction(module, "ginc_make_func", mk_func_type);
                    LLVMValueRef args[] = { rhs_val };
                    boxed_val = LLVMBuildCall2(builder, mk_func_type, mk_func, args, 1, "boxed_func");
                } else {
                    boxed_val = rhs_val; // null (ginc_make_null) も文字列も、既にBoxedなポインタなのでそのまま代入
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

            // ポインタ変数への添字代入 p[i] = val
            int const ptr_t = get_ptr_target_type(var_name);
            if (ptr_t >= 0) {
                LLVMValueRef pvar = NULL;
                LLVMTypeRef ptype = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                for (int i = local_count - 1; i >= 1; i--) {
                    if (local_names[i] && strcmp(local_names[i], var_name) == 0) {
                        pvar = local_vars[i]; break;
                    }
                }
                if (!pvar) pvar = LLVMGetNamedGlobal(module, var_name);
                if (!pvar) return NULL;
                LLVMValueRef pval = LLVMBuildLoad2(builder, ptype, pvar, var_name);
                LLVMValueRef addr = call_ptr_elem_addr(pval, idx_val);
                LLVMTypeRef elem_t = ginc_type_to_llvm((GincType)ptr_t);
                rhs_val = coerce_value(rhs_val, elem_t, ptr_t == TYPE_BOOL);
                LLVMBuildStore(builder, rhs_val, addr);
                return rhs_val;
            }

            // 配列インデックス代入
            LLVMValueRef array_var = NULL;
            LLVMTypeRef array_type = NULL;
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], var_name) == 0) {
                    array_var = local_vars[i];
                    array_type = local_types[i];
                    break;
                }
            }
            if (!array_var) {
                array_var = LLVMGetNamedGlobal(module, var_name);
                if (array_var) {
                    array_type = LLVMGlobalGetValueType(array_var);
                }
            }
            if (!array_var || !array_type) {
                fprintf(stderr, "Ga! (Codegen Error): Undeclared array '%s'\n", var_name);
                return NULL;
            }
            if (node->cond) {
                LLVMValueRef idx2_val = generate_node(node->cond);
                LLVMValueRef indices2d[] = {
                    LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0),
                    idx_val,
                    idx2_val
                };
                LLVMValueRef gep = LLVMBuildGEP2(builder, array_type, array_var, indices2d, 3, "arr2didx");
                rhs_val = coerce_value(rhs_val, LLVMGetElementType(LLVMGetElementType(array_type)), 0);
                LLVMBuildStore(builder, rhs_val, gep);
                return rhs_val;
            }
            LLVMValueRef indices[] = {
                LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0),
                idx_val
            };
            LLVMValueRef gep = LLVMBuildGEP2(builder, array_type, array_var, indices, 2, "arrayidx");
            rhs_val = coerce_value(rhs_val, LLVMGetElementType(array_type), 0);
            LLVMBuildStore(builder, rhs_val, gep);
            return rhs_val;
        }

        case AST_ASSIGN: {
            if (node->left && node->left->type == AST_DEREF) {
                LLVMValueRef ptr_val = generate_node(node->left->left);
                LLVMValueRef rhs_val = generate_node(node->right);
                if (!ptr_val || !rhs_val) return NULL;
                int const pt = infer_ptr_target_type(node->left->left);
                LLVMTypeRef elem_t = (pt >= 0) ? ginc_type_to_llvm((GincType)pt) : LLVMInt32TypeInContext(context);
                LLVMValueRef addr = call_ptr_elem_addr(ptr_val, LLVMConstInt(LLVMInt64TypeInContext(context), 0, 0));
                rhs_val = coerce_value(rhs_val, elem_t, pt == TYPE_BOOL);
                LLVMBuildStore(builder, rhs_val, addr);
                return rhs_val;
            }
            if (!node->left || node->left->type != AST_VAR) {
                fprintf(stderr, "Ga! (Compile Error): Expression is not assignable\n");
                exit(1);
            }
            LLVMValueRef rhs = generate_node(node->right);
            if (!rhs) return NULL;

            char const* const var_name = node->left->var_name;
            if (is_const_var(var_name)) {
                fprintf(stderr, "Ga! (Compile Error): Assignment of read-only variable '%s'\n", var_name);
                exit(1);
            }
            LLVMValueRef var = NULL;
            LLVMTypeRef target_type = NULL;
            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], var_name) == 0) {
                    var = local_vars[i];
                    target_type = local_types[i];
                    break;
                }
            }
            if (!var) {
                var = LLVMGetNamedGlobal(module, var_name);
                if (var) target_type = LLVMGlobalGetValueType(var);
            }
            if (!var || !target_type) {
                fprintf(stderr, "Ga! (Codegen Error): Undeclared variable '%s'\n", var_name);
                return NULL;
            }
            if (get_ptr_target_type(var_name) == TYPE_CHAR && node->right &&
                (node->right->type == AST_STRING || node->right->type == AST_STR)) {
                rhs = call_ptr_make_cstr(rhs);
            } else {
                rhs = coerce_value(rhs, target_type, 0);
            }
            LLVMBuildStore(builder, rhs, var);
            return rhs;
        }

        case AST_ADD: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            if (!lhs || !rhs) return NULL;

            if (infer_ptr_target_type(node->left) >= 0 &&
                node->left->type != AST_STR && node->left->type != AST_STRING) {
                return call_ptr_add(lhs, auto_unwrap(rhs));
            }
            if (infer_ptr_target_type(node->right) >= 0 &&
                node->right->type != AST_STR && node->right->type != AST_STRING) {
                return call_ptr_add(rhs, auto_unwrap(lhs));
            }
            
            int const lhs_is_ptr = (LLVMGetTypeKind(LLVMTypeOf(lhs)) == LLVMPointerTypeKind);
            int const rhs_is_ptr = (LLVMGetTypeKind(LLVMTypeOf(rhs)) == LLVMPointerTypeKind);

            if (lhs_is_ptr && rhs_is_ptr) {
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                LLVMTypeRef param_types[] = { ptr_type, ptr_type };
                LLVMTypeRef concat_type = LLVMFunctionType(ptr_type, param_types, 2, 0);
                LLVMValueRef concat_func = LLVMGetNamedFunction(module, "ginc_str_concat");
                if (!concat_func) concat_func = LLVMAddFunction(module, "ginc_str_concat", concat_type);
                LLVMValueRef args[] = { lhs, rhs };
                return LLVMBuildCall2(builder, concat_type, concat_func, args, 2, "concattmp");
            }
            
            promote_arithmetic(&lhs, &rhs);
            if (is_fp_type(LLVMTypeOf(lhs))) {
                return LLVMBuildFAdd(builder, lhs, rhs, "faddtmp");
            }
            return LLVMBuildAdd(builder, lhs, rhs, "addtmp");
        }

        case AST_SUB: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            if (!lhs || !rhs) return NULL;
            int const lp = (infer_ptr_target_type(node->left) >= 0 &&
                node->left->type != AST_STR && node->left->type != AST_STRING);
            int const rp = (infer_ptr_target_type(node->right) >= 0 &&
                node->right->type != AST_STR && node->right->type != AST_STRING);
            if (lp && rp) {
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                LLVMTypeRef i64_type = LLVMInt64TypeInContext(context);
                LLVMTypeRef param_types[] = { ptr_type, ptr_type };
                LLVMTypeRef fn_type = LLVMFunctionType(i64_type, param_types, 2, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(module, "ginc_ptr_diff");
                if (!fn) fn = LLVMAddFunction(module, "ginc_ptr_diff", fn_type);
                LLVMValueRef args[] = { lhs, rhs };
                return LLVMBuildCall2(builder, fn_type, fn, args, 2, "ptrdiff");
            }
            if (lp) {
                LLVMValueRef neg_off = LLVMBuildNeg(builder, coerce_value(auto_unwrap(rhs), LLVMInt64TypeInContext(context), 0), "negoff");
                return call_ptr_add(lhs, neg_off);
            }
            promote_arithmetic(&lhs, &rhs);
            if (is_fp_type(LLVMTypeOf(lhs))) {
                return LLVMBuildFSub(builder, lhs, rhs, "fsubtmp");
            }
            return LLVMBuildSub(builder, lhs, rhs, "subtmp");
        }

        case AST_UNARY_MINUS: {
            LLVMValueRef val = auto_unwrap(generate_node(node->left));
            if (is_fp_type(LLVMTypeOf(val))) {
                return LLVMBuildFNeg(builder, val, "fnegtmp");
            }
            return LLVMBuildNeg(builder, val, "negtmp");
        }

        case AST_POST_INC:
        case AST_POST_DEC: {
            char const* const var_name = node->left->var_name;
            if (is_const_var(var_name)) {
                fprintf(stderr, "Ga! (Compile Error): Assignment of read-only variable '%s'\n", var_name);
                exit(1);
            }
            LLVMValueRef var = NULL;
            LLVMTypeRef var_type = NULL;

            for (int i = local_count - 1; i >= 1; i--) {
                if (local_names[i] && strcmp(local_names[i], var_name) == 0) {
                    var = local_vars[i];
                    var_type = local_types[i];
                    break;
                }
            }
            if (!var) {
                var = LLVMGetNamedGlobal(module, var_name);
                if (var) var_type = LLVMGlobalGetValueType(var);
            }
            if (!var || !var_type) {
                fprintf(stderr, "Ga! (Codegen Error): Unknown variable '%s'\n", var_name);
                return NULL;
            }

            LLVMValueRef old_val = LLVMBuildLoad2(builder, var_type, var, "post_old");
            LLVMValueRef new_val = NULL;

            if (get_ptr_target_type(var_name) >= 0) {
                long long const delta = (node->type == AST_POST_INC) ? 1 : -1;
                new_val = call_ptr_add(old_val, LLVMConstInt(LLVMInt64TypeInContext(context), (unsigned long long)delta, 1));
                LLVMBuildStore(builder, new_val, var);
                return old_val;
            }

            if (LLVMGetTypeKind(var_type) == LLVMDoubleTypeKind) {
                LLVMValueRef one = LLVMConstReal(var_type, 1.0);
                if (node->type == AST_POST_INC) {
                    new_val = LLVMBuildFAdd(builder, old_val, one, "finc_tmp");
                } else {
                    new_val = LLVMBuildFSub(builder, old_val, one, "fdec_tmp");
                }
            } else {
                LLVMValueRef old_unwrapped = auto_unwrap(old_val);
                LLVMValueRef one = LLVMConstInt(LLVMInt32TypeInContext(context), 1, 0);
                if (node->type == AST_POST_INC) {
                    new_val = LLVMBuildAdd(builder, old_unwrapped, one, "inc_tmp");
                } else {
                    new_val = LLVMBuildSub(builder, old_unwrapped, one, "dec_tmp");
                }
            }

            LLVMBuildStore(builder, new_val, var);
            return old_val;
        }

        case AST_MUL: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            promote_arithmetic(&lhs, &rhs);
            if (is_fp_type(LLVMTypeOf(lhs))) {
                return LLVMBuildFMul(builder, lhs, rhs, "fmultmp");
            }
            return LLVMBuildMul(builder, lhs, rhs, "multmp");
        }

        case AST_DIV: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            promote_arithmetic(&lhs, &rhs);
            if (is_fp_type(LLVMTypeOf(lhs))) {
                return LLVMBuildFDiv(builder, lhs, rhs, "fdivtmp");
            }
            return LLVMBuildSDiv(builder, lhs, rhs, "divtmp");
        }

        case AST_MOD: {
            LLVMValueRef lhs = auto_unwrap(generate_node(node->left));
            LLVMValueRef rhs = auto_unwrap(generate_node(node->right));
            return LLVMBuildSRem(builder, lhs, rhs, "modtmp");
        }

        case AST_BIT_AND: {
            LLVMValueRef lhs = auto_unwrap(generate_node(node->left));
            LLVMValueRef rhs = auto_unwrap(generate_node(node->right));
            return LLVMBuildAnd(builder, lhs, rhs, "andtmp");
        }

        case AST_BIT_OR: {
            LLVMValueRef lhs = auto_unwrap(generate_node(node->left));
            LLVMValueRef rhs = auto_unwrap(generate_node(node->right));
            return LLVMBuildOr(builder, lhs, rhs, "ortmp");
        }

        case AST_BIT_XOR: {
            LLVMValueRef lhs = auto_unwrap(generate_node(node->left));
            LLVMValueRef rhs = auto_unwrap(generate_node(node->right));
            return LLVMBuildXor(builder, lhs, rhs, "xortmp");
        }

        case AST_LSHIFT: {
            LLVMValueRef lhs = auto_unwrap(generate_node(node->left));
            LLVMValueRef rhs = auto_unwrap(generate_node(node->right));
            return LLVMBuildShl(builder, lhs, rhs, "shltmp");
        }

        case AST_RSHIFT: {
            LLVMValueRef lhs = auto_unwrap(generate_node(node->left));
            LLVMValueRef rhs = auto_unwrap(generate_node(node->right));
            return LLVMBuildAShr(builder, lhs, rhs, "ashrtmp");
        }

        case AST_BIT_NOT: {
            LLVMValueRef val = auto_unwrap(generate_node(node->left));
            return LLVMBuildNot(builder, val, "bitnottmp");
        }

        case AST_TERNARY: {
            LLVMValueRef cond_val = auto_unwrap(generate_node(node->cond));
            if (!cond_val) return NULL;
            LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(cond_val), 0, 0);
            LLVMValueRef cond_i1 = LLVMBuildICmp(builder, LLVMIntNE, cond_val, zero, "terncond");

            LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(context, func, "ternthen");
            LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(context, func, "ternelse");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(context, func, "terncont");

            LLVMBuildCondBr(builder, cond_i1, then_bb, else_bb);

            LLVMPositionBuilderAtEnd(builder, then_bb);
            LLVMValueRef then_val = generate_node(node->then_body);
            LLVMBasicBlockRef then_end_bb = LLVMGetInsertBlock(builder);
            if (!LLVMGetBasicBlockTerminator(then_end_bb)) {
                LLVMBuildBr(builder, merge_bb);
            }

            LLVMPositionBuilderAtEnd(builder, else_bb);
            LLVMValueRef else_val = generate_node(node->else_body);
            LLVMBasicBlockRef else_end_bb = LLVMGetInsertBlock(builder);
            if (!LLVMGetBasicBlockTerminator(else_end_bb)) {
                LLVMBuildBr(builder, merge_bb);
            }

            LLVMPositionBuilderAtEnd(builder, merge_bb);
            if (!then_val || !else_val) return NULL;

            if (LLVMTypeOf(then_val) != LLVMTypeOf(else_val)) {
                then_val = auto_unwrap(then_val);
                else_val = auto_unwrap(else_val);
            }

            LLVMValueRef phi = LLVMBuildPhi(builder, LLVMTypeOf(then_val), "terntmp");
            LLVMValueRef incoming_vals[] = { then_val, else_val };
            LLVMBasicBlockRef incoming_bbs[] = { then_end_bb, else_end_bb };
            LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);
            return phi;
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
                if (infer_ptr_target_type(node->left) == TYPE_CHAR &&
                    node->left->type != AST_STR && node->left->type != AST_STRING) {
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                    LLVMTypeRef cstr_param_types[] = { ptr_type };
                    LLVMTypeRef cstr_fn_type = LLVMFunctionType(ptr_type, cstr_param_types, 1, 0);
                    LLVMValueRef cstr_fn = LLVMGetNamedFunction(module, "ginc_ptr_cstr");
                    if (!cstr_fn) cstr_fn = LLVMAddFunction(module, "ginc_ptr_cstr", cstr_fn_type);
                    LLVMValueRef cargs[] = { arg_val };
                    arg_val = LLVMBuildCall2(builder, cstr_fn_type, cstr_fn, cargs, 1, "cstr");
                }
                if (node->left->type == AST_ARRAY_ACCESS && node->left->left &&
                    (node->left->left->type == AST_STR || node->left->left->type == AST_STRING)) {
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
            char const* fmt_str_val = "%d\n";
            LLVMTypeRef arg_t = LLVMTypeOf(arg_val);
            if (LLVMGetTypeKind(arg_t) == LLVMFloatTypeKind) {
                arg_val = LLVMBuildFPExt(builder, arg_val, LLVMDoubleTypeInContext(context), "fpext");
                fmt_str_val = "%f\n";
            } else if (LLVMGetTypeKind(arg_t) == LLVMDoubleTypeKind) {
                fmt_str_val = "%f\n";
            } else if (LLVMGetTypeKind(arg_t) == LLVMIntegerTypeKind) {
                unsigned const w = LLVMGetIntTypeWidth(arg_t);
                if (w == 64) {
                    fmt_str_val = "%lld\n";
                } else if (w < 32) {
                    arg_val = LLVMBuildSExt(builder, arg_val, LLVMInt32TypeInContext(context), "sext_print");
                }
            }
            LLVMValueRef format_str = LLVMBuildGlobalStringPtr(builder, fmt_str_val, "fmt");
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
        case AST_NEQ:
        case AST_LT:
        case AST_LE:
        case AST_GT:
        case AST_GE: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            if (!lhs || !rhs) return NULL;

            int const lhs_is_ptr = (LLVMGetTypeKind(LLVMTypeOf(lhs)) == LLVMPointerTypeKind);
            int const rhs_is_ptr = (LLVMGetTypeKind(LLVMTypeOf(rhs)) == LLVMPointerTypeKind);

            if (lhs_is_ptr && rhs_is_ptr) {
                LLVMIntPredicate pred;
                switch (node->type) {
                    case AST_EQ:  pred = LLVMIntEQ; break;
                    case AST_NEQ: pred = LLVMIntNE; break;
                    case AST_LT:  pred = LLVMIntULT; break;
                    case AST_LE:  pred = LLVMIntULE; break;
                    case AST_GT:  pred = LLVMIntUGT; break;
                    case AST_GE:  pred = LLVMIntUGE; break;
                    default:      pred = LLVMIntEQ; break;
                }
                LLVMValueRef cmp = LLVMBuildICmp(builder, pred, lhs, rhs, "ptrcmptmp");
                return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(context), "ptrzexttmp");
            }

            if ((lhs_is_ptr && !rhs_is_ptr) || (!lhs_is_ptr && rhs_is_ptr)) {
                ASTNode* ptr_node = lhs_is_ptr ? node->left : node->right;
                LLVMValueRef int_val = lhs_is_ptr ? rhs : lhs;
                if (infer_ptr_target_type(ptr_node) >= 0 ||
                    (LLVMIsAConstantInt(int_val) && LLVMConstIntGetZExtValue(int_val) == 0)) {
                    LLVMTypeRef ptr_t = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                    if (!lhs_is_ptr) lhs = coerce_value(lhs, ptr_t, 0);
                    if (!rhs_is_ptr) rhs = coerce_value(rhs, ptr_t, 0);
                    LLVMIntPredicate pred = (node->type == AST_NEQ) ? LLVMIntNE : LLVMIntEQ;
                    LLVMValueRef cmp = LLVMBuildICmp(builder, pred, lhs, rhs, "ptrnullcmp");
                    return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(context), "ptrnullzext");
                }
            }

            promote_arithmetic(&lhs, &rhs);
            if (is_fp_type(LLVMTypeOf(lhs))) {
                LLVMRealPredicate fpred;
                switch (node->type) {
                    case AST_EQ:  fpred = LLVMRealOEQ; break;
                    case AST_NEQ: fpred = LLVMRealONE; break;
                    case AST_LT:  fpred = LLVMRealOLT; break;
                    case AST_LE:  fpred = LLVMRealOLE; break;
                    case AST_GT:  fpred = LLVMRealOGT; break;
                    case AST_GE:  fpred = LLVMRealOGE; break;
                    default:      fpred = LLVMRealOEQ; break;
                }
                LLVMValueRef fcmp = LLVMBuildFCmp(builder, fpred, lhs, rhs, "fcmptmp");
                return LLVMBuildZExt(builder, fcmp, LLVMInt32TypeInContext(context), "fzexttmp");
            }

            LLVMIntPredicate pred;
            switch (node->type) {
                case AST_EQ:  pred = LLVMIntEQ; break;
                case AST_NEQ: pred = LLVMIntNE; break;
                case AST_LT:  pred = LLVMIntSLT; break;
                case AST_LE:  pred = LLVMIntSLE; break;
                case AST_GT:  pred = LLVMIntSGT; break;
                case AST_GE:  pred = LLVMIntSGE; break;
                default:      pred = LLVMIntEQ; break;
            }
            LLVMValueRef cmp = LLVMBuildICmp(builder, pred, lhs, rhs, "cmptmp");
            return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(context), "zexttmp");
        }

        case AST_LOGICAL_NOT: {
            LLVMValueRef val = auto_unwrap(generate_node(node->left));
            if (!val) return NULL;
            LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0);
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntEQ, val, zero, "nottmp");
            return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(context), "notzext");
        }

        case AST_LOGICAL_AND:
        case AST_LOGICAL_OR: {
            LLVMValueRef lhs = auto_unwrap(generate_node(node->left));
            LLVMValueRef rhs = auto_unwrap(generate_node(node->right));
            if (!lhs || !rhs) return NULL;

            LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0);
            LLVMValueRef lhs_b = LLVMBuildICmp(builder, LLVMIntNE, lhs, zero, "lhs_b");
            LLVMValueRef rhs_b = LLVMBuildICmp(builder, LLVMIntNE, rhs, zero, "rhs_b");

            LLVMValueRef res = (node->type == AST_LOGICAL_AND)
                ? LLVMBuildAnd(builder, lhs_b, rhs_b, "andtmp")
                : LLVMBuildOr(builder, lhs_b, rhs_b, "ortmp");

            return LLVMBuildZExt(builder, res, LLVMInt32TypeInContext(context), "logzext");
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
            if (continue_depth < MAX_CONTROL_DEPTH) {
                continue_target_stack[continue_depth++] = cond_bb;
            }
            if (break_depth < MAX_CONTROL_DEPTH) {
                break_target_stack[break_depth++] = after_bb;
            }
            generate_node(node->then_body);
            if (continue_depth > 0) continue_depth--;
            if (break_depth > 0) break_depth--;

            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildBr(builder, cond_bb);
            }

            // ループ脱出後
            LLVMPositionBuilderAtEnd(builder, after_bb);
            return NULL;
        }

        case AST_DO_WHILE: {
            LLVMValueRef current_function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(context, current_function, "dobody");
            LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(context, current_function, "docond");
            LLVMBasicBlockRef end_block = LLVMAppendBasicBlockInContext(context, current_function, "doend");

            LLVMBuildBr(builder, body_block);
            LLVMPositionBuilderAtEnd(builder, body_block);

            if (continue_depth < MAX_CONTROL_DEPTH) {
                continue_target_stack[continue_depth++] = cond_block;
            }
            if (break_depth < MAX_CONTROL_DEPTH) {
                break_target_stack[break_depth++] = end_block;
            }
            if (node->left) generate_node(node->left);
            if (continue_depth > 0) continue_depth--;
            if (break_depth > 0) break_depth--;

            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildBr(builder, cond_block);
            }

            LLVMPositionBuilderAtEnd(builder, cond_block);
            LLVMValueRef cond = auto_unwrap(generate_node(node->right));
            LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(cond), 0, 0);
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntNE, cond, zero, "docmp");
            LLVMBuildCondBr(builder, cmp, body_block, end_block);

            LLVMPositionBuilderAtEnd(builder, end_block);
            return NULL;
        }

        case AST_BREAK:
            if (break_depth > 0) {
                LLVMBuildBr(builder, break_target_stack[break_depth - 1]);
            }
            return NULL;

        case AST_CONTINUE:
            if (continue_depth > 0) {
                LLVMBuildBr(builder, continue_target_stack[continue_depth - 1]);
            }
            return NULL;

        case AST_SWITCH: {
            LLVMValueRef cond_val = auto_unwrap(generate_node(node->left));
            if (!cond_val) return NULL;

            LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(context, func, "switchend");
            LLVMBasicBlockRef default_bb = end_bb;

            int total_items = 0;
            int case_count = 0;
            for (ASTNode* cur = node->right; cur != NULL; cur = cur->next) {
                total_items++;
                if (cur->type == AST_CASE) {
                    case_count++;
                }
            }

            ASTNode** items = NULL;
            LLVMBasicBlockRef* blocks = NULL;
            if (total_items > 0) {
                items = malloc(sizeof(ASTNode*) * (size_t)total_items);
                blocks = malloc(sizeof(LLVMBasicBlockRef) * (size_t)total_items);
            }

            int idx = 0;
            for (ASTNode* cur = node->right; cur != NULL; cur = cur->next) {
                items[idx] = cur;
                if (cur->type == AST_CASE) {
                    blocks[idx] = LLVMAppendBasicBlockInContext(context, func, "swcase");
                } else {
                    blocks[idx] = LLVMAppendBasicBlockInContext(context, func, "swdefault");
                    default_bb = blocks[idx];
                }
                idx++;
            }

            LLVMValueRef sw = LLVMBuildSwitch(builder, cond_val, default_bb, (unsigned)case_count);
            LLVMTypeRef i32_type = LLVMInt32TypeInContext(context);
            for (int i = 0; i < total_items; i++) {
                if (items[i]->type == AST_CASE) {
                    int cval = 0;
                    if (items[i]->left->type == AST_VAR) {
                        if (!lookup_enum(items[i]->left->var_name, &cval)) {
                            fprintf(stderr, "Ga! (Compile Error): Case label '%s' is not an integer constant\n", items[i]->left->var_name);
                            exit(1);
                        }
                    } else {
                        cval = items[i]->left->val;
                    }
                    LLVMValueRef case_val = LLVMConstInt(i32_type, (unsigned long long)cval, 1);
                    LLVMAddCase(sw, case_val, blocks[i]);
                }
            }

            if (break_depth < MAX_CONTROL_DEPTH) {
                break_target_stack[break_depth++] = end_bb;
            }
            for (int i = 0; i < total_items; i++) {
                LLVMPositionBuilderAtEnd(builder, blocks[i]);
                ASTNode* body = (items[i]->type == AST_CASE) ? items[i]->right : items[i]->left;
                if (body) {
                    generate_node(body);
                }
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                    LLVMBasicBlockRef next_bb = (i + 1 < total_items) ? blocks[i + 1] : end_bb;
                    LLVMBuildBr(builder, next_bb);
                }
            }
            if (break_depth > 0) {
                break_depth--;
            }

            free(items);
            free(blocks);

            LLVMPositionBuilderAtEnd(builder, end_bb);
            return NULL;
        }

        case AST_CASE:
        case AST_DEFAULT:
            return NULL;

        case AST_FOR: {
            LLVMValueRef current_function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            if (node->left) generate_node(node->left); // init

            LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(context, current_function, "forcond");
            LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(context, current_function, "forbody");
            LLVMBasicBlockRef inc_block = LLVMAppendBasicBlockInContext(context, current_function, "forinc");
            LLVMBasicBlockRef end_block = LLVMAppendBasicBlockInContext(context, current_function, "forend");

            LLVMBuildBr(builder, cond_block);

            // 条件判定ブロック
            LLVMPositionBuilderAtEnd(builder, cond_block);
            LLVMValueRef cond;
            if (node->cond) {
                cond = auto_unwrap(generate_node(node->cond));
            } else {
                cond = LLVMConstInt(LLVMInt32TypeInContext(context), 1, 0);
            }
            LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(cond), 0, 0);
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntNE, cond, zero, "loopcond");
            LLVMBuildCondBr(builder, cmp, body_block, end_block);

            // 本体ブロック
            LLVMPositionBuilderAtEnd(builder, body_block);
            if (continue_depth < MAX_CONTROL_DEPTH) {
                continue_target_stack[continue_depth++] = inc_block;
            }
            if (break_depth < MAX_CONTROL_DEPTH) {
                break_target_stack[break_depth++] = end_block;
            }
            generate_node(node->then_branch);
            if (continue_depth > 0) continue_depth--;
            if (break_depth > 0) break_depth--;

            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                LLVMBuildBr(builder, inc_block);
            }

            // インクリメントブロック
            LLVMPositionBuilderAtEnd(builder, inc_block);
            if (node->right) generate_node(node->right); // inc
            LLVMBuildBr(builder, cond_block);

            // 終了ブロック
            LLVMPositionBuilderAtEnd(builder, end_block);
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

            char const* target_class = get_class_of_var(obj_name);
            if (target_class) {
                check_access(target_class, method_name);
            }

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

            int arg_count = 0;
            ASTNode* curr_arg = node->left;
            while (curr_arg) { arg_count++; curr_arg = curr_arg->right; }

            LLVMTypeRef* arg_types = malloc(sizeof(LLVMTypeRef) * (arg_count + 1));
            LLVMValueRef* args = malloc(sizeof(LLVMValueRef) * (arg_count + 1));

            args[0] = ht_ptr;
            arg_types[0] = ptr_type;

            curr_arg = node->left;
            int i = 1;
            while (curr_arg) {
                args[i] = generate_node(curr_arg->left);
                arg_types[i] = LLVMTypeOf(args[i]);
                curr_arg = curr_arg->right; i++;
            }

            LLVMTypeRef method_type = LLVMFunctionType(LLVMInt32TypeInContext(context), arg_types, arg_count + 1, 0);
            LLVMValueRef call_res = LLVMBuildCall2(builder, method_type, raw_func, args, arg_count + 1, "method_res");
            free(arg_types);
            free(args);
            return call_res;
        }

        case AST_RETURN: {
            LLVMValueRef ret_val = generate_node(node->left);
            LLVMValueRef cur_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMTypeRef ret_type = LLVMGetReturnType(LLVMGlobalGetValueType(cur_func));
            ret_val = coerce_value(ret_val, ret_type, 0);
            return LLVMBuildRet(builder, ret_val);
        }

        case AST_RETURN_VOID: {
            return LLVMBuildRetVoid(builder);
        }

        case AST_FUNC_CALL: {
            int arg_count = 0;
            ASTNode* arg_curr = node->left;
            while (arg_curr) { arg_count++; arg_curr = arg_curr->right; }

            for (int i = 0; i < func_meta_count; i++) {
                if (strcmp(func_meta[i].func_name, node->var_name) == 0) {
                    if ((!func_meta[i].is_variadic && func_meta[i].param_count != arg_count) ||
                        (func_meta[i].is_variadic && arg_count < func_meta[i].param_count)) {
                        fprintf(stderr, "Ga! (Compile Error): Function '%s' expects %s%d arguments, but got %d\n", 
                                node->var_name, func_meta[i].is_variadic ? "at least " : "", func_meta[i].param_count, arg_count);
                        exit(1);
                    }

                    arg_curr = node->left;
                    int const check_count = (arg_count < func_meta[i].param_count) ? arg_count : func_meta[i].param_count;
                    for (int p = 0; p < check_count; p++) {
                        int expected_type = func_meta[i].param_types[p];
                        if (expected_type != TYPE_VAR && expected_type != TYPE_FILE) {
                            int actual_node_type = arg_curr->left->type;
                            int is_type_mismatch = 0;

                            if (expected_type == TYPE_INT && (actual_node_type == AST_STR || actual_node_type == AST_STRING || actual_node_type == AST_FLOAT)) is_type_mismatch = 1;
                            if (expected_type == TYPE_STR && (actual_node_type == AST_NUM || actual_node_type == AST_FLOAT)) is_type_mismatch = 1;
                            if (expected_type == TYPE_DOUBLE && (actual_node_type == AST_STR || actual_node_type == AST_STRING)) is_type_mismatch = 1;

                            if (is_type_mismatch) {
                                fprintf(stderr, "Ga! (Compile Error): Type mismatch for argument %d of function '%s'\n", 
                                        p + 1, node->var_name);
                                exit(1);
                            }
                        }
                        arg_curr = arg_curr->right;
                    }
                    break;
                }
            }

            LLVMValueRef func = LLVMGetNamedFunction(module, node->var_name);
            LLVMTypeRef func_type = NULL;
            if (func) {
                // ★超重要: Opaque Pointer環境ではLLVMGlobalGetValueTypeで関数の実際の型を取得する
                func_type = LLVMGlobalGetValueType(func);
            } else {
                /* 関数ポインタ変数からの呼び出しを確認 */
                LLVMValueRef fp_var = NULL;
                for (int i = local_count - 1; i >= 1; i--) {
                    if (local_names[i] && strcmp(local_names[i], node->var_name) == 0 && local_fn_types[i]) {
                        fp_var = local_vars[i];
                        func_type = local_fn_types[i];
                        break;
                    }
                }
                if (!fp_var) {
                    for (int i = 0; i < global_var_count; i++) {
                        if (global_var_names[i] && strcmp(global_var_names[i], node->var_name) == 0 && global_fn_types[i]) {
                            fp_var = LLVMGetNamedGlobal(module, node->var_name);
                            func_type = global_fn_types[i];
                            break;
                        }
                    }
                }
                if (fp_var && func_type) {
                    LLVMTypeRef ptr_t = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
                    func = LLVMBuildLoad2(builder, ptr_t, fp_var, "fp_load");
                }
            }
            if (!func || !func_type) {
                fprintf(stderr, "Ga! (Codegen Error): Unknown function '%s'\n", node->var_name);
                return NULL;
            }

            LLVMValueRef* args = NULL;
            if (arg_count > 0) {
                unsigned const expected_params = LLVMCountParamTypes(func_type);
                LLVMTypeRef* expected_types = NULL;
                if (expected_params > 0) {
                    expected_types = malloc(sizeof(LLVMTypeRef) * expected_params);
                    LLVMGetParamTypes(func_type, expected_types);
                }
                args = malloc(sizeof(LLVMValueRef) * arg_count);
                ASTNode* curr_arg = node->left;
                int i = 0;
                while (curr_arg) {
                    LLVMValueRef aval = generate_node(curr_arg->left);
                    if (expected_types && (unsigned)i < expected_params) {
                        if (LLVMGetTypeKind(expected_types[i]) == LLVMPointerTypeKind &&
                            (curr_arg->left->type == AST_STRING || curr_arg->left->type == AST_STR)) {
                            /* char* 仮引数に文字列リテラルを渡す場合はファットポインタ化 */
                            int is_ptr_param = 0;
                            for (int fm = 0; fm < func_meta_count; fm++) {
                                if (strcmp(func_meta[fm].func_name, node->var_name) == 0 && i < func_meta[fm].param_count) {
                                    if (func_meta[fm].param_types[i] == TYPE_PTR) is_ptr_param = 1;
                                    break;
                                }
                            }
                            if (is_ptr_param) {
                                aval = call_ptr_make_cstr(aval);
                            }
                        } else {
                            aval = coerce_value(aval, expected_types[i], 0);
                        }
                    } else if ((unsigned)i >= expected_params) {
                        /* C デフォルト引数昇格 (float -> double, char/short -> int) */
                        LLVMTypeRef at = LLVMTypeOf(aval);
                        if (LLVMGetTypeKind(at) == LLVMFloatTypeKind) {
                            aval = LLVMBuildFPExt(builder, aval, LLVMDoubleTypeInContext(context), "va_fpext");
                        } else if (LLVMGetTypeKind(at) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(at) < 32) {
                            aval = LLVMBuildSExt(builder, aval, LLVMInt32TypeInContext(context), "va_sext");
                        }
                    }
                    args[i] = aval;
                    curr_arg = curr_arg->right; i++;
                }
                if (expected_types) free(expected_types);
            }
            LLVMTypeRef ret_type = LLVMGetReturnType(func_type);
            char const* const call_name = (LLVMGetTypeKind(ret_type) == LLVMVoidTypeKind) ? "" : "calltmp";
            LLVMValueRef call_res = LLVMBuildCall2(builder, func_type, func, args, arg_count, call_name);
            if (args) free(args);
            return call_res;
        }

        case AST_FUNC_DECL: {
            char old_class[256];
            strcpy(old_class, current_codegen_class);
            if (node->class_name) {
                strcpy(current_codegen_class, node->class_name);
            }

            int param_count = 0;
            int is_variadic = 0;
            ASTNode* curr_param = node->left; // params (AST_EXPR_LIST)
            while (curr_param) {
                if (curr_param->left && curr_param->left->is_variadic) {
                    is_variadic = 1;
                    break;
                }
                param_count++;
                curr_param = curr_param->right;
            }

            char const* const mname = node->str_val ? node->str_val : node->var_name;
            int found_meta = 0;
            for (int i = 0; i < func_meta_count; i++) {
                if (strcmp(func_meta[i].func_name, mname) == 0) {
                    found_meta = 1;
                    break;
                }
            }
            if (!found_meta) {
                FuncMeta meta;
                meta.func_name = strdup(mname);
                meta.param_count = param_count;
                meta.is_variadic = is_variadic;
                ASTNode* cp_meta = node->left;
                for (int i = 0; i < param_count; i++) {
                    meta.param_types[i] = cp_meta->left->var_type;
                    cp_meta = cp_meta->right;
                }
                func_meta[func_meta_count++] = meta;
            }

            LLVMTypeRef* param_types = NULL;
            if (param_count > 0) {
                param_types = malloc(sizeof(LLVMTypeRef) * param_count);
                ASTNode* cp = node->left;
                for (int i = 0; i < param_count; i++) {
                    param_types[i] = ginc_type_to_llvm(cp->left->var_type);
                    cp = cp->right;
                }
            }

            LLVMTypeRef ret_llvm_type = ginc_type_to_llvm(node->var_type);

            LLVMTypeRef func_type = LLVMFunctionType(ret_llvm_type, param_types, param_count, is_variadic);
            LLVMValueRef func = LLVMGetNamedFunction(module, node->var_name);
            if (!func) {
                func = LLVMAddFunction(module, node->var_name, func_type);
            }

            if (!node->body) {
                if (param_types) free(param_types);
                strcpy(current_codegen_class, old_class);
                return NULL;
            }

            LLVMBasicBlockRef old_bb = LLVMGetInsertBlock(builder);
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, func, "entry");
            LLVMPositionBuilderAtEnd(builder, entry);

            int old_local_count = local_count;
            local_count = 1; // ローカル変数は1から開始

            int old_label_count = label_count;
            char* saved_label_names[MAX_LABELS];
            LLVMBasicBlockRef saved_label_bbs[MAX_LABELS];
            for (int li = 0; li < old_label_count; li++) {
                saved_label_names[li] = label_names[li];
                saved_label_bbs[li] = label_bbs[li];
            }
            label_count = 0;

            curr_param = node->left;
            int p_idx = 0;
            while (curr_param && p_idx < param_count) {
                char* p_name = curr_param->left->var_name;
                LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, param_types[p_idx], p_name);
                local_names[local_count] = strdup(p_name);
                local_vars[local_count] = alloca_inst;
                local_types[local_count] = param_types[p_idx];
                local_is_const[local_count] = 0;
                local_ptr_targets[local_count] = (curr_param->left->var_type == TYPE_PTR) ? (int)curr_param->left->ptr_target_type : -1;
                local_fn_types[local_count] = curr_param->left->is_func_ptr ? build_llvm_func_type(curr_param->left->fn_ret_type, curr_param->left->left) : NULL;
                if (p_idx == 0 && strcmp(p_name, "this") == 0 && node->class_name) {
                    local_classes[local_count] = strdup(node->class_name);
                } else {
                    local_classes[local_count] = NULL;
                }
                local_count++;

                LLVMValueRef arg_val = LLVMGetParam(func, p_idx);
                LLVMBuildStore(builder, arg_val, alloca_inst);

                curr_param = curr_param->right; p_idx++;
            }

            if (param_types) free(param_types);

            generate_node(node->body); // 関数本体の評価

            // ターミネータ(return等)が無ければダミーのreturnを追加して検証エラーを防ぐ
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                if (node->var_type == TYPE_VOID) {
                    LLVMBuildRetVoid(builder);
                } else if (node->var_type == TYPE_DOUBLE) {
                    LLVMBuildRet(builder, LLVMConstReal(LLVMDoubleTypeInContext(context), 0.0));
                } else if (node->var_type == TYPE_STR || node->var_type == TYPE_FILE || node->var_type == TYPE_VAR) {
                    LLVMBuildRet(builder, LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(context), 0)));
                } else {
                    LLVMBuildRet(builder, LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0));
                }
            }

            local_count = old_local_count;
            for (int li = 0; li < label_count; li++) {
                if (label_names[li]) free(label_names[li]);
            }
            label_count = old_label_count;
            for (int li = 0; li < old_label_count; li++) {
                label_names[li] = saved_label_names[li];
                label_bbs[li] = saved_label_bbs[li];
            }
            strcpy(current_codegen_class, old_class);
            LLVMPositionBuilderAtEnd(builder, old_bb);
            return NULL;
        }

        case AST_LAMBDA: {
            char lname[64];
            snprintf(lname, sizeof(lname), "__lambda_%d", lambda_counter++);

            int pcnt = 0;
            for (ASTNode* cp = node->left; cp != NULL; cp = cp->right) pcnt++;
            LLVMTypeRef* ptypes = NULL;
            if (pcnt > 0) {
                ptypes = malloc(sizeof(LLVMTypeRef) * (size_t)pcnt);
                ASTNode* cp = node->left;
                for (int i = 0; i < pcnt; i++) {
                    ptypes[i] = ginc_type_to_llvm(cp->left->var_type);
                    cp = cp->right;
                }
            }
            LLVMTypeRef ret_t = ginc_type_to_llvm(node->var_type);
            LLVMTypeRef fn_type = LLVMFunctionType(ret_t, ptypes, (unsigned)pcnt, 0);
            LLVMValueRef fn = LLVMAddFunction(module, lname, fn_type);

            LLVMBasicBlockRef old_bb = LLVMGetInsertBlock(builder);
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, fn, "entry");
            LLVMPositionBuilderAtEnd(builder, entry);

            int old_local_count = local_count;
            local_count = 1;
            int old_label_count = label_count;
            char* saved_label_names[MAX_LABELS];
            LLVMBasicBlockRef saved_label_bbs[MAX_LABELS];
            for (int li = 0; li < old_label_count; li++) {
                saved_label_names[li] = label_names[li];
                saved_label_bbs[li] = label_bbs[li];
            }
            label_count = 0;

            ASTNode* cp = node->left;
            for (int i = 0; i < pcnt; i++) {
                char* pname = cp->left->var_name;
                LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, ptypes[i], pname);
                local_names[local_count] = strdup(pname);
                local_vars[local_count] = alloca_inst;
                local_types[local_count] = ptypes[i];
                local_classes[local_count] = NULL;
                local_is_const[local_count] = 0;
                local_ptr_targets[local_count] = (cp->left->var_type == TYPE_PTR) ? (int)cp->left->ptr_target_type : -1;
                local_fn_types[local_count] = NULL;
                local_count++;
                LLVMBuildStore(builder, LLVMGetParam(fn, (unsigned)i), alloca_inst);
                cp = cp->right;
            }
            if (ptypes) free(ptypes);

            generate_node(node->body);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                if (node->var_type == TYPE_VOID) {
                    LLVMBuildRetVoid(builder);
                } else {
                    LLVMBuildRet(builder, LLVMConstNull(ret_t));
                }
            }

            local_count = old_local_count;
            for (int li = 0; li < label_count; li++) {
                if (label_names[li]) free(label_names[li]);
            }
            label_count = old_label_count;
            for (int li = 0; li < old_label_count; li++) {
                label_names[li] = saved_label_names[li];
                label_bbs[li] = saved_label_bbs[li];
            }
            LLVMPositionBuilderAtEnd(builder, old_bb);
            return fn;
        }
    }
    return NULL;
}

static void reset_codegen_state(void) {
    local_count = 0;
    global_var_count = 0;
    class_meta_count = 0;
    func_meta_count = 0;
    enum_count = 0;
    break_depth = 0;
    continue_depth = 0;
    label_count = 0;
    lambda_counter = 0;
    current_codegen_class[0] = '\0';
}

int generate_code_ex(ASTNode* root, int verbose, int* out_exit_code) {
    reset_codegen_state();
    context = LLVMContextCreate();
    module = LLVMModuleCreateWithNameInContext("ginc_module", context);
    builder = LLVMCreateBuilderInContext(context);

    LLVMTypeRef main_func_type = LLVMFunctionType(LLVMInt32TypeInContext(context), NULL, 0, 0);
    LLVMValueRef main_func = LLVMAddFunction(module, "__ginc_main", main_func_type);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, main_func, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    generate_node(root);

    LLVMValueRef user_main = LLVMGetNamedFunction(module, "main");
    if (user_main && LLVMIsAFunction(user_main)) {
        LLVMTypeRef um_type = LLVMGlobalGetValueType(user_main);
        unsigned const pcnt = LLVMCountParamTypes(um_type);
        LLVMTypeRef ret_t = LLVMGetReturnType(um_type);
        LLVMValueRef main_res = NULL;
        if (pcnt == 0) {
            char const* const call_name = (LLVMGetTypeKind(ret_t) == LLVMVoidTypeKind) ? "" : "main_ret";
            main_res = LLVMBuildCall2(builder, um_type, user_main, NULL, 0, call_name);
        } else if (pcnt == 2) {
            LLVMTypeRef i32_t = LLVMInt32TypeInContext(context);
            LLVMTypeRef ptr_t = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            LLVMTypeRef get_argc_t = LLVMFunctionType(i32_t, NULL, 0, 0);
            LLVMValueRef get_argc_fn = LLVMGetNamedFunction(module, "ginc_get_argc");
            if (!get_argc_fn) get_argc_fn = LLVMAddFunction(module, "ginc_get_argc", get_argc_t);
            LLVMValueRef argc_val = LLVMBuildCall2(builder, get_argc_t, get_argc_fn, NULL, 0, "argc");

            LLVMTypeRef get_argv_t = LLVMFunctionType(ptr_t, NULL, 0, 0);
            LLVMValueRef get_argv_fn = LLVMGetNamedFunction(module, "ginc_get_argv");
            if (!get_argv_fn) get_argv_fn = LLVMAddFunction(module, "ginc_get_argv", get_argv_t);
            LLVMValueRef argv_val = LLVMBuildCall2(builder, get_argv_t, get_argv_fn, NULL, 0, "argv");

            LLVMValueRef margs[] = { argc_val, argv_val };
            char const* const call_name = (LLVMGetTypeKind(ret_t) == LLVMVoidTypeKind) ? "" : "main_ret";
            main_res = LLVMBuildCall2(builder, um_type, user_main, margs, 2, call_name);
        }
        if (main_res && LLVMGetTypeKind(ret_t) != LLVMVoidTypeKind) {
            LLVMBuildRet(builder, coerce_value(main_res, LLVMInt32TypeInContext(context), 0));
        } else {
            LLVMBuildRet(builder, LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0));
        }
    } else {
        LLVMBuildRet(builder, LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0));
    }

    char *error = NULL;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "Ga! (LLVM Verify Error): %s\n", error);
        LLVMDisposeMessage(error);
        LLVMContextDispose(context);
        return 1;
    }

    if (verbose) {
        printf("\n--- LLVM IR Dump ---\n");
        char *ir_str = LLVMPrintModuleToString(module);
        if (ir_str) {
            printf("%s", ir_str);
            LLVMDisposeMessage(ir_str);
        }
        fflush(stdout);
    }

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

    void* ginc_make_null(void);
    LLVMAddSymbol("ginc_make_null", (void*)ginc_make_null);

    void* ginc_make_int(int v);
    LLVMAddSymbol("ginc_make_int", (void*)ginc_make_int);

    void* ginc_make_double(double v);
    LLVMAddSymbol("ginc_make_double", (void*)ginc_make_double);

    double ginc_unwrap_double(void* val);
    LLVMAddSymbol("ginc_unwrap_double", (void*)ginc_unwrap_double);

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

    /* モジュール内の外部宣言関数を動的ロード済みライブラリから解決 */
    void* ginc_resolve_symbol(char const* name);
    for (LLVMValueRef fn = LLVMGetFirstFunction(module); fn != NULL; fn = LLVMGetNextFunction(fn)) {
        if (LLVMIsDeclaration(fn)) {
            char const* const fname = LLVMGetValueName(fn);
            if (fname && *fname) {
                void* sym = ginc_resolve_symbol(fname);
                if (sym) {
                    LLVMAddSymbol(fname, sym);
                }
            }
        }
    }

    LLVMExecutionEngineRef engine;
    LLVMLinkInMCJIT();
    
    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        fprintf(stderr, "Ga! (JIT Error): %s\n", error);
        LLVMDisposeMessage(error);
        LLVMContextDispose(context);
        return 1;
    }

    uint64_t func_addr = LLVMGetFunctionAddress(engine, "__ginc_main");
    if (func_addr == 0) {
        fprintf(stderr, "Ga! (JIT Error): '__ginc_main' function address not found.\n");
        LLVMDisposeExecutionEngine(engine);
        LLVMContextDispose(context);
        return 1;
    }

    int (*main_fp)(void) = (int (*)(void))(intptr_t)func_addr;
    int res_val = main_fp();

    if (out_exit_code) *out_exit_code = res_val;
    if (verbose) {
        printf("\n=> JIT Execution Result: %d\n", res_val);
    }

    LLVMDisposeExecutionEngine(engine);
    LLVMContextDispose(context);
    return 0;
}

void generate_code(ASTNode* root) {
    generate_code_ex(root, 1, NULL);
}
