#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

extern int yyparse();
extern const char *cursor;
extern const char *limit;
extern ASTNode* root_node;

int main(int argc, char **argv) {
    // パースするソースコード
    const char* source = "count = 10 + 20; print(count);";
    cursor = source;
    limit = source + strlen(source);

    printf("GINC (Ga! Is Never Called) Compiler\n");
    if (yyparse() == 0) {
        printf("Parse successful!\n");
        printf("--- AST Dump ---\n");
        print_ast(root_node, 0); // ★ASTを出力

		// ★LLVM IRの生成処理を呼び出す
		generate_code(root_node);
    }
    
    return 0;
}
