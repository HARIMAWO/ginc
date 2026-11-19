#include "ginc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
	GincEngine* eng = ginc_create();
	int verbose = 1;
	int script_arg_idx = -1;

	for (int i = 1; i < argc; i++) {
		if (strncmp(argv[i], "-I", 2) == 0) {
			char const* dir = argv[i] + 2;
			if (*dir == '\0' && i + 1 < argc) dir = argv[++i];
			if (*dir) ginc_add_include_dir(eng, dir);
		} else if (strncmp(argv[i], "-L", 2) == 0) {
			char const* dir = argv[i] + 2;
			if (*dir == '\0' && i + 1 < argc) dir = argv[++i];
			if (*dir) ginc_add_lib_dir(eng, dir);
		} else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
			verbose = 0;
		} else {
			script_arg_idx = i;
			break;
		}
	}

	ginc_set_verbose(eng, verbose);

	int ret = 0;
	if (script_arg_idx > 0) {
		ret = ginc_eval_file(eng, argv[script_arg_idx], argc - script_arg_idx, argv + script_arg_idx);
	} else {
		size_t capacity = 1024;
		size_t size = 0;
		char* buffer = malloc(capacity);
		if (!buffer) {
			fprintf(stderr, "Ga! (Memory Error): Failed to allocate memory\n");
			ginc_destroy(eng);
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
					ginc_destroy(eng);
					return 1;
				}
				buffer = new_buf;
			}
		}
		buffer[size] = '\0';
		ret = ginc_eval_string(eng, buffer, "<stdin>", argc, argv);
		free(buffer);
	}

	ginc_destroy(eng);
	return (ret != 0 && !verbose) ? ret : 0;
}
