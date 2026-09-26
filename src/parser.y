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
%token <str> T_IDENT
%token T_ASSIGN T_PLUS T_MINUS T_SEMI T_PRINT T_LPAREN T_RPAREN

%type <ast> stmt_list stmt expr

%left T_PLUS T_MINUS

%%

program:
    stmt_list { root_node = $1; }
    ;

stmt_list:
    stmt_list stmt { $$ = make_binop_node(AST_SEQ, $1, $2); }
    | stmt         { $$ = $1; }
    ;

stmt:
    T_IDENT T_ASSIGN expr T_SEMI {
        $$ = make_binop_node(AST_ASSIGN, make_var_node($1), $3);
    }
    | T_PRINT T_LPAREN expr T_RPAREN T_SEMI {
        $$ = make_print_node($3);
    }
    | expr T_SEMI {
        $$ = $1;
    }
    ;

expr:
    T_NUM { $$ = make_num_node($1); }
    | T_IDENT { $$ = make_var_node($1); }
    | expr T_PLUS expr { $$ = make_binop_node(AST_ADD, $1, $3); }
    | expr T_MINUS expr { $$ = make_binop_node(AST_SUB, $1, $3); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Ga! (Parse Error): %s\n", s);
}
