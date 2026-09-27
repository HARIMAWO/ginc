%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

int yylex(void);
void yyerror(const char *s);
ASTNode* root_node;
%}

%union {
    int num;
    char* str;
    ASTNode* ast;
}

%token <num> T_NUM
%token <str> T_IDENT T_STR T_STRING T_REGEX
%token T_INT_TYPE T_STR_TYPE T_VAR_TYPE T_ASSIGN T_PLUS T_MINUS T_STAR T_SLASH T_SEMI T_PRINT T_LPAREN T_RPAREN T_IF T_ELSE T_WHILE T_LBRACE T_RBRACE T_EQ T_LT T_MATCH T_LBRACKET T_RBRACKET T_COMMA T_RETURN T_TRY T_CATCH T_THROW T_FINALLY T_DOT T_CLASS

%type <ast> stmt_list stmt expr expr_list param_list member_decl member_list finally_clause

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

param_list:
    /* empty */ { $$ = NULL; }
    | T_INT_TYPE T_IDENT { $$ = make_expr_list_node(make_param_decl_node(TYPE_INT, $2), NULL); }
    | T_STR_TYPE T_IDENT { $$ = make_expr_list_node(make_param_decl_node(TYPE_STR, $2), NULL); }
    | T_INT_TYPE T_IDENT T_COMMA param_list { $$ = make_expr_list_node(make_param_decl_node(TYPE_INT, $2), $4); }
    | T_STR_TYPE T_IDENT T_COMMA param_list { $$ = make_expr_list_node(make_param_decl_node(TYPE_STR, $2), $4); }
    ;

member_decl:
    T_INT_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_INT, $2); }
    | T_STR_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_STR, $2); }
    | T_VAR_TYPE T_IDENT T_SEMI { $$ = make_param_decl_node(TYPE_VAR, $2); }
    ;

member_list:
    /* empty */ { $$ = NULL; }
    | member_decl member_list { $$ = make_expr_list_node($1, $2); }
    ;

finally_clause:
    /* empty */ { $$ = NULL; }
    | T_FINALLY T_LBRACE stmt_list T_RBRACE { $$ = $3; }
    ;

stmt:
    T_INT_TYPE T_IDENT T_LPAREN param_list T_RPAREN T_LBRACE stmt_list T_RBRACE {
        $$ = make_func_decl_node(TYPE_INT, $2, $4, $7);
    }
    | T_STR_TYPE T_IDENT T_LPAREN param_list T_RPAREN T_LBRACE stmt_list T_RBRACE {
        $$ = make_func_decl_node(TYPE_STR, $2, $4, $7);
    }
    | T_RETURN expr T_SEMI {
        $$ = make_return_node($2);
    }
    | T_TRY T_LBRACE stmt_list T_RBRACE T_CATCH T_LPAREN T_IDENT T_RPAREN T_LBRACE stmt_list T_RBRACE finally_clause {
        $$ = make_try_catch_node($3, $7, $10, $12);
    }
    | T_TRY stmt T_CATCH T_LPAREN T_IDENT T_RPAREN stmt {
        $$ = make_try_node($2, $5, $7);
    }
    | T_THROW expr T_SEMI {
        $$ = make_throw_node($2);
    }
    | T_CLASS T_IDENT T_LBRACE member_list T_RBRACE T_SEMI {
        $$ = make_class_def_node($2, $4);
    }
    | T_CLASS T_IDENT T_IDENT T_SEMI {
        $$ = make_hash_decl_node(TYPE_INT, $3, TYPE_STR);
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
    | T_STR { $$ = make_str_node($1); }
    | T_STRING { $$ = make_string_node($1); }
    | T_REGEX { $$ = make_regex_node($1); }
    | T_IDENT T_LPAREN expr_list T_RPAREN { $$ = make_func_call_node($1, $3); }
    | T_IDENT T_LPAREN T_RPAREN { $$ = make_func_call_node($1, NULL); }
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
