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
    int num;
    double float_val;
    char* str;
    ASTNode* ast;
}

%token <num> T_NUM
%token <float_val> T_FLOAT_LITERAL
%token <str> T_IDENT T_STR T_STRING T_REGEX
%token T_INT_TYPE T_STR_TYPE T_VAR_TYPE T_DOUBLE_TYPE T_ASSIGN T_PLUS T_MINUS T_STAR T_SLASH T_SEMI T_PRINT T_LPAREN T_RPAREN T_IF T_ELSE T_WHILE T_LBRACE T_RBRACE T_EQ T_LT T_MATCH T_LBRACKET T_RBRACKET T_COMMA T_RETURN T_TRY T_CATCH T_THROW T_FINALLY T_DOT T_CLASS T_STRUCT T_PUBLIC T_PRIVATE T_COLON T_TRUE T_FALSE T_NULL

%type <ast> stmt_list stmt expr expr_list param_list param_decl member_decl member_list class_def class_member class_item class_member_list finally_clause func_body catch_block catch_list

%left T_MATCH
%left T_EQ T_LT
%left T_PLUS T_MINUS
%left T_STAR T_SLASH
%right UMINUS

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

param_decl:
    T_INT_TYPE T_IDENT { $$ = make_param_decl_node(TYPE_INT, $2); }
  | T_STR_TYPE T_IDENT { $$ = make_param_decl_node(TYPE_STR, $2); }
  | T_VAR_TYPE T_IDENT { $$ = make_param_decl_node(TYPE_VAR, $2); }
  | T_DOUBLE_TYPE T_IDENT { $$ = make_param_decl_node(TYPE_DOUBLE, $2); }
;

param_list:
    /* empty */ { $$ = NULL; }
    | param_decl { $$ = make_expr_list_node($1, NULL); }
    | param_decl T_COMMA param_list { $$ = make_expr_list_node($1, $3); }
    ;

member_decl:
    T_INT_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_INT, $2); }
    | T_STR_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_STR, $2); }
    | T_VAR_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_VAR, $2); }
    | T_DOUBLE_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_DOUBLE, $2); }
    ;

member_list:
    /* empty */ { $$ = NULL; }
    | member_decl member_list { $$ = make_expr_list_node($1, $2); }
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
    T_INT_TYPE T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_INT, $2, $4, $6);
    }
    | T_STR_TYPE T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_STR, $2, $4, $6);
    }
    | T_DOUBLE_TYPE T_IDENT T_LPAREN param_list T_RPAREN func_body {
        $$ = make_func_decl_node(TYPE_DOUBLE, $2, $4, $6);
    }
    | T_RETURN expr T_SEMI {
        $$ = make_return_node($2);
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
    | T_IDENT T_DOT T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_index_assign_node($1, make_string_node($3), $5);
    }
    | T_INT_TYPE T_IDENT T_LBRACKET T_STR_TYPE T_RBRACKET T_SEMI {
        $$ = make_hash_decl_node(TYPE_INT, $2, TYPE_STR);
    }
    | T_VAR_TYPE T_IDENT T_LBRACKET T_STR_TYPE T_RBRACKET T_SEMI {
        $$ = make_hash_decl_node(TYPE_VAR, $2, TYPE_STR);
    }
    | T_IDENT T_LBRACKET expr T_RBRACKET T_ASSIGN expr T_SEMI {
        $$ = make_index_assign_node($1, $3, $6);
    }
    | T_INT_TYPE T_IDENT T_LBRACKET T_NUM T_RBRACKET T_ASSIGN T_LBRACE expr_list T_RBRACE T_SEMI {
        $$ = make_array_decl_node(TYPE_INT, $2, $4, $8);
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
    | T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_binop_node(AST_ASSIGN, make_var_node($1), $3);
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
    | expr T_SEMI {
        $$ = $1;
    }
    ;

expr:
    T_NUM { $$ = make_num_node($1); }
    | T_FLOAT_LITERAL { $$ = make_float_node($1); }
    | T_TRUE { $$ = make_num_node(1); }
    | T_FALSE { $$ = make_num_node(0); }
    | T_NULL { $$ = make_unary_node(AST_NULL, NULL); }
    | T_STR { $$ = make_str_node($1); }
    | T_STRING { $$ = make_string_node($1); }
    | T_REGEX { $$ = make_regex_node($1); }
    | T_IDENT T_LPAREN expr_list T_RPAREN { $$ = make_func_call_node($1, $3); }
    | T_IDENT T_LPAREN T_RPAREN { $$ = make_func_call_node($1, NULL); }
    | T_IDENT T_DOT T_IDENT T_LPAREN expr_list T_RPAREN { $$ = make_method_call_node($1, $3, $5); }
    | T_IDENT T_DOT T_IDENT T_LPAREN T_RPAREN { $$ = make_method_call_node($1, $3, NULL); }
    | T_IDENT T_DOT T_IDENT { $$ = make_array_access_node($1, make_string_node($3)); }
    | T_IDENT T_LBRACKET expr T_RBRACKET { $$ = make_array_access_node($1, $3); }
    | T_IDENT { $$ = make_var_node($1); }
    | expr T_MATCH expr { $$ = make_binop_node(AST_MATCH, $1, $3); }
    | expr T_EQ expr { $$ = make_binop_node(AST_EQ, $1, $3); }
    | expr T_LT expr { $$ = make_binop_node(AST_LT, $1, $3); }
    | expr T_PLUS expr { $$ = make_binop_node(AST_ADD, $1, $3); }
    | expr T_MINUS expr { $$ = make_binop_node(AST_SUB, $1, $3); }
    | T_MINUS expr %prec UMINUS { $$ = make_unary_node(AST_UNARY_MINUS, $2); }
    | expr T_STAR expr { $$ = make_binop_node(AST_MUL, $1, $3); }
    | expr T_SLASH expr { $$ = make_binop_node(AST_DIV, $1, $3); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "%s:%d:%d: Ga! (Parse Error): %s\n", current_filename, current_line, current_column, s);
}
