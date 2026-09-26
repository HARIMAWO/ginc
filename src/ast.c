#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

ASTNode* make_num_node(int val) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_NUM;
	node->val = val;
	return node;
}

ASTNode* make_str_node(char* str_val) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_STR;
	node->var_name = str_val;
	return node;
}

ASTNode* make_string_node(char* str) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_STRING;
	node->str_val = str;
	return node;
}

ASTNode* make_regex_node(char* pattern) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_REGEX;
	node->str_val = pattern;
	return node;
}

ASTNode* make_var_node(char* name) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_VAR;
	node->var_name = name;
	return node;
}

ASTNode* make_vardecl_node(GincType type, char* var_name, ASTNode* init_expr) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_VARDECL;
	node->var_type = type;
	node->var_name = var_name;
	node->right = init_expr;
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

ASTNode* make_if_node(ASTNode* cond, ASTNode* then_body, ASTNode* else_body) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_IF;
	node->cond = cond;
	node->then_body = then_body;
	node->else_body = else_body;
	return node;
}

ASTNode* make_while_node(ASTNode* cond, ASTNode* body) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_WHILE;
	node->cond = cond;
	node->then_body = body;
	return node;
}

ASTNode* make_block_node(ASTNode* stmt_list) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_BLOCK;
	node->left = stmt_list;
	return node;
}

ASTNode* make_array_decl_node(GincType type, char* name, int max_size, ASTNode* init_list) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_ARRAY_DECL;
	node->var_type = type;
	node->var_name = name;
	node->val = max_size;
	node->right = init_list;
	return node;
}

ASTNode* make_array_access_node(char* name, ASTNode* index) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_ARRAY_ACCESS;
	node->var_name = name;
	node->left = index;
	return node;
}

ASTNode* make_expr_list_node(ASTNode* expr, ASTNode* next) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_EXPR_LIST;
	node->left = expr;
	node->right = next;
	return node;
}

// ASTの構造をツリー状に表示するデバッグ関数
void print_ast(ASTNode* node, int depth) {
	if (!node) return;
	for (int i = 0; i < depth; i++) printf("  ");
	
	switch (node->type) {
		case AST_NUM:    printf("NUM(%d)\n", node->val); break;
		case AST_STR:    printf("STR(\"%s\")\n", node->var_name); break;
		case AST_STRING: printf("STR(\"%s\")\n", node->str_val); break;
		case AST_REGEX:  printf("REGEX(/%s/)\n", node->str_val); break;
		case AST_VAR:    printf("VAR(%s)\n", node->var_name); break;
		case AST_VARDECL:
			if (node->var_type == TYPE_INT) {
				printf("INT VARDECL(%s)\n", node->var_name);
			} else {
				printf("STR VARDECL(%s)\n", node->var_name);
			}
			break;
		case AST_ASSIGN: printf("=\n"); break;
		case AST_ADD:    printf("+\n"); break;
		case AST_SUB:    printf("-\n"); break;
		case AST_SEQ:    printf("SEQ\n"); break;
		case AST_PRINT:  printf("PRINT\n"); break;
		case AST_BLOCK:  printf("BLOCK\n"); break;
		case AST_IF:     printf("IF\n"); break;
		case AST_WHILE:  printf("WHILE\n"); break;
		case AST_EQ:     printf("==\n"); break;
		case AST_LT:     printf("<\n"); break;
		case AST_MATCH:  printf("=~\n"); break;
		case AST_ARRAY_DECL: printf("ARRAY_DECL(%s[%d])\n", node->var_name, node->val); break;
		case AST_ARRAY_ACCESS: printf("ARRAY_ACCESS(%s)\n", node->var_name); break;
		case AST_EXPR_LIST: printf("EXPR_LIST\n"); break;
	}
	
	// 子ノードの再帰表示
	if (node->type == AST_SEQ || node->type == AST_ASSIGN || node->type == AST_ADD || node->type == AST_SUB || node->type == AST_EQ || node->type == AST_LT || node->type == AST_MATCH) {
		print_ast(node->left, depth + 1);
		print_ast(node->right, depth + 1);
	} else if (node->type == AST_VARDECL) {
		print_ast(node->right, depth + 1);
	} else if (node->type == AST_PRINT || node->type == AST_BLOCK) {
		print_ast(node->left, depth + 1);
	} else if (node->type == AST_IF) {
		print_ast(node->cond, depth + 1);
		print_ast(node->then_body, depth + 1);
		if (node->else_body) {
			print_ast(node->else_body, depth + 1);
		}
	} else if (node->type == AST_WHILE) {
		print_ast(node->cond, depth + 1);
		print_ast(node->then_body, depth + 1);
	} else if (node->type == AST_ARRAY_DECL) {
		print_ast(node->right, depth + 1);
	} else if (node->type == AST_ARRAY_ACCESS || node->type == AST_EXPR_LIST) {
		print_ast(node->left, depth + 1);
		if (node->right) {
			print_ast(node->right, depth + 1);
		}
	}
}
