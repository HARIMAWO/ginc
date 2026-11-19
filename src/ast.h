#ifndef AST_H
#define AST_H

typedef enum {
	TYPE_INT,
	TYPE_CHAR,
	TYPE_SHORT,
	TYPE_LONG,
	TYPE_UINT,
	TYPE_FLOAT,
	TYPE_STR,
	TYPE_VAR,
	TYPE_DOUBLE,
	TYPE_BOOL,
	TYPE_VOID,
	TYPE_FILE,
	TYPE_PTR,
	TYPE_VA_LIST,
	TYPE_ALIAS
} GincType;

typedef struct TypeAlias {
	char* alias;
	GincType base_type;
	char* target_type_str;
	struct TypeAlias* next;
} TypeAlias;

typedef enum {
	AST_NUM,
	AST_FLOAT,
	AST_NULL,
	AST_STR,
	AST_STRING,
	AST_REGEX,
	AST_VAR,
	AST_VARDECL,
	AST_ASSIGN,
	AST_ADD,
	AST_SUB,
	AST_UNARY_MINUS,
	AST_POST_INC,
	AST_POST_DEC,
	AST_MUL,
	AST_DIV,
	AST_MOD,
	AST_BIT_AND,
	AST_BIT_OR,
	AST_BIT_XOR,
	AST_LSHIFT,
	AST_RSHIFT,
	AST_LOGICAL_OR,
	AST_LOGICAL_AND,
	AST_LOGICAL_NOT,
	AST_NEQ,
	AST_GT,
	AST_LE,
	AST_GE,
	AST_SEQ,
	AST_PRINT,
	AST_BLOCK,
	AST_IF,
	AST_WHILE,
	AST_DO_WHILE,
	AST_FOR,
	AST_BREAK,
	AST_CONTINUE,
	AST_EQ,
	AST_LT,
	AST_MATCH,
	AST_ARRAY_DECL,
	AST_ARRAY_ACCESS,
	AST_EXPR_LIST,
	AST_FUNC_DECL,
	AST_FUNC_CALL,
	AST_RETURN,
	AST_RETURN_VOID,
	AST_PARAM_DECL,
	AST_TRY,
	AST_TRY_CATCH,
	AST_THROW,
	AST_HASH_DECL,
	AST_INDEX_ASSIGN,
	AST_CLASS_DEF,
	AST_CLASS_VAR_DECL,
	AST_CATCH_BLOCK,
	AST_METHOD_CALL,
	AST_TYPEDEF,
	AST_SWITCH,
	AST_CASE,
	AST_DEFAULT,
	AST_BIT_NOT,
	AST_TERNARY,
	AST_ENUM_DEF,
	AST_ENUM_ITEM,
	AST_CAST,
	AST_ADDR_OF,
	AST_DEREF,
	AST_DEREF_ASSIGN,
	AST_COMMA,
	AST_GOTO,
	AST_LABEL,
	AST_SIZEOF_TYPE,
	AST_SIZEOF_EXPR,
	AST_LAMBDA,
	AST_VA_START,
	AST_VA_ARG,
	AST_VA_END
} ASTNodeType;

// 前方宣言
typedef struct ASTNode ASTNode;

struct ASTNode {
	ASTNodeType type;
	GincType var_type; // 変数の型用
	GincType ptr_target_type; // TYPE_PTR が指す先の型
	long long val;     // 数値用
	double float_val;  // 浮動小数点数用
	char* var_name;    // 変数名用
	char* str_val;     // 文字列リテラル用
	ASTNode* left;     // 左辺
	ASTNode* right;    // 右辺
	ASTNode* cond;     // 条件式
	ASTNode* then_body;// then節
	ASTNode* then_branch; // then節(forループ等用)
	ASTNode* else_body;// else節
	ASTNode* body;     // 関数本体用
	ASTNode* finally_body; // finally節
	GincType catch_type;   // catchブロックの型用
	char* catch_class_name; // Exceptionなどのクラス名用
	int access;            // 0: private, 1: public
	int is_const;          // 0: mutable, 1: const
	int is_static;         // 0: normal, 1: static
	int dim2;              // 2次元配列の列数
	int is_func_ptr;       // 関数ポインタフラグ
	int is_variadic;       // 可変長引数フラグ
	GincType fn_ret_type;  // 関数ポインタ戻り値型
	char* class_name;      // メソッドが所属するクラス名用
	ASTNode* next;         // switch の case/default リンクリスト用
};

extern ASTNode* make_num_node(long long val);
extern ASTNode* make_float_node(double val);
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
extern ASTNode* make_do_while_node(ASTNode* body, ASTNode* cond);
extern ASTNode* make_break_node(void);
extern ASTNode* make_continue_node(void);
extern ASTNode* make_for_node(ASTNode* init, ASTNode* cond, ASTNode* inc, ASTNode* body);
extern ASTNode* make_block_node(ASTNode* stmt_list);
extern ASTNode* make_array_decl_node(GincType type, char* name, int max_size, ASTNode* init_list);
extern ASTNode* make_array_access_node(char* name, ASTNode* index);
extern ASTNode* make_expr_list_node(ASTNode* expr, ASTNode* next);
extern ASTNode* make_func_decl_node(GincType ret_type, char* name, ASTNode* params, ASTNode* body);
extern ASTNode* make_func_call_node(char* name, ASTNode* args);
extern ASTNode* make_return_node(ASTNode* expr);
extern ASTNode* make_return_void_node(void);
extern ASTNode* make_param_decl_node(GincType type, char* name);
extern ASTNode* make_try_node(ASTNode* try_block, char* catch_var, ASTNode* catch_block);
extern ASTNode* make_catch_block_node(GincType type, char* class_name, char* var_name, ASTNode* body);
extern ASTNode* make_try_catch_node(ASTNode* try_body, ASTNode* catch_blocks, ASTNode* finally_body);
extern ASTNode* make_throw_node(ASTNode* expr);
extern ASTNode* make_hash_decl_node(GincType val_type, char* name, GincType key_type);
extern ASTNode* make_index_assign_node(char* name, ASTNode* index, ASTNode* rhs);
extern ASTNode* make_class_def_node(char* name, ASTNode* members);
extern ASTNode* make_class_var_decl_node(char* class_name, char* var_name);
extern ASTNode* make_method_call_node(char* obj_name, char* method_name, ASTNode* args);
extern int is_reserved_keyword(char const* name);
extern int is_typedef_name(char const* name);
extern void register_type_alias(GincType base_type, char const* alias);
extern void register_type_alias_ext(GincType base_type, char const* alias, char const* target_str);
extern GincType resolve_type_alias(char const* name, int* is_found);
extern void reset_type_aliases(void);
extern ASTNode* make_typedef_node(GincType base_type, char* alias);
extern ASTNode* make_typedef_ext_node(GincType base_type, char* alias, char* target_str);
extern ASTNode* make_typedef_node_from_user_type(char const* user_type_name, char* new_alias);
extern ASTNode* make_switch_node(ASTNode* expr, ASTNode* case_list);
extern ASTNode* make_case_node(ASTNode* val_expr, ASTNode* body);
extern ASTNode* make_default_node(ASTNode* body);
extern ASTNode* make_ternary_node(ASTNode* cond, ASTNode* then_expr, ASTNode* else_expr);
extern ASTNode* make_enum_def_node(char* name, ASTNode* items);
extern ASTNode* make_enum_item_node(char* name, ASTNode* val_expr);
extern ASTNode* make_cast_node(GincType target_type, ASTNode* expr);
extern ASTNode* make_ptr_vardecl_node(GincType target_type, char* var_name, ASTNode* init_expr);
extern ASTNode* make_ptr_param_node(GincType target_type, char* var_name);
extern ASTNode* make_goto_node(char* label_name);
extern ASTNode* make_label_node(char* label_name, ASTNode* stmt);
extern ASTNode* make_sizeof_type_node(GincType target_type);
extern ASTNode* make_sizeof_expr_node(ASTNode* expr);
extern ASTNode* make_array2d_decl_node(GincType type, char* name, int dim1, int dim2, ASTNode* init_list);
extern ASTNode* make_array2d_access_node(char* name, ASTNode* idx1, ASTNode* idx2);
extern ASTNode* make_index2d_assign_node(char* name, ASTNode* idx1, ASTNode* idx2, ASTNode* rhs);
extern ASTNode* make_func_ptr_vardecl_node(GincType ret_type, char* name, ASTNode* params, ASTNode* init_expr);
extern ASTNode* make_func_ptr_param_node(GincType ret_type, char* name, ASTNode* params);
extern ASTNode* make_lambda_node(GincType ret_type, ASTNode* params, ASTNode* body);
extern ASTNode* make_va_start_node(char* ap_name, char* last_param);
extern ASTNode* make_va_arg_node(char* ap_name, GincType target_type);
extern ASTNode* make_va_end_node(char* ap_name);
extern void print_ast(ASTNode* node, int depth);

extern char const* current_filename;
extern int current_line;
extern int current_column;

#endif
