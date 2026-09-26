#ifndef AST_H
#define AST_H

typedef enum {
	TYPE_INT,
	TYPE_STR
} GincType;

typedef enum {
	AST_NUM,
	AST_STR,
	AST_STRING,
	AST_REGEX,
	AST_VAR,
	AST_VARDECL,
	AST_ASSIGN,
	AST_ADD,
	AST_SUB,
	AST_SEQ,
	AST_PRINT,
	AST_BLOCK,
	AST_IF,
	AST_WHILE,
	AST_EQ,
	AST_LT,
	AST_MATCH,
	AST_ARRAY_DECL,
	AST_ARRAY_ACCESS,
	AST_EXPR_LIST
} ASTNodeType;

// 前方宣言
typedef struct ASTNode ASTNode;

struct ASTNode {
	ASTNodeType type;
	GincType var_type; // 変数の型用
	int val;           // 数値用
	char* var_name;    // 変数名用
	char* str_val;     // 文字列リテラル用
	ASTNode* left;     // 左辺
	ASTNode* right;    // 右辺
	ASTNode* cond;     // 条件式
	ASTNode* then_body;// then節
	ASTNode* else_body;// else節
};

ASTNode* make_num_node(int val);
ASTNode* make_str_node(char* str_val);
ASTNode* make_string_node(char* str);
ASTNode* make_regex_node(char* pattern);
ASTNode* make_var_node(char* name);
ASTNode* make_vardecl_node(GincType type, char* var_name, ASTNode* init_expr);
ASTNode* make_binop_node(ASTNodeType type, ASTNode* left, ASTNode* right);
ASTNode* make_print_node(ASTNode* arg);
ASTNode* make_if_node(ASTNode* cond, ASTNode* then_body, ASTNode* else_body);
ASTNode* make_while_node(ASTNode* cond, ASTNode* body);
ASTNode* make_block_node(ASTNode* stmt_list);
ASTNode* make_array_decl_node(GincType type, char* name, int max_size, ASTNode* init_list);
ASTNode* make_array_access_node(char* name, ASTNode* index);
ASTNode* make_expr_list_node(ASTNode* expr, ASTNode* next);
void print_ast(ASTNode* node, int depth); // ★追加

extern char const* current_filename;
extern int current_line;
extern int current_column;

#endif
