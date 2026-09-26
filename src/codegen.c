#include "codegen.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h> // ★ uint64_t, intptr_t 用に追加

static LLVMContextRef context;
static LLVMModuleRef module;
static LLVMBuilderRef builder;

LLVMValueRef generate_node(ASTNode* node) {
    if (!node) return NULL;

    switch (node->type) {
        case AST_NUM:
            return LLVMConstInt(LLVMInt32TypeInContext(context), node->val, 0);

        case AST_VAR: {
            LLVMValueRef var = LLVMGetNamedGlobal(module, node->var_name);
            if (!var) {
                fprintf(stderr, "Ga! (Codegen Error): Unknown variable '%s'\n", node->var_name);
                return NULL;
            }
            return LLVMBuildLoad2(builder, LLVMInt32TypeInContext(context), var, node->var_name);
        }

        case AST_ASSIGN: {
            LLVMValueRef rhs = generate_node(node->right);
            if (!rhs) return NULL;

            if (node->left->type != AST_VAR) {
                fprintf(stderr, "Ga! (Codegen Error): LHS of assignment must be a variable\n");
                return NULL;
            }

            char* var_name = node->left->var_name;
            LLVMValueRef var = LLVMGetNamedGlobal(module, var_name);
            
            if (!var) {
                var = LLVMAddGlobal(module, LLVMInt32TypeInContext(context), var_name);
                LLVMSetInitializer(var, LLVMConstNull(LLVMInt32TypeInContext(context)));
            }

            LLVMBuildStore(builder, rhs, var);
            return rhs;
        }

        case AST_ADD: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            return LLVMBuildAdd(builder, lhs, rhs, "addtmp");
        }

        case AST_SUB: {
            LLVMValueRef lhs = generate_node(node->left);
            LLVMValueRef rhs = generate_node(node->right);
            return LLVMBuildSub(builder, lhs, rhs, "subtmp");
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
    // ★ LLVMPrintMessageAction から LLVMReturnStatusAction に変更 (安全のため)
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "Ga! (LLVM Verify Error): %s\n", error);
        LLVMDisposeMessage(error);
        return; // クラッシュを防ぐためここで停止
    }

    printf("\n--- LLVM IR Dump ---\n");
    char *ir_str = LLVMPrintModuleToString(module);
    if (ir_str) {
        printf("%s", ir_str);
        LLVMDisposeMessage(ir_str);
    }
    fflush(stdout); // ★ バッファに溜まったIRを確実に画面に出力させる

    // ==========================================
    // JIT (Just-In-Time) コンパイルと実行
    // ==========================================

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    LLVMExecutionEngineRef engine;
    LLVMLinkInMCJIT();
    
    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        fprintf(stderr, "Ga! (JIT Error): %s\n", error);
        LLVMDisposeMessage(error);
        return;
    }

    // ★ LLVMRunFunctionを廃止し、関数ポインタ(アドレス)を取得してC言語から直接呼ぶ
    uint64_t func_addr = LLVMGetFunctionAddress(engine, "main");
    if (func_addr == 0) {
        fprintf(stderr, "Ga! (JIT Error): 'main' function address not found.\n");
        return;
    }

    // メモリ上に展開された機械語を、C言語の関数としてキャスト
    int (*main_fp)(void) = (int (*)(void))(intptr_t)func_addr;
    
    // ネイティブ実行！
    int res_val = main_fp();

    printf("\n=> JIT Execution Result: %d\n", res_val);

    // ==========================================
    // クリーンアップ
    // ==========================================
    LLVMDisposeExecutionEngine(engine); // moduleも自動的に破棄される
    LLVMContextDispose(context);
}