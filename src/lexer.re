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
        LINE_DIRECTIVE = [ \t]* "#" ("line")? [ \t]* [0-9]+ [ \t]* "\"" [^"\x00\n]* "\"" [^\x00\n]*;
        WS = [ \t\r\n]+;
        LINE_COMMENT = "//" [^\x00\n]*;
        BLOCK_COMMENT = "/*" ([^*] | ("*" [^/]))* "*"+ "/";
        NUM = [0-9]+;
        FLOAT = [0-9]+ "." [0-9]+;
        IDENT = [a-zA-Z_][a-zA-Z0-9_]*;
        STR = "\"" [^"\x00\n]* "\"";
        REGEX = "/" [^/\x00\n]+ "/";

        * {
            fprintf(stderr, "%s:%d:%d: Ga! (Lex Error): Unknown token '%c'\n", current_filename, current_line, current_column, *tok);
            exit(1);
        }
        "\x00" { return 0; }
        SHEBANG { update_location(tok, cursor); continue; }
        LINE_DIRECTIVE {
            int line_num = 1;
            char filename_buf[512] = {0};
            char const* p = tok;

            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#') p++;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "line", 4) == 0) p += 4;
            while (*p == ' ' || *p == '\t') p++;

            if (sscanf(p, "%d \"%511[^\"]\"", &line_num, filename_buf) == 2) {
                current_line = line_num;
                current_filename = strdup(filename_buf);
                current_column = 1;
            } else if (sscanf(p, "%d", &line_num) == 1) {
                current_line = line_num;
                current_column = 1;
            }
            continue;
        }
        WS { update_location(tok, cursor); continue; }
        LINE_COMMENT { update_location(tok, cursor); continue; }
        BLOCK_COMMENT { update_location(tok, cursor); continue; }

        FLOAT {
            update_location(tok, cursor);
            yylval.float_val = atof(tok);
            return T_FLOAT_LITERAL;
        }
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
        "true" { update_location(tok, cursor); return T_TRUE; }
        "false" { update_location(tok, cursor); return T_FALSE; }
        "null" { update_location(tok, cursor); return T_NULL; }
        "typedef" { update_location(tok, cursor); return T_TYPEDEF; }
        "var" { update_location(tok, cursor); return T_VAR_TYPE; }
        "int" { update_location(tok, cursor); return T_INT_TYPE; }
        "double" { update_location(tok, cursor); return T_DOUBLE_TYPE; }
        "str" { update_location(tok, cursor); return T_STR_TYPE; }
        "void" { update_location(tok, cursor); return T_VOID; }
        "char" { update_location(tok, cursor); return T_CHAR; }
        "FILE" { update_location(tok, cursor); return T_FILE; }
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
        "++" { update_location(tok, cursor); return T_INC; }
        "--" { update_location(tok, cursor); return T_DEC; }
        "do" { update_location(tok, cursor); return T_DO; }
        "break" { update_location(tok, cursor); return T_BREAK; }
        "continue" { update_location(tok, cursor); return T_CONTINUE; }
        "while" { update_location(tok, cursor); return T_WHILE; }
        "for" { update_location(tok, cursor); return T_FOR; }
        "return" { update_location(tok, cursor); return T_RETURN; }
        "print" { update_location(tok, cursor); return T_PRINT; }
        IDENT {
            update_location(tok, cursor);
            yylval.str = strndup(tok, cursor - tok);
            if (is_typedef_name(yylval.str)) {
                return T_USER_TYPE;
            }
            return T_IDENT;
        }
        "&&" { update_location(tok, cursor); return T_AND; }
        "||" { update_location(tok, cursor); return T_OR; }
        "!=" { update_location(tok, cursor); return T_NEQ; }
        "!" { update_location(tok, cursor); return T_NOT; }
        "==" { update_location(tok, cursor); return T_EQ; }
        "=" { update_location(tok, cursor); return T_ASSIGN; }
        ">=" { update_location(tok, cursor); return T_GE; }
        "<=" { update_location(tok, cursor); return T_LE; }
        ">>" { update_location(tok, cursor); return T_RSHIFT; }
        "<<" { update_location(tok, cursor); return T_LSHIFT; }
        ">" { update_location(tok, cursor); return T_GT; }
        "<" { update_location(tok, cursor); return T_LT; }
        "%" { update_location(tok, cursor); return T_MOD; }
        "&" { update_location(tok, cursor); return T_AMP; }
        "|" { update_location(tok, cursor); return T_BIT_OR_TOK; }
        "^" { update_location(tok, cursor); return T_XOR; }
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
