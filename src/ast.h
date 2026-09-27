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
	AST_UNARY_MINUS,
	AST_MUL,
	AST_DIV,
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
	AST_EXPR_LIST,
	AST_FUNC_DECL,
	AST_FUNC_CALL,
	AST_RETURN,
	AST_PARAM_DECL,
	AST_TRY,
	AST_TRY_CATCH,
	AST_THROW,
	AST_HASH_DECL,
	AST_INDEX_ASSIGN
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
	ASTNode* body;     // 関数本体用
};

extern ASTNode* make_num_node(int val);
extern ASTNode* make_str_node(char* str_val);
extern ASTNode* make_string_node(char* str);
extern ASTNode* make_regex_node(char* pattern);
extern ASTNode* make_var_node(char* name);
extern ASTNode* make_vardecl_node(GincType type, char* var_name, ASTNode* init_expr);
extern ASTNode* make_binop_node(ASTNodeType type, ASTNode* left, ASTNode* right);
extern ASTNode* make_unary_node(ASTNodeType type, ASTNode* expr);
extern ASTNode* make_print_node(ASTNode* arg);
extern ASTNode* make_if_node(ASTNode* cond, ASTNode* then_body, ASTNode* else_body);
extern ASTNode* make_while_node(ASTNode* cond, ASTNode* body);
extern ASTNode* make_block_node(ASTNode* stmt_list);
extern ASTNode* make_array_decl_node(GincType type, char* name, int max_size, ASTNode* init_list);
extern ASTNode* make_array_access_node(char* name, ASTNode* index);
extern ASTNode* make_expr_list_node(ASTNode* expr, ASTNode* next);
extern ASTNode* make_func_decl_node(GincType ret_type, char* name, ASTNode* params, ASTNode* body);
extern ASTNode* make_func_call_node(char* name, ASTNode* args);
extern ASTNode* make_return_node(ASTNode* expr);
extern ASTNode* make_param_decl_node(GincType type, char* name);
extern ASTNode* make_try_node(ASTNode* try_block, char* catch_var, ASTNode* catch_block);
extern ASTNode* make_try_catch_node(ASTNode* try_body, char* catch_var, ASTNode* catch_body);
extern ASTNode* make_throw_node(ASTNode* expr);
extern ASTNode* make_hash_decl_node(GincType val_type, char* name, GincType key_type);
extern ASTNode* make_index_assign_node(char* name, ASTNode* index, ASTNode* rhs);
extern void print_ast(ASTNode* node, int depth);

extern char const* current_filename;
extern int current_line;
extern int current_column;

#endif
