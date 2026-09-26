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
		FILE* fp = fopen(argv[1], "rb");
		if (!fp) {
			fprintf(stderr, "Ga! (File Error): Cannot open file '%s'\n", argv[1]);
			return 1;
		}
		fseek(fp, 0, SEEK_END);
		size = (size_t)ftell(fp);
		fseek(fp, 0, SEEK_SET);

		buffer = malloc(size + 1);
		if (!buffer) {
			fprintf(stderr, "Ga! (Memory Error): Failed to allocate memory\n");
			fclose(fp);
			return 1;
		}

		if (fread(buffer, 1, size, fp) != size) {
			fprintf(stderr, "Ga! (File Error): Failed to read file '%s'\n", argv[1]);
			free(buffer);
			fclose(fp);
			return 1;
		}
		fclose(fp);
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
