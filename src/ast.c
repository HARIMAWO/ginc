#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

ASTNode* make_num_node(int val) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = AST_NUM;
    node->val = val;
    return node;
}

ASTNode* make_var_node(char* name) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = AST_VAR;
    node->var_name = name;
    return node;
}

ASTNode* make_binop_node(ASTNodeType type, ASTNode* left, ASTNode* right) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = type;
    node->left = left;
    node->right = right;
    return node;
}

ASTNode* make_print_node(ASTNode* arg) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = AST_PRINT;
    node->left = arg;
    node->right = NULL;
    return node;
}

// ASTの構造をツリー状に表示するデバッグ関数
void print_ast(ASTNode* node, int depth) {
    if (!node) return;
    for (int i = 0; i < depth; i++) printf("  ");
    
    switch (node->type) {
        case AST_NUM:    printf("NUM(%d)\n", node->val); break;
        case AST_VAR:    printf("VAR(%s)\n", node->var_name); break;
        case AST_ASSIGN: printf("=\n"); break;
        case AST_ADD:    printf("+\n"); break;
        case AST_SUB:    printf("-\n"); break;
        case AST_SEQ:    printf("SEQ\n"); break;
        case AST_PRINT:  printf("PRINT\n"); break;
    }
    
    // 子ノードの再帰表示
    if (node->type == AST_SEQ || node->type == AST_ASSIGN || node->type == AST_ADD || node->type == AST_SUB) {
        print_ast(node->left, depth + 1);
        print_ast(node->right, depth + 1);
    } else if (node->type == AST_PRINT) {
        print_ast(node->left, depth + 1);
    }
}
