%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

int yylex(void);
void yyerror(const char *s);
ASTNode* root_node;
static char current_class[256] = "";
static int current_access = 0;
%}

%union {
    long long num;
    double float_val;
    char* str;
    ASTNode* ast;
    GincType ginc_type;
}

%token <num> T_NUM
%token <float_val> T_FLOAT_LITERAL
%token <str> T_IDENT T_STR T_STRING T_REGEX T_USER_TYPE
%token T_INT_TYPE T_STR_TYPE T_VAR_TYPE T_DOUBLE_TYPE T_ASSIGN T_PLUS T_MINUS T_STAR T_SLASH T_SEMI T_PRINT T_LPAREN T_RPAREN T_IF T_ELSE T_WHILE T_LBRACE T_RBRACE T_EQ T_LT T_MATCH T_LBRACKET T_RBRACKET T_COMMA T_RETURN T_TRY T_CATCH T_THROW T_FINALLY T_DOT T_CLASS T_STRUCT T_PUBLIC T_PRIVATE T_COLON T_TRUE T_FALSE T_NULL T_VOID T_CHAR T_FILE T_AND T_OR T_NOT T_NEQ T_GT T_LE T_GE T_MOD T_AMP T_FOR T_INC T_DEC T_DO T_BREAK T_CONTINUE T_TYPEDEF T_BIT_OR_TOK T_XOR T_LSHIFT T_RSHIFT T_SWITCH T_CASE T_DEFAULT T_ADD_ASSIGN T_SUB_ASSIGN T_MUL_ASSIGN T_DIV_ASSIGN T_MOD_ASSIGN T_AND_ASSIGN T_OR_ASSIGN T_XOR_ASSIGN T_LSHIFT_ASSIGN T_RSHIFT_ASSIGN T_TILDE T_QUESTION T_CONST T_ENUM T_SHORT T_LONG T_UNSIGNED T_FLOAT_TYPE T_BOOL T_STATIC T_EXTERN T_GOTO T_SIZEOF T_ARROW T_ELLIPSIS T_VA_LIST T_VA_START T_VA_ARG T_VA_END

%type <ast> stmt_list stmt expr comma_expr expr_list param_list param_decl member_decl member_list class_def class_member class_item class_member_list finally_clause func_body catch_block catch_list opt_expr for_init case_list case_item opt_stmt_list case_val enum_def enum_item enum_list array2d_init_row array2d_init_list fn_ptr_param fn_ptr_param_list
%type <ginc_type> ext_type

%right T_ASSIGN T_ADD_ASSIGN T_SUB_ASSIGN T_MUL_ASSIGN T_DIV_ASSIGN T_MOD_ASSIGN T_AND_ASSIGN T_OR_ASSIGN T_XOR_ASSIGN T_LSHIFT_ASSIGN T_RSHIFT_ASSIGN
%right T_QUESTION T_COLON
%left T_OR
%left T_AND
%left T_BIT_OR_TOK
%left T_XOR
%left T_AMP
%left T_EQ T_NEQ
%left T_LT T_LE T_GT T_GE T_MATCH
%left T_LSHIFT T_RSHIFT
%left T_PLUS T_MINUS
%left T_STAR T_SLASH T_MOD
%right UMINUS T_NOT T_TILDE USTAR UAMP T_INC T_DEC
%left T_DOT T_ARROW T_LBRACKET T_LPAREN

%%

program:
    stmt_list { root_node = $1; }
    ;

stmt_list:
    stmt_list stmt { $$ = make_binop_node(AST_SEQ, $1, $2); }
    | stmt         { $$ = $1; }
    ;

expr_list:
    expr { $$ = make_expr_list_node($1, NULL); }
    | expr T_COMMA expr_list { $$ = make_expr_list_node($1, $3); }
    ;

ext_type:
    T_SHORT { $$ = TYPE_SHORT; }
    | T_LONG { $$ = TYPE_LONG; }
    | T_UNSIGNED { $$ = TYPE_UINT; }
    | T_UNSIGNED T_INT_TYPE { $$ = TYPE_UINT; }
    | T_FLOAT_TYPE { $$ = TYPE_FLOAT; }
    | T_BOOL { $$ = TYPE_BOOL; }
    ;

param_decl:
    T_INT_TYPE T_IDENT { $$ = make_param_decl_node(TYPE_INT, $2); }
  | T_CHAR T_IDENT { $$ = make_param_decl_node(TYPE_CHAR, $2); }
  | ext_type T_IDENT { $$ = make_param_decl_node($1, $2); }
  | T_STR_TYPE T_IDENT { $$ = make_param_decl_node(TYPE_STR, $2); }
  | T_VAR_TYPE T_IDENT { $$ = make_param_decl_node(TYPE_VAR, $2); }
  | T_DOUBLE_TYPE T_IDENT { $$ = make_param_decl_node(TYPE_DOUBLE, $2); }
  | T_USER_TYPE T_IDENT {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        $$ = make_param_decl_node(found ? bt : TYPE_INT, $2);
    }
  | T_CHAR T_STAR T_IDENT T_LBRACKET T_RBRACKET { $$ = make_ptr_param_node(TYPE_STR, $3); }
  | T_CHAR T_STAR T_STAR T_IDENT { $$ = make_ptr_param_node(TYPE_STR, $4); }
  | T_CHAR T_STAR T_IDENT { $$ = make_ptr_param_node(TYPE_CHAR, $3); }
  | T_INT_TYPE T_STAR T_IDENT { $$ = make_ptr_param_node(TYPE_INT, $3); }
  | T_DOUBLE_TYPE T_STAR T_IDENT { $$ = make_ptr_param_node(TYPE_DOUBLE, $3); }
  | ext_type T_STAR T_IDENT { $$ = make_ptr_param_node($1, $3); }
  | T_VOID T_STAR T_IDENT { $$ = make_ptr_param_node(TYPE_VOID, $3); }
  | T_FILE T_STAR T_IDENT { $$ = make_param_decl_node(TYPE_FILE, $3); }
  | T_INT_TYPE T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN {
        $$ = make_func_ptr_param_node(TYPE_INT, $4, $7);
    }
  | T_VOID T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN {
        $$ = make_func_ptr_param_node(TYPE_VOID, $4, $7);
    }
  | T_DOUBLE_TYPE T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN {
        $$ = make_func_ptr_param_node(TYPE_DOUBLE, $4, $7);
    }
  | T_IDENT T_IDENT {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        if (found) {
            $$ = make_param_decl_node(bt, $2);
        } else {
            yyerror("Unknown type name");
            YYERROR;
        }
    }
;

param_list:
    /* empty */ { $$ = NULL; }
    | T_VOID { $$ = NULL; }
    | T_ELLIPSIS {
        ASTNode* n = make_param_decl_node(TYPE_VOID, "__ellipsis__");
        n->is_variadic = 1;
        $$ = make_expr_list_node(n, NULL);
    }
    | param_decl { $$ = make_expr_list_node($1, NULL); }
    | param_decl T_COMMA param_list { $$ = make_expr_list_node($1, $3); }
    ;

fn_ptr_param:
    T_INT_TYPE { $$ = make_param_decl_node(TYPE_INT, ""); }
    | T_DOUBLE_TYPE { $$ = make_param_decl_node(TYPE_DOUBLE, ""); }
    | T_CHAR { $$ = make_param_decl_node(TYPE_CHAR, ""); }
    | ext_type { $$ = make_param_decl_node($1, ""); }
    | T_STR_TYPE { $$ = make_param_decl_node(TYPE_STR, ""); }
    | param_decl { $$ = $1; }
    ;

fn_ptr_param_list:
    /* empty */ { $$ = NULL; }
    | T_VOID { $$ = NULL; }
    | fn_ptr_param { $$ = make_expr_list_node($1, NULL); }
    | fn_ptr_param T_COMMA fn_ptr_param_list { $$ = make_expr_list_node($1, $3); }
    ;

array2d_init_row:
    T_LBRACE expr_list T_RBRACE { $$ = $2; }
    ;

array2d_init_list:
    array2d_init_row { $$ = make_expr_list_node($1, NULL); }
    | array2d_init_row T_COMMA array2d_init_list { $$ = make_expr_list_node($1, $3); }
    ;

member_decl:
    T_INT_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_INT, $2); }
    | T_STR_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_STR, $2); }
    | T_VAR_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_VAR, $2); }
    | T_DOUBLE_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_DOUBLE, $2); }
    | T_USER_TYPE T_IDENT T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        $$ = make_param_decl_node(found ? bt : TYPE_INT, $2);
    }
    | T_IDENT T_IDENT T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        if (found) {
            $$ = make_param_decl_node(bt, $2);
        } else {
            yyerror("Unknown type name in class member");
            YYERROR;
        }
    }
    ;

member_list:
    /* empty */ { $$ = NULL; }
    | member_decl member_list { $$ = make_expr_list_node($1, $2); }
    ;

opt_expr:
    /* empty */ { $$ = NULL; }
    | comma_expr { $$ = $1; }
    ;

case_val:
    T_NUM { $$ = make_num_node($1); }
    | T_MINUS T_NUM { $$ = make_num_node(-$2); }
    | T_IDENT { $$ = make_var_node($1); }
    ;

enum_item:
    T_IDENT { $$ = make_enum_item_node($1, NULL); }
    | T_IDENT T_ASSIGN T_NUM { $$ = make_enum_item_node($1, make_num_node($3)); }
    | T_IDENT T_ASSIGN T_MINUS T_NUM { $$ = make_enum_item_node($1, make_num_node(-$4)); }
    ;

enum_list:
    enum_item { $$ = make_expr_list_node($1, NULL); }
    | enum_item T_COMMA { $$ = make_expr_list_node($1, NULL); }
    | enum_item T_COMMA enum_list { $$ = make_expr_list_node($1, $3); }
    ;

enum_def:
    T_ENUM T_IDENT T_LBRACE enum_list T_RBRACE T_SEMI {
        $$ = make_enum_def_node($2, $4);
    }
    | T_ENUM T_LBRACE enum_list T_RBRACE T_SEMI {
        $$ = make_enum_def_node(NULL, $3);
    }
    ;

opt_stmt_list:
    /* empty */ { $$ = NULL; }
    | stmt_list { $$ = $1; }
    ;

case_item:
    T_CASE case_val T_COLON opt_stmt_list {
        $$ = make_case_node($2, $4);
    }
    | T_DEFAULT T_COLON opt_stmt_list {
        $$ = make_default_node($3);
    }
    ;

case_list:
    /* empty */ { $$ = NULL; }
    | case_item case_list {
        $1->next = $2;
        $$ = $1;
    }
    ;

for_init:
    /* empty */ { $$ = NULL; }
    | comma_expr { $$ = $1; }
    | T_INT_TYPE T_IDENT T_ASSIGN expr { $$ = make_vardecl_node(TYPE_INT, $2, $4); }
    | T_CHAR T_IDENT T_ASSIGN expr { $$ = make_vardecl_node(TYPE_CHAR, $2, $4); }
    | ext_type T_IDENT T_ASSIGN expr { $$ = make_vardecl_node($1, $2, $4); }
    | T_STR_TYPE T_IDENT T_ASSIGN expr { $$ = make_vardecl_node(TYPE_STR, $2, $4); }
    | T_DOUBLE_TYPE T_IDENT T_ASSIGN expr { $$ = make_vardecl_node(TYPE_DOUBLE, $2, $4); }
    | T_VAR_TYPE T_IDENT T_ASSIGN expr { $$ = make_vardecl_node(TYPE_VAR, $2, $4); }
    | T_USER_TYPE T_IDENT T_ASSIGN expr {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        $$ = make_vardecl_node(found ? bt : TYPE_INT, $2, $4);
    }
    ;

class_member:
    member_decl {
        $$ = $1;
    }
    | T_INT_TYPE T_IDENT T_LPAREN param_list T_RPAREN func_body {
        if (strlen(current_class) > 0) {
            char func_name[512];
            sprintf(func_name, "%s_%s", current_class, $2);
            ASTNode* this_param = make_param_decl_node(TYPE_VAR, "this");
            ASTNode* this_list = make_expr_list_node(this_param, $4);
            $$ = make_func_decl_node(TYPE_INT, strdup(func_name), this_list, $6);
            $$->str_val = strdup($2);
        } else {
            $$ = make_func_decl_node(TYPE_INT, $2, $4, $6);
            $$->str_val = strdup($2);
        }
    }
    ;

class_item:
    T_PUBLIC T_COLON { current_access = 1; $$ = NULL; }
    | T_PRIVATE T_COLON { current_access = 0; $$ = NULL; }
    | class_member {
        $$ = $1;
        if ($$) {
            $$->access = current_access;
            if (strlen(current_class) > 0 && $$->type == AST_FUNC_DECL) {
                $$->class_name = strdup(current_class);
            }
        }
    }
    ;

class_member_list:
    /* empty */ { $$ = NULL; }
    | class_item class_member_list { $$ = $1 ? make_expr_list_node($1, $2) : $2; }
    ;

class_def:
    T_CLASS T_IDENT { strcpy(current_class, $2); current_access = 0; } T_LBRACE class_member_list T_RBRACE T_SEMI {
        $$ = make_class_def_node($2, $5);
        current_class[0] = '\0';
    }
    | T_STRUCT T_IDENT { strcpy(current_class, $2); current_access = 1; } T_LBRACE class_member_list T_RBRACE T_SEMI {
        $$ = make_class_def_node($2, $5);
        current_class[0] = '\0';
    }
    ;

finally_clause:
    /* empty */ { $$ = NULL; }
    | T_FINALLY T_LBRACE stmt_list T_RBRACE { $$ = $3; }
    ;

catch_block:
    T_CATCH T_LPAREN T_IDENT T_IDENT T_RPAREN T_LBRACE stmt_list T_RBRACE
    { $$ = make_catch_block_node(TYPE_VAR, $3, $4, $7); }
    | T_CATCH T_LPAREN T_INT_TYPE T_IDENT T_RPAREN T_LBRACE stmt_list T_RBRACE
    { $$ = make_catch_block_node(TYPE_INT, NULL, $4, $7); }
    | T_CATCH T_LPAREN T_STR_TYPE T_IDENT T_RPAREN T_LBRACE stmt_list T_RBRACE
    { $$ = make_catch_block_node(TYPE_STR, NULL, $4, $7); }
    | T_CATCH T_LPAREN T_VAR_TYPE T_IDENT T_RPAREN T_LBRACE stmt_list T_RBRACE
    { $$ = make_catch_block_node(TYPE_VAR, NULL, $4, $7); }
    ;

catch_list:
    catch_block { $$ = make_expr_list_node($1, NULL); }
    | catch_block catch_list { $$ = make_expr_list_node($1, $2); }
    ;

func_body:
    T_LBRACE stmt_list T_RBRACE { $$ = $2; }
    | T_TRY T_LBRACE stmt_list T_RBRACE catch_list finally_clause {
        $$ = make_try_catch_node($3, $5, $6);
    }
    | T_TRY T_LBRACE stmt_list T_RBRACE finally_clause {
        $$ = make_try_catch_node($3, NULL, $5);
    }
    ;

stmt:
    T_TYPEDEF T_INT_TYPE T_IDENT T_SEMI { $$ = make_typedef_node(TYPE_INT, $3); }
    | T_TYPEDEF T_INT_TYPE T_USER_TYPE T_SEMI { $$ = make_typedef_node(TYPE_INT, $3); }
    | T_TYPEDEF T_STR_TYPE T_IDENT T_SEMI { $$ = make_typedef_node(TYPE_STR, $3); }
    | T_TYPEDEF T_STR_TYPE T_USER_TYPE T_SEMI { $$ = make_typedef_node(TYPE_STR, $3); }
    | T_TYPEDEF T_DOUBLE_TYPE T_IDENT T_SEMI { $$ = make_typedef_node(TYPE_DOUBLE, $3); }
    | T_TYPEDEF T_DOUBLE_TYPE T_USER_TYPE T_SEMI { $$ = make_typedef_node(TYPE_DOUBLE, $3); }
    | T_TYPEDEF T_CHAR T_IDENT T_SEMI { $$ = make_typedef_node(TYPE_CHAR, $3); }
    | T_TYPEDEF T_CHAR T_USER_TYPE T_SEMI { $$ = make_typedef_node(TYPE_CHAR, $3); }
    | T_TYPEDEF ext_type T_IDENT T_SEMI { $$ = make_typedef_node($2, $3); }
    | T_TYPEDEF ext_type T_USER_TYPE T_SEMI { $$ = make_typedef_node($2, $3); }
    | T_TYPEDEF T_FILE T_STAR T_IDENT T_SEMI { $$ = make_typedef_node(TYPE_FILE, $4); }
    | T_TYPEDEF T_FILE T_STAR T_USER_TYPE T_SEMI { $$ = make_typedef_node(TYPE_FILE, $4); }
    | T_TYPEDEF T_USER_TYPE T_IDENT T_SEMI { $$ = make_typedef_node_from_user_type($2, $3); }
    | T_TYPEDEF T_USER_TYPE T_USER_TYPE T_SEMI { $$ = make_typedef_node_from_user_type($2, $3); }
    | T_TYPEDEF T_IDENT T_IDENT T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($2, &found);
        if (found) {
            $$ = make_typedef_ext_node(bt, $3, $2);
        } else {
            // 構造体名または未解像型エイリアスとして登録
            $$ = make_typedef_ext_node(TYPE_ALIAS, $3, $2);
        }
    }
    | T_TYPEDEF T_IDENT T_USER_TYPE T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($2, &found);
        if (found) {
            $$ = make_typedef_ext_node(bt, $3, $2);
        } else {
            $$ = make_typedef_ext_node(TYPE_ALIAS, $3, $2);
        }
    }
    | T_USER_TYPE T_IDENT T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        $$ = make_vardecl_node(found ? bt : TYPE_INT, $2, NULL);
    }
    | T_USER_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        $$ = make_vardecl_node(found ? bt : TYPE_INT, $2, $4);
    }
    | T_USER_TYPE T_IDENT T_LPAREN param_list T_RPAREN func_body {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        $$ = make_func_decl_node(found ? bt : TYPE_INT, $2, $4, $6);
    }
    | T_USER_TYPE T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        $$ = make_func_decl_node(found ? bt : TYPE_INT, $2, $4, NULL);
    }
    | T_IDENT T_IDENT T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        if (found) {
            $$ = make_vardecl_node(bt, $2, NULL);
        } else {
            $$ = make_class_var_decl_node($1, $2);
        }
    }
    | T_IDENT T_IDENT T_ASSIGN expr T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($1, &found);
        if (found) {
            $$ = make_vardecl_node(bt, $2, $4);
        } else {
            yyerror("Unknown type for assignment");
            YYERROR;
        }
    }
    | T_INT_TYPE T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_INT, $2, $4, $6);
    }
    | T_STR_TYPE T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_STR, $2, $4, $6);
    }
    | T_DOUBLE_TYPE T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_DOUBLE, $2, $4, $6);
    }
    | T_VOID T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_VOID, $2, $4, $6);
    }
    | T_INT_TYPE T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node(TYPE_INT, $2, $4, NULL);
    }
    | T_STR_TYPE T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node(TYPE_STR, $2, $4, NULL);
    }
    | T_DOUBLE_TYPE T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node(TYPE_DOUBLE, $2, $4, NULL);
    }
    | T_VOID T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node(TYPE_VOID, $2, $4, NULL);
    }
    | T_CHAR T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_CHAR, $2, $4, $6);
    }
    | T_CHAR T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node(TYPE_CHAR, $2, $4, NULL);
    }
    | ext_type T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node($1, $2, $4, $6);
    }
    | ext_type T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node($1, $2, $4, NULL);
    }
    | enum_def {
        $$ = $1;
    }
    | T_RETURN expr T_SEMI {
        $$ = make_return_node($2);
    }
    | T_RETURN T_SEMI {
        $$ = make_return_void_node();
    }
    | T_TRY T_LBRACE stmt_list T_RBRACE catch_list finally_clause {
        $$ = make_try_catch_node($3, $5, $6);
    }
    | T_TRY T_LBRACE stmt_list T_RBRACE catch_list finally_clause T_SEMI {
        $$ = make_try_catch_node($3, $5, $6);
    }
    | T_TRY T_LBRACE stmt_list T_RBRACE finally_clause T_SEMI {
        $$ = make_try_catch_node($3, NULL, $5);
    }
    | T_TRY stmt T_CATCH T_LPAREN T_IDENT T_RPAREN stmt {
        $$ = make_try_node($2, $5, $7);
    }
    | T_THROW expr T_SEMI {
        $$ = make_throw_node($2);
    }
    | class_def {
        $$ = $1;
    }
    | T_CLASS T_IDENT T_IDENT T_SEMI {
        $$ = make_class_var_decl_node($2, $3);
    }
    | T_STRUCT T_IDENT T_IDENT T_SEMI {
        $$ = make_class_var_decl_node($2, $3);
    }
    | T_INT_TYPE T_IDENT T_LBRACKET T_STR_TYPE T_RBRACKET T_SEMI {
        $$ = make_hash_decl_node(TYPE_INT, $2, TYPE_STR);
    }
    | T_VAR_TYPE T_IDENT T_LBRACKET T_STR_TYPE T_RBRACKET T_SEMI {
        $$ = make_hash_decl_node(TYPE_VAR, $2, TYPE_STR);
    }
    | T_INT_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node(TYPE_INT, $2, $4, $8);
    }
    | T_INT_TYPE T_IDENT T_LBRACKET T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node(TYPE_INT, $2, -1, $7);
    }
    | T_INT_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_SEMI {
        $$ = make_array_decl_node(TYPE_INT, $2, $4, NULL);
    }
    | T_INT_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_LBRACKET T_NUM T_RBRACKET T_SEMI {
        $$ = make_array2d_decl_node(TYPE_INT, $2, $4, $7, NULL);
    }
    | T_INT_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_LBRACKET T_NUM T_RBRACKET T_ASSIGN T_LBRACE array2d_init_list T_RBRACE T_SEMI {
        $$ = make_array2d_decl_node(TYPE_INT, $2, $4, $7, $11);
    }
    | T_DOUBLE_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_LBRACKET T_NUM T_RBRACKET T_SEMI {
        $$ = make_array2d_decl_node(TYPE_DOUBLE, $2, $4, $7, NULL);
    }
    | T_DOUBLE_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_LBRACKET T_NUM T_RBRACKET T_ASSIGN T_LBRACE array2d_init_list T_RBRACE T_SEMI {
        $$ = make_array2d_decl_node(TYPE_DOUBLE, $2, $4, $7, $11);
    }
    | ext_type T_IDENT T_LBRACKET T_NUM T_RBRACKET T_LBRACKET T_NUM T_RBRACKET T_SEMI {
        $$ = make_array2d_decl_node($1, $2, $4, $7, NULL);
    }
    | ext_type T_IDENT T_LBRACKET T_NUM T_RBRACKET T_LBRACKET T_NUM T_RBRACKET T_ASSIGN T_LBRACE array2d_init_list T_RBRACE T_SEMI {
        $$ = make_array2d_decl_node($1, $2, $4, $7, $11);
    }
    | T_INT_TYPE T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN T_SEMI {
        $$ = make_func_ptr_vardecl_node(TYPE_INT, $4, $7, NULL);
    }
    | T_INT_TYPE T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN T_ASSIGN expr T_SEMI {
        $$ = make_func_ptr_vardecl_node(TYPE_INT, $4, $7, $10);
    }
    | T_VOID T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN T_SEMI {
        $$ = make_func_ptr_vardecl_node(TYPE_VOID, $4, $7, NULL);
    }
    | T_VOID T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN T_ASSIGN expr T_SEMI {
        $$ = make_func_ptr_vardecl_node(TYPE_VOID, $4, $7, $10);
    }
    | T_DOUBLE_TYPE T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN T_SEMI {
        $$ = make_func_ptr_vardecl_node(TYPE_DOUBLE, $4, $7, NULL);
    }
    | T_DOUBLE_TYPE T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN T_ASSIGN expr T_SEMI {
        $$ = make_func_ptr_vardecl_node(TYPE_DOUBLE, $4, $7, $10);
    }
    | ext_type T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN T_SEMI {
        $$ = make_func_ptr_vardecl_node($1, $4, $7, NULL);
    }
    | ext_type T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN fn_ptr_param_list T_RPAREN T_ASSIGN expr T_SEMI {
        $$ = make_func_ptr_vardecl_node($1, $4, $7, $10);
    }
    | T_DOUBLE_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node(TYPE_DOUBLE, $2, $4, $8);
    }
    | T_DOUBLE_TYPE T_IDENT T_LBRACKET T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node(TYPE_DOUBLE, $2, -1, $7);
    }
    | T_DOUBLE_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_SEMI {
        $$ = make_array_decl_node(TYPE_DOUBLE, $2, $4, NULL);
    }
    | T_STR_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node(TYPE_STR, $2, $4, $8);
    }
    | T_STR_TYPE T_IDENT T_LBRACKET T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node(TYPE_STR, $2, -1, $7);
    }
    | T_STR_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_SEMI {
        $$ = make_array_decl_node(TYPE_STR, $2, $4, NULL);
    }
    | T_CONST T_INT_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_INT, $3, $5);
        $$->is_const = 1;
    }
    | T_CONST T_DOUBLE_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_DOUBLE, $3, $5);
        $$->is_const = 1;
    }
    | T_CONST T_STR_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_STR, $3, $5);
        $$->is_const = 1;
    }
    | T_CONST T_VAR_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_VAR, $3, $5);
        $$->is_const = 1;
    }
    | T_CONST T_USER_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        int found = 0;
        GincType bt = resolve_type_alias($2, &found);
        $$ = make_vardecl_node(found ? bt : TYPE_INT, $3, $5);
        $$->is_const = 1;
    }
    | T_CHAR T_IDENT T_SEMI {
        $$ = make_vardecl_node(TYPE_CHAR, $2, NULL);
    }
    | T_CHAR T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_CHAR, $2, $4);
    }
    | T_CONST T_CHAR T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_CHAR, $3, $5);
        $$->is_const = 1;
    }
    | ext_type T_IDENT T_SEMI {
        $$ = make_vardecl_node($1, $2, NULL);
    }
    | ext_type T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node($1, $2, $4);
    }
    | T_CONST ext_type T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node($2, $3, $5);
        $$->is_const = 1;
    }
    | ext_type T_IDENT T_LBRACKET T_NUM T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node($1, $2, $4, $8);
    }
    | ext_type T_IDENT T_LBRACKET T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node($1, $2, -1, $7);
    }
    | ext_type T_IDENT T_LBRACKET T_NUM T_RBRACKET T_SEMI {
        $$ = make_array_decl_node($1, $2, $4, NULL);
    }
    | T_CHAR T_IDENT T_LBRACKET T_NUM T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node(TYPE_CHAR, $2, $4, $8);
    }
    | T_CHAR T_IDENT T_LBRACKET T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node(TYPE_CHAR, $2, -1, $7);
    }
    | T_CHAR T_IDENT T_LBRACKET T_NUM T_RBRACKET T_SEMI {
        $$ = make_array_decl_node(TYPE_CHAR, $2, $4, NULL);
    }
    | T_INT_TYPE T_STAR T_IDENT T_SEMI {
        $$ = make_ptr_vardecl_node(TYPE_INT, $3, NULL);
    }
    | T_INT_TYPE T_STAR T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_ptr_vardecl_node(TYPE_INT, $3, $5);
    }
    | T_DOUBLE_TYPE T_STAR T_IDENT T_SEMI {
        $$ = make_ptr_vardecl_node(TYPE_DOUBLE, $3, NULL);
    }
    | T_DOUBLE_TYPE T_STAR T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_ptr_vardecl_node(TYPE_DOUBLE, $3, $5);
    }
    | T_CHAR T_STAR T_IDENT T_SEMI {
        $$ = make_ptr_vardecl_node(TYPE_CHAR, $3, NULL);
    }
    | T_CHAR T_STAR T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_ptr_vardecl_node(TYPE_CHAR, $3, $5);
    }
    | ext_type T_STAR T_IDENT T_SEMI {
        $$ = make_ptr_vardecl_node($1, $3, NULL);
    }
    | ext_type T_STAR T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_ptr_vardecl_node($1, $3, $5);
    }
    | T_VOID T_STAR T_IDENT T_SEMI {
        $$ = make_ptr_vardecl_node(TYPE_VOID, $3, NULL);
    }
    | T_VOID T_STAR T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_ptr_vardecl_node(TYPE_VOID, $3, $5);
    }
    | T_INT_TYPE T_IDENT T_SEMI {
        $$ = make_vardecl_node(TYPE_INT, $2, NULL);
    }
    | T_STR_TYPE T_IDENT T_SEMI {
        $$ = make_vardecl_node(TYPE_STR, $2, NULL);
    }
    | T_DOUBLE_TYPE T_IDENT T_SEMI {
        $$ = make_vardecl_node(TYPE_DOUBLE, $2, NULL);
    }
    | T_VAR_TYPE T_IDENT T_SEMI {
        $$ = make_vardecl_node(TYPE_VAR, $2, NULL);
    }
    | T_INT_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_INT, $2, $4);
    }
    | T_STR_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_STR, $2, $4);
    }
    | T_DOUBLE_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_DOUBLE, $2, $4);
    }
    | T_VAR_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_VAR, $2, $4);
    }
    | T_FILE T_STAR T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_FILE, $3, $5);
    }
    | T_FILE T_STAR T_IDENT T_SEMI {
        $$ = make_vardecl_node(TYPE_FILE, $3, NULL);
    }
    | T_PRINT T_LPAREN expr T_RPAREN T_SEMI {
        $$ = make_print_node($3);
    }
    | T_LBRACE stmt_list T_RBRACE {
        $$ = make_block_node($2);
    }
    | T_IF T_LPAREN expr T_RPAREN stmt T_ELSE stmt {
        $$ = make_if_node($3, $5, $7);
    }
    | T_IF T_LPAREN expr T_RPAREN stmt {
        $$ = make_if_node($3, $5, NULL);
    }
    | T_WHILE T_LPAREN expr T_RPAREN stmt {
        $$ = make_while_node($3, $5);
    }
    | T_DO stmt T_WHILE T_LPAREN expr T_RPAREN T_SEMI {
        $$ = make_do_while_node($2, $5);
    }
    | T_BREAK T_SEMI {
        $$ = make_break_node();
    }
    | T_CONTINUE T_SEMI {
        $$ = make_continue_node();
    }
    | T_FOR T_LPAREN for_init T_SEMI opt_expr T_SEMI opt_expr T_RPAREN stmt {
        $$ = make_for_node($3, $5, $7, $9);
    }
    | T_SWITCH T_LPAREN expr T_RPAREN T_LBRACE case_list T_RBRACE {
        $$ = make_switch_node($3, $6);
    }
    | T_IDENT T_STAR T_IDENT T_SEMI {
        $$ = make_class_var_decl_node($1, $3);
    }
    | T_STATIC T_INT_TYPE T_IDENT T_SEMI {
        $$ = make_vardecl_node(TYPE_INT, $3, NULL);
        $$->is_static = 1;
    }
    | T_STATIC T_INT_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_INT, $3, $5);
        $$->is_static = 1;
    }
    | T_STATIC T_DOUBLE_TYPE T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node(TYPE_DOUBLE, $3, $5);
        $$->is_static = 1;
    }
    | T_STATIC ext_type T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_vardecl_node($2, $3, $5);
        $$->is_static = 1;
    }
    | T_STATIC T_INT_TYPE T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_INT, $3, $5, $7);
    }
    | T_STATIC T_VOID T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_VOID, $3, $5, $7);
    }
    | T_STATIC ext_type T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node($2, $3, $5, $7);
    }
    | T_EXTERN T_INT_TYPE T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node(TYPE_INT, $3, $5, NULL);
    }
    | T_EXTERN T_VOID T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node(TYPE_VOID, $3, $5, NULL);
    }
    | T_EXTERN T_DOUBLE_TYPE T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node(TYPE_DOUBLE, $3, $5, NULL);
    }
    | T_EXTERN T_CHAR T_STAR T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        ASTNode* fn = make_func_decl_node(TYPE_PTR, $4, $6, NULL);
        fn->ptr_target_type = TYPE_CHAR;
        $$ = fn;
    }
    | T_EXTERN T_VOID T_STAR T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        ASTNode* fn = make_func_decl_node(TYPE_PTR, $4, $6, NULL);
        fn->ptr_target_type = TYPE_VOID;
        $$ = fn;
    }
    | T_EXTERN T_FILE T_STAR T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node(TYPE_FILE, $4, $6, NULL);
    }
    | T_EXTERN ext_type T_IDENT T_LPAREN param_list T_RPAREN T_SEMI {
        $$ = make_func_decl_node($2, $3, $5, NULL);
    }
    | T_GOTO T_IDENT T_SEMI {
        $$ = make_goto_node($2);
    }
    | T_VA_LIST T_IDENT T_SEMI {
        $$ = make_vardecl_node(TYPE_VA_LIST, $2, NULL);
    }
    | T_VA_START T_LPAREN T_IDENT T_COMMA T_IDENT T_RPAREN T_SEMI {
        $$ = make_va_start_node($3, $5);
    }
    | T_VA_END T_LPAREN T_IDENT T_RPAREN T_SEMI {
        $$ = make_va_end_node($3);
    }
    | T_IDENT T_COLON stmt {
        $$ = make_label_node($1, $3);
    }
    | T_SEMI {
        $$ = NULL;
    }
    | comma_expr T_SEMI {
        $$ = $1;
    }
    ;

comma_expr:
    expr { $$ = $1; }
    | comma_expr T_COMMA expr { $$ = make_binop_node(AST_COMMA, $1, $3); }
    ;

expr:
    T_INC T_IDENT { $$ = make_binop_node(AST_ASSIGN, make_var_node($2), make_binop_node(AST_ADD, make_var_node($2), make_num_node(1))); }
    | T_DEC T_IDENT { $$ = make_binop_node(AST_ASSIGN, make_var_node($2), make_binop_node(AST_SUB, make_var_node($2), make_num_node(1))); }
    | T_IDENT T_INC { $$ = make_unary_node(AST_POST_INC, make_var_node($1)); }
    | T_IDENT T_DEC { $$ = make_unary_node(AST_POST_DEC, make_var_node($1)); }
    | expr T_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, $1, $3); }
    | T_AMP expr %prec UAMP { $$ = make_unary_node(AST_ADDR_OF, $2); }
    | T_STAR expr %prec USTAR { $$ = make_unary_node(AST_DEREF, $2); }
    | T_LPAREN T_INT_TYPE T_RPAREN expr %prec UMINUS { $$ = make_cast_node(TYPE_INT, $4); }
    | T_LPAREN T_DOUBLE_TYPE T_RPAREN expr %prec UMINUS { $$ = make_cast_node(TYPE_DOUBLE, $4); }
    | T_LPAREN T_CHAR T_RPAREN expr %prec UMINUS { $$ = make_cast_node(TYPE_CHAR, $4); }
    | T_LPAREN ext_type T_RPAREN expr %prec UMINUS { $$ = make_cast_node($2, $4); }
    | T_IDENT T_ADD_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_ADD, make_var_node($1), $3)); }
    | T_IDENT T_SUB_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_SUB, make_var_node($1), $3)); }
    | T_IDENT T_MUL_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_MUL, make_var_node($1), $3)); }
    | T_IDENT T_DIV_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_DIV, make_var_node($1), $3)); }
    | T_IDENT T_MOD_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_MOD, make_var_node($1), $3)); }
    | T_IDENT T_AND_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_BIT_AND, make_var_node($1), $3)); }
    | T_IDENT T_OR_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_BIT_OR, make_var_node($1), $3)); }
    | T_IDENT T_XOR_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_BIT_XOR, make_var_node($1), $3)); }
    | T_IDENT T_LSHIFT_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_LSHIFT, make_var_node($1), $3)); }
    | T_IDENT T_RSHIFT_ASSIGN expr { $$ = make_binop_node(AST_ASSIGN, make_var_node($1), make_binop_node(AST_RSHIFT, make_var_node($1), $3)); }
    | expr T_QUESTION expr T_COLON expr { $$ = make_ternary_node($1, $3, $5); }
    | T_IDENT T_DOT T_IDENT T_ASSIGN expr { $$ = make_index_assign_node($1, make_string_node($3), $5); }
    | T_IDENT T_LBRACKET expr T_RBRACKET T_LBRACKET expr T_RBRACKET T_ASSIGN expr { $$ = make_index2d_assign_node($1, $3, $6, $9); }
    | T_IDENT T_LBRACKET expr T_RBRACKET T_ASSIGN expr { $$ = make_index_assign_node($1, $3, $6); }
    | T_NUM { $$ = make_num_node($1); }
    | T_FLOAT_LITERAL { $$ = make_float_node($1); }
    | T_LPAREN comma_expr T_RPAREN { $$ = $2; }
    | T_SIZEOF T_LPAREN T_INT_TYPE T_RPAREN { $$ = make_sizeof_type_node(TYPE_INT); }
    | T_SIZEOF T_LPAREN T_CHAR T_RPAREN { $$ = make_sizeof_type_node(TYPE_CHAR); }
    | T_SIZEOF T_LPAREN T_DOUBLE_TYPE T_RPAREN { $$ = make_sizeof_type_node(TYPE_DOUBLE); }
    | T_SIZEOF T_LPAREN ext_type T_RPAREN { $$ = make_sizeof_type_node($3); }
    | T_SIZEOF T_LPAREN T_USER_TYPE T_RPAREN {
        int found = 0;
        GincType bt = resolve_type_alias($3, &found);
        $$ = make_sizeof_type_node(found ? bt : TYPE_INT);
    }
    | T_SIZEOF T_LPAREN T_USER_TYPE T_STAR T_RPAREN { $$ = make_sizeof_type_node(TYPE_PTR); }
    | T_SIZEOF T_LPAREN T_INT_TYPE T_STAR T_RPAREN { $$ = make_sizeof_type_node(TYPE_PTR); }
    | T_SIZEOF T_LPAREN T_CHAR T_STAR T_RPAREN { $$ = make_sizeof_type_node(TYPE_PTR); }
    | T_SIZEOF T_LPAREN T_VOID T_STAR T_RPAREN { $$ = make_sizeof_type_node(TYPE_PTR); }
    | T_SIZEOF T_LPAREN expr T_RPAREN { $$ = make_sizeof_expr_node($3); }
    | T_VA_ARG T_LPAREN T_IDENT T_COMMA T_INT_TYPE T_RPAREN { $$ = make_va_arg_node($3, TYPE_INT); }
    | T_VA_ARG T_LPAREN T_IDENT T_COMMA T_DOUBLE_TYPE T_RPAREN { $$ = make_va_arg_node($3, TYPE_DOUBLE); }
    | T_VA_ARG T_LPAREN T_IDENT T_COMMA T_CHAR T_RPAREN { $$ = make_va_arg_node($3, TYPE_CHAR); }
    | T_VA_ARG T_LPAREN T_IDENT T_COMMA T_CHAR T_STAR T_RPAREN { $$ = make_va_arg_node($3, TYPE_PTR); }
    | T_VA_ARG T_LPAREN T_IDENT T_COMMA T_VOID T_STAR T_RPAREN { $$ = make_va_arg_node($3, TYPE_PTR); }
    | T_VA_ARG T_LPAREN T_IDENT T_COMMA T_STR_TYPE T_RPAREN { $$ = make_va_arg_node($3, TYPE_STR); }
    | T_VA_ARG T_LPAREN T_IDENT T_COMMA ext_type T_RPAREN { $$ = make_va_arg_node($3, $5); }
    | T_TRUE { $$ = make_num_node(1); }
    | T_FALSE { $$ = make_num_node(0); }
    | T_NULL { $$ = make_unary_node(AST_NULL, NULL); }
    | T_STR { $$ = make_str_node($1); }
    | T_STRING { $$ = make_string_node($1); }
    | T_REGEX { $$ = make_regex_node($1); }
    | T_IDENT T_LPAREN expr_list T_RPAREN { $$ = make_func_call_node($1, $3); }
    | T_IDENT T_LPAREN T_RPAREN { $$ = make_func_call_node($1, NULL); }
    | T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN expr_list T_RPAREN { $$ = make_func_call_node($3, $6); }
    | T_LPAREN T_STAR T_IDENT T_RPAREN T_LPAREN T_RPAREN { $$ = make_func_call_node($3, NULL); }
    | T_LBRACKET T_RBRACKET T_LPAREN param_list T_RPAREN T_LBRACE stmt_list T_RBRACE { $$ = make_lambda_node(TYPE_INT, $4, $7); }
    | T_LBRACKET T_RBRACKET T_LPAREN param_list T_RPAREN T_ARROW T_INT_TYPE T_LBRACE stmt_list T_RBRACE { $$ = make_lambda_node(TYPE_INT, $4, $9); }
    | T_LBRACKET T_RBRACKET T_LPAREN param_list T_RPAREN T_ARROW T_DOUBLE_TYPE T_LBRACE stmt_list T_RBRACE { $$ = make_lambda_node(TYPE_DOUBLE, $4, $9); }
    | T_LBRACKET T_RBRACKET T_LPAREN param_list T_RPAREN T_ARROW T_VOID T_LBRACE stmt_list T_RBRACE { $$ = make_lambda_node(TYPE_VOID, $4, $9); }
    | T_IDENT T_DOT T_IDENT T_LPAREN expr_list T_RPAREN { $$ = make_method_call_node($1, $3, $5); }
    | T_IDENT T_DOT T_IDENT T_LPAREN T_RPAREN { $$ = make_method_call_node($1, $3, NULL); }
    | T_IDENT T_DOT T_IDENT { $$ = make_array_access_node($1, make_string_node($3)); }
    | T_IDENT T_ARROW T_IDENT T_ASSIGN expr { $$ = make_index_assign_node($1, make_string_node($3), $5); }
    | T_IDENT T_ARROW T_IDENT T_LPAREN expr_list T_RPAREN { $$ = make_method_call_node($1, $3, $5); }
    | T_IDENT T_ARROW T_IDENT T_LPAREN T_RPAREN { $$ = make_method_call_node($1, $3, NULL); }
    | T_IDENT T_ARROW T_IDENT { $$ = make_array_access_node($1, make_string_node($3)); }
    | T_IDENT T_LBRACKET expr T_RBRACKET T_LBRACKET expr T_RBRACKET { $$ = make_array2d_access_node($1, $3, $6); }
    | T_IDENT T_LBRACKET expr T_RBRACKET { $$ = make_array_access_node($1, $3); }
    | T_IDENT { $$ = make_var_node($1); }
    | expr T_OR expr { $$ = make_binop_node(AST_LOGICAL_OR, $1, $3); }
    | expr T_AND expr { $$ = make_binop_node(AST_LOGICAL_AND, $1, $3); }
    | T_NOT expr %prec T_NOT { $$ = make_unary_node(AST_LOGICAL_NOT, $2); }
    | T_TILDE expr %prec T_TILDE { $$ = make_unary_node(AST_BIT_NOT, $2); }
    | expr T_MATCH expr { $$ = make_binop_node(AST_MATCH, $1, $3); }
    | expr T_EQ expr { $$ = make_binop_node(AST_EQ, $1, $3); }
    | expr T_NEQ expr { $$ = make_binop_node(AST_NEQ, $1, $3); }
    | expr T_LT expr { $$ = make_binop_node(AST_LT, $1, $3); }
    | expr T_LE expr { $$ = make_binop_node(AST_LE, $1, $3); }
    | expr T_GT expr { $$ = make_binop_node(AST_GT, $1, $3); }
    | expr T_GE expr { $$ = make_binop_node(AST_GE, $1, $3); }
    | expr T_PLUS expr { $$ = make_binop_node(AST_ADD, $1, $3); }
    | expr T_MINUS expr { $$ = make_binop_node(AST_SUB, $1, $3); }
    | T_MINUS expr %prec UMINUS { $$ = make_unary_node(AST_UNARY_MINUS, $2); }
    | expr T_STAR expr { $$ = make_binop_node(AST_MUL, $1, $3); }
    | expr T_SLASH expr { $$ = make_binop_node(AST_DIV, $1, $3); }
    | expr T_MOD expr { $$ = make_binop_node(AST_MOD, $1, $3); }
    | expr T_AMP expr { $$ = make_binop_node(AST_BIT_AND, $1, $3); }
    | expr T_BIT_OR_TOK expr { $$ = make_binop_node(AST_BIT_OR, $1, $3); }
    | expr T_XOR expr { $$ = make_binop_node(AST_BIT_XOR, $1, $3); }
    | expr T_LSHIFT expr { $$ = make_binop_node(AST_LSHIFT, $1, $3); }
    | expr T_RSHIFT expr { $$ = make_binop_node(AST_RSHIFT, $1, $3); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "%s:%d:%d: Ga! (Parse Error): %s\n", current_filename, current_line, current_column, s);
}
