#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

ASTNode* make_unary_node(ASTNodeType type, ASTNode* expr) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = type;
	node->left = expr;
	node->right = NULL;
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

ASTNode* make_func_decl_node(GincType ret_type, char* name, ASTNode* params, ASTNode* body) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_FUNC_DECL;
	node->var_type = ret_type;
	node->var_name = name;
	node->left = params;
	node->body = body;
	return node;
}

ASTNode* make_func_call_node(char* name, ASTNode* args) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_FUNC_CALL;
	node->var_name = name;
	node->left = args;
	return node;
}

ASTNode* make_return_node(ASTNode* expr) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_RETURN;
	node->left = expr;
	return node;
}

ASTNode* make_param_decl_node(GincType type, char* name) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_PARAM_DECL;
	node->var_type = type;
	node->var_name = name;
	return node;
}

ASTNode* make_try_node(ASTNode* try_block, char* catch_var, ASTNode* catch_block) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_TRY;
	node->then_body = try_block;
	node->var_name = catch_var;
	node->else_body = catch_block;
	return node;
}

ASTNode* make_catch_block_node(GincType type, char* class_name, char* var_name, ASTNode* body) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_CATCH_BLOCK;
	node->catch_type = type;
	node->catch_class_name = class_name ? strdup(class_name) : NULL;
	node->var_name = var_name ? strdup(var_name) : NULL;
	node->left = body;
	return node;
}

ASTNode* make_try_catch_node(ASTNode* try_body, ASTNode* catch_blocks, ASTNode* finally_body) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_TRY_CATCH;
	node->left = try_body;
	node->right = catch_blocks;
	node->finally_body = finally_body;
	return node;
}

ASTNode* make_throw_node(ASTNode* expr) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_THROW;
	node->left = expr;
	return node;
}

ASTNode* make_hash_decl_node(GincType val_type, char* name, GincType key_type) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_HASH_DECL;
	node->var_type = val_type;
	node->var_name = name;
	return node;
}

ASTNode* make_index_assign_node(char* name, ASTNode* index, ASTNode* rhs) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_INDEX_ASSIGN;
	node->var_name = name;
	node->left = index;
	node->right = rhs;
	return node;
}

ASTNode* make_class_def_node(char* name, ASTNode* members) {
	ASTNode* node = malloc(sizeof(ASTNode));
	node->type = AST_CLASS_DEF;
	node->var_name = name;
	node->left = members;
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
		case AST_UNARY_MINUS: printf("UNARY -\n"); break;
		case AST_MUL:    printf("*\n"); break;
		case AST_DIV:    printf("/\n"); break;
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
		case AST_FUNC_DECL: printf("FUNC_DECL(%s)\n", node->var_name); break;
		case AST_FUNC_CALL: printf("FUNC_CALL(%s)\n", node->var_name); break;
		case AST_RETURN: printf("RETURN\n"); break;
		case AST_PARAM_DECL: printf("PARAM_DECL(%s)\n", node->var_name); break;
		case AST_TRY: printf("TRY (CATCH %s)\n", node->var_name); break;
		case AST_TRY_CATCH: printf("TRY_CATCH(%s)\n", node->var_name ? node->var_name : ""); break;
		case AST_THROW: printf("THROW\n"); break;
		case AST_HASH_DECL: printf("HASH_DECL(%s)\n", node->var_name); break;
		case AST_INDEX_ASSIGN: printf("INDEX_ASSIGN(%s)\n", node->var_name); break;
		case AST_CLASS_DEF: printf("CLASS_DEF(%s)\n", node->var_name); break;
		case AST_CATCH_BLOCK:
			printf("CATCH_BLOCK(type:%d, class:%s, var:%s)\n", node->catch_type, node->catch_class_name ? node->catch_class_name : "NULL", node->var_name ? node->var_name : "NULL");
			break;
	}
	
	// 子ノードの再帰表示
	if (node->type == AST_SEQ || node->type == AST_ASSIGN || node->type == AST_ADD || node->type == AST_SUB || node->type == AST_MUL || node->type == AST_DIV || node->type == AST_EQ || node->type == AST_LT || node->type == AST_MATCH) {
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
	} else if (node->type == AST_FUNC_DECL) {
		if (node->left) print_ast(node->left, depth + 1);
		if (node->body) print_ast(node->body, depth + 1);
	} else if (node->type == AST_FUNC_CALL || node->type == AST_RETURN || node->type == AST_THROW || node->type == AST_UNARY_MINUS) {
		if (node->left) print_ast(node->left, depth + 1);
	} else if (node->type == AST_TRY) {
		if (node->then_body) print_ast(node->then_body, depth + 1);
		if (node->else_body) print_ast(node->else_body, depth + 1);
	} else if (node->type == AST_TRY_CATCH || node->type == AST_INDEX_ASSIGN) {
		if (node->left) print_ast(node->left, depth + 1);
		if (node->right) print_ast(node->right, depth + 1);
		if (node->finally_body) print_ast(node->finally_body, depth + 1);
	} else if (node->type == AST_CLASS_DEF || node->type == AST_CATCH_BLOCK) {
		if (node->left) print_ast(node->left, depth + 1);
	}
}
