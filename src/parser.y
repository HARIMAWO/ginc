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
%token <str> T_IDENT T_STR T_STRING
%token T_ASSIGN T_PLUS T_MINUS T_SEMI T_PRINT T_LPAREN T_RPAREN T_IF T_ELSE T_WHILE T_LBRACE T_RBRACE T_EQ T_LT

%type <ast> stmt_list stmt expr

%left T_EQ T_LT
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
    | T_IDENT { $$ = make_var_node($1); }
    | expr T_EQ expr { $$ = make_binop_node(AST_EQ, $1, $3); }
    | expr T_LT expr { $$ = make_binop_node(AST_LT, $1, $3); }
    | expr T_PLUS expr { $$ = make_binop_node(AST_ADD, $1, $3); }
    | expr T_MINUS expr { $$ = make_binop_node(AST_SUB, $1, $3); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Ga! (Parse Error): %s\n", s);
}
