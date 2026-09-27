#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

extern int yyparse();
extern const char *cursor;
extern const char *limit;
extern ASTNode* root_node;

char const* current_filename = "<stdin>";
int current_line = 1;
int current_column = 1;

int main(int argc, char **argv) {
	char* buffer = NULL;
	size_t size = 0;

	if (argc > 1) {
		current_filename = argv[1];

		// ファイル先頭のShebang (#!...) をチェックし、プリプロセッサエラーを防止する
		FILE* check_fp = fopen(argv[1], "rb");
		int has_shebang = 0;
		if (check_fp) {
			char first_two[2] = {0};
			if (fread(first_two, 1, 2, check_fp) == 2) {
				if (first_two[0] == '#' && first_two[1] == '!') {
					has_shebang = 1;
				}
			}
			fclose(check_fp);
		}

		char cmd[1024];
		if (has_shebang) {
			// Shebangがある場合は1行目をスキップして標準入力経由でclang -Eに渡す
			snprintf(cmd, sizeof(cmd), "tail -n +2 %s | clang -E -x c -", argv[1]);
		} else {
			snprintf(cmd, sizeof(cmd), "clang -E -x c %s", argv[1]);
		}

		FILE* pipe = popen(cmd, "r");
		if (!pipe) {
			fprintf(stderr, "Ga! (CPP Error): Failed to run preprocessor for '%s'\n", argv[1]);
			return 1;
		}

		size_t capacity = 4096;
		buffer = malloc(capacity);
		if (!buffer) {
			fprintf(stderr, "Ga! (Memory Error): Failed to allocate memory\n");
			pclose(pipe);
			return 1;
		}

		size_t nread = 0;
		while ((nread = fread(buffer + size, 1, capacity - size, pipe)) > 0) {
			size += nread;
			if (size >= capacity) {
				capacity *= 2;
				char* new_buf = realloc(buffer, capacity);
				if (!new_buf) {
					fprintf(stderr, "Ga! (Memory Error): Failed to reallocate memory\n");
					free(buffer);
					pclose(pipe);
					return 1;
				}
				buffer = new_buf;
			}
		}
		pclose(pipe);
	} else {
		size_t capacity = 1024;
		buffer = malloc(capacity);
		if (!buffer) {
			fprintf(stderr, "Ga! (Memory Error): Failed to allocate memory\n");
			return 1;
		}

		size_t nread = 0;
		while ((nread = fread(buffer + size, 1, capacity - size, stdin)) > 0) {
			size += nread;
			if (size >= capacity) {
				capacity *= 2;
				char* new_buf = realloc(buffer, capacity);
				if (!new_buf) {
					fprintf(stderr, "Ga! (Memory Error): Failed to reallocate memory\n");
					free(buffer);
					return 1;
				}
				buffer = new_buf;
			}
		}
	}

	buffer[size] = '\0';
	cursor = buffer;
	limit = buffer + size;

	printf("GINC (Ga! Is Never Called) Compiler\n");
	if (yyparse() == 0) {
		printf("Parse successful!\n");
		printf("--- AST Dump ---\n");
		print_ast(root_node, 0);

		generate_code(root_node);
	}

	free(buffer);
	return 0;
}
