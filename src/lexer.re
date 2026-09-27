#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "parser.h"

const char *cursor;
const char *limit;

static void update_location(char const* tok, char const* cur) {
	for (char const* p = tok; p < cur; p++) {
		if (*p == '\n') {
			current_line++;
			current_column = 1;
		} else {
			current_column++;
		}
	}
}

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

        SHEBANG = "#!" [^\x00\n]*;
        WS = [ \t\r\n]+;
        LINE_COMMENT = "//" [^\x00\n]*;
        BLOCK_COMMENT = "/*" ([^*] | ("*" [^/]))* "*"+ "/";
        NUM = [0-9]+;
        IDENT = [a-zA-Z_][a-zA-Z0-9_]*;
        STR = "\"" [^"\x00\n]* "\"";
        REGEX = "/" [^/\x00\n]+ "/";

        * {
            fprintf(stderr, "%s:%d:%d: Ga! (Lex Error): Unknown token '%c'\n", current_filename, current_line, current_column, *tok);
            exit(1);
        }
        "\x00" { return 0; }
        SHEBANG { update_location(tok, cursor); continue; }
        WS { update_location(tok, cursor); continue; }
        LINE_COMMENT { update_location(tok, cursor); continue; }
        BLOCK_COMMENT { update_location(tok, cursor); continue; }

        NUM {
            update_location(tok, cursor);
            // strtolは数字以外の文字(スペースやセミコロン)で勝手に止まるため安全
            yylval.num = strtol(tok, NULL, 10);
            return T_NUM;
        }
        STR {
            update_location(tok, cursor);
            // 前後のダブルクォーテーションを除外して複製
            yylval.str = strndup(tok + 1, cursor - tok - 2);
            return T_STRING;
        }
        REGEX {
            update_location(tok, cursor);
            // 前後のスラッシュを除外して複製
            yylval.str = strndup(tok + 1, cursor - tok - 2);
            return T_REGEX;
        }
        "=~" { update_location(tok, cursor); return T_MATCH; }
        "var" { update_location(tok, cursor); return T_VAR_TYPE; }
        "int" { update_location(tok, cursor); return T_INT_TYPE; }
        "str" { update_location(tok, cursor); return T_STR_TYPE; }
        "class" { update_location(tok, cursor); return T_CLASS; }
        "struct" { update_location(tok, cursor); return T_STRUCT; }
        "public" { update_location(tok, cursor); return T_PUBLIC; }
        "private" { update_location(tok, cursor); return T_PRIVATE; }
        ":" { update_location(tok, cursor); return T_COLON; }
        "try" { update_location(tok, cursor); return T_TRY; }
        "catch" { update_location(tok, cursor); return T_CATCH; }
        "finally" { update_location(tok, cursor); return T_FINALLY; }
        "throw" { update_location(tok, cursor); return T_THROW; }
        "if" { update_location(tok, cursor); return T_IF; }
        "else" { update_location(tok, cursor); return T_ELSE; }
        "while" { update_location(tok, cursor); return T_WHILE; }
        "return" { update_location(tok, cursor); return T_RETURN; }
        "print" { update_location(tok, cursor); return T_PRINT; }
        IDENT {
            update_location(tok, cursor);
            // 変数名はASTに持たせるため、tokから差分(文字数)だけ複製
            yylval.str = strndup(tok, cursor - tok); 
            return T_IDENT;
        }
        "==" { update_location(tok, cursor); return T_EQ; }
        "=" { update_location(tok, cursor); return T_ASSIGN; }
        "<" { update_location(tok, cursor); return T_LT; }
        "+" { update_location(tok, cursor); return T_PLUS; }
        "-" { update_location(tok, cursor); return T_MINUS; }
        "*" { update_location(tok, cursor); return T_STAR; }
        "/" { update_location(tok, cursor); return T_SLASH; }
        "." { update_location(tok, cursor); return T_DOT; }
        ";" { update_location(tok, cursor); return T_SEMI; }
        "(" { update_location(tok, cursor); return T_LPAREN; }
        ")" { update_location(tok, cursor); return T_RPAREN; }
        "{" { update_location(tok, cursor); return T_LBRACE; }
        "}" { update_location(tok, cursor); return T_RBRACE; }
        "[" { update_location(tok, cursor); return T_LBRACKET; }
        "]" { update_location(tok, cursor); return T_RBRACKET; }
        "," { update_location(tok, cursor); return T_COMMA; }
        */
    }
}
