#ifndef AST_H
#define AST_H

typedef enum {
    AST_NUM,
    AST_VAR,
    AST_ASSIGN,
    AST_ADD,
    AST_SUB
} ASTNodeType;

// 前方宣言
typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeType type;
    int val;           // 数値用
    char* var_name;    // 変数名用
    ASTNode* left;     // 左辺
    ASTNode* right;    // 右辺
};

ASTNode* make_num_node(int val);
ASTNode* make_var_node(char* name);
ASTNode* make_binop_node(ASTNodeType type, ASTNode* left, ASTNode* right);
void print_ast(ASTNode* node, int depth); // ★追加

#endif
