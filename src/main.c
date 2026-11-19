/*
 * Copyright 2026 HARIMAWO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "ginc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GINC_VERSION "0.1.0"

static void print_version(void) {
	printf("GINC (Ga! Is Never Called) %s\n", GINC_VERSION);
	printf("Copyright 2026 HARIMAWO\n");
	printf("License: Apache License 2.0 <http://www.apache.org/licenses/LICENSE-2.0>\n");
	printf("Powered by LLVM (Apache-2.0 WITH LLVM-exception)\n");
}

static void print_usage(char const* const prog) {
	printf("Usage: %s [options] [script.ginc [args...]]\n", prog);
	printf("Options:\n");
	printf("  -I<dir>        Add header include search directory\n");
	printf("  -L<dir>        Add shared library search directory\n");
	printf("  -d, --dump     Show AST, LLVM IR dumps, and JIT result (or set GINC_DUMP=1)\n");
	printf("  -q, --quiet    Suppress dumps (default)\n");
	printf("  -v, --version  Show version and license information\n");
	printf("  -h, --help     Show this help message\n");
}

int main(int argc, char** argv) {
	GincEngine* eng = ginc_create();
	int verbose = 0;
	char const* const env_dump = getenv("GINC_DUMP");
	if (env_dump && strcmp(env_dump, "1") == 0) {
		verbose = 1;
	}
	int script_arg_idx = -1;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-V") == 0) {
			print_version();
			ginc_destroy(eng);
			return 0;
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			ginc_destroy(eng);
			return 0;
		} else if (strncmp(argv[i], "-I", 2) == 0) {
			char const* dir = argv[i] + 2;
			if (*dir == '\0' && i + 1 < argc) dir = argv[++i];
			if (*dir) ginc_add_include_dir(eng, dir);
		} else if (strncmp(argv[i], "-L", 2) == 0) {
			char const* dir = argv[i] + 2;
			if (*dir == '\0' && i + 1 < argc) dir = argv[++i];
			if (*dir) ginc_add_lib_dir(eng, dir);
		} else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dump") == 0 || strcmp(argv[i], "--verbose") == 0) {
			verbose = 1;
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
	return ret;
}
