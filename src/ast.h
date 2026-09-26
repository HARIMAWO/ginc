#ifndef AST_H
#define AST_H

typedef enum {
	AST_NUM,
	AST_VAR,
	AST_ASSIGN,
	AST_ADD,
	AST_SUB,
	AST_SEQ,
	AST_PRINT,
	AST_BLOCK,
	AST_IF,
	AST_WHILE,
	AST_EQ,
	AST_LT
} ASTNodeType;

// 前方宣言
typedef struct ASTNode ASTNode;

struct ASTNode {
	ASTNodeType type;
	int val;           // 数値用
	char* var_name;    // 変数名用
	ASTNode* left;     // 左辺
	ASTNode* right;    // 右辺
	ASTNode* cond;     // 条件式
	ASTNode* then_body;// then節
	ASTNode* else_body;// else節
};

ASTNode* make_num_node(int val);
ASTNode* make_var_node(char* name);
ASTNode* make_binop_node(ASTNodeType type, ASTNode* left, ASTNode* right);
ASTNode* make_print_node(ASTNode* arg);
ASTNode* make_if_node(ASTNode* cond, ASTNode* then_body, ASTNode* else_body);
ASTNode* make_while_node(ASTNode* cond, ASTNode* body);
ASTNode* make_block_node(ASTNode* stmt_list);
void print_ast(ASTNode* node, int depth); // ★追加

#endif
