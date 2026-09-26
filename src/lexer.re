#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "parser.h"

const char *cursor;
const char *limit;

int yylex() {
    const char *marker;
	(void)marker;
    while (1) {
        // ★ ここでトークンの開始位置を記録します
        const char *tok = cursor; 

        /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:define:YYCURSOR = cursor;
        re2c:define:YYMARKER = marker;
        re2c:define:YYLIMIT = limit;
        re2c:yyfill:enable = 0;

        WS = [ \t\r\n]+;
        NUM = [0-9]+;
        IDENT = [a-zA-Z_][a-zA-Z0-9_]*;

        * { printf("Ga! (Lex Error): Unknown token\n"); exit(1); }
        "\x00" { return 0; }
        WS { continue; }

        NUM {
            // strtolは数字以外の文字(スペースやセミコロン)で勝手に止まるため安全
            yylval.num = strtol(tok, NULL, 10);
            return T_NUM;
        }
        "print" { return T_PRINT; }
        IDENT {
            // 変数名はASTに持たせるため、tokから差分(文字数)だけ複製
            yylval.str = strndup(tok, cursor - tok); 
            return T_IDENT;
        }
        "=" { return T_ASSIGN; }
        "+" { return T_PLUS; }
        "-" { return T_MINUS; }
        ";" { return T_SEMI; }
        "(" { return T_LPAREN; }
        ")" { return T_RPAREN; }
        */
    }
}
