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
#include "ast.h"
#include "codegen.h"
#include "cpp.h"
#include "ginc_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

extern int yyparse(void);
extern char const* cursor;
extern char const* limit;
extern ASTNode* root_node;
extern void ginc_set_args(int argc, char** argv);
extern void ginc_set_exe_dir(char const* dir);

char const* current_filename = "<stdin>";
int current_line = 1;
int current_column = 1;

#define MAX_INC_DIRS 64

struct GincEngine {
	char* include_dirs[MAX_INC_DIRS];
	int include_dir_count;
	int verbose;
};

static void detect_exe_dir(char const* const argv0, char* const out_dir, size_t const cap) {
	char path_buf[1024] = {0};
#ifdef __FreeBSD__
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
	size_t len = sizeof(path_buf) - 1;
	if (sysctl(mib, 4, path_buf, &len, NULL, 0) != 0) {
		path_buf[0] = '\0';
	}
#elif defined(__linux__)
	ssize_t const n = readlink("/proc/self/exe", path_buf, sizeof(path_buf) - 1);
	if (n > 0) path_buf[n] = '\0';
	else path_buf[0] = '\0';
#endif
	if (path_buf[0] == '\0' && argv0) {
		snprintf(path_buf, sizeof(path_buf), "%s", argv0);
	}
	char* const slash = strrchr(path_buf, '/');
	if (slash) {
		*slash = '\0';
		snprintf(out_dir, cap, "%s", path_buf);
	} else {
		snprintf(out_dir, cap, ".");
	}
}

static void add_colon_dirs(GincEngine* const eng, char const* const env_val) {
	if (!env_val || !*env_val) return;
	char* copy = strdup(env_val);
	char* tok = strtok(copy, ":");
	while (tok && eng->include_dir_count < MAX_INC_DIRS) {
		if (*tok) ginc_add_include_dir(eng, tok);
		tok = strtok(NULL, ":");
	}
	free(copy);
}

static void init_default_paths(GincEngine* const eng, char const* const argv0) {
	char exe_dir[1024];
	detect_exe_dir(argv0, exe_dir, sizeof(exe_dir));
	ginc_set_exe_dir(exe_dir);

	add_colon_dirs(eng, getenv("GINC_INCLUDE_PATH"));
	add_colon_dirs(eng, getenv("GINC_INCLUDE"));

	if (exe_dir[0]) {
		char rel1[1024];
		char rel2[1024];
		char rel3[1024];
		snprintf(rel1, sizeof(rel1), "%s/std", exe_dir);
		snprintf(rel2, sizeof(rel2), "%s/../lib/ginc/include", exe_dir);
		snprintf(rel3, sizeof(rel3), "%s/../ginc/include", exe_dir);
		ginc_add_include_dir(eng, rel1);
		ginc_add_include_dir(eng, rel2);
		ginc_add_include_dir(eng, rel3);
	}
	ginc_add_include_dir(eng, "./include");
	ginc_add_include_dir(eng, "./std");

	char const* const home = getenv("HOME");
	if (home && *home) {
		char host_buf[256] = {0};
		if (gethostname(host_buf, sizeof(host_buf) - 1) == 0 && host_buf[0]) {
			char* const dot = strchr(host_buf, '.');
			if (dot) *dot = '\0';
			char host_inc[1024];
			snprintf(host_inc, sizeof(host_inc), "%s/.local/%s/ginc/include", home, host_buf);
			ginc_add_include_dir(eng, host_inc);
		}
		char local_inc[1024];
		snprintf(local_inc, sizeof(local_inc), "%s/.local/ginc/include", home);
		ginc_add_include_dir(eng, local_inc);
	}

	ginc_add_include_dir(eng, GINC_DEFAULT_INCDIR);
}

GincEngine* ginc_create(void) {
	GincEngine* eng = calloc(1, sizeof(GincEngine));
	init_default_paths(eng, NULL);
	return eng;
}

void ginc_destroy(GincEngine* const eng) {
	if (!eng) return;
	for (int i = 0; i < eng->include_dir_count; i++) {
		free(eng->include_dirs[i]);
	}
	free(eng);
}

void ginc_add_include_dir(GincEngine* const eng, char const* const dir) {
	if (!eng || !dir || !*dir || eng->include_dir_count >= MAX_INC_DIRS) return;
	eng->include_dirs[eng->include_dir_count++] = strdup(dir);
}

void ginc_add_lib_dir(GincEngine* const eng, char const* const dir) {
	(void)eng;
	extern void ginc_add_lib_dir_impl(char const* dir);
	ginc_add_lib_dir_impl(dir);
}

void ginc_set_verbose(GincEngine* const eng, int const verbose) {
	if (eng) eng->verbose = verbose;
}

static int run_preprocessed(GincEngine* const eng, char* const pp_buf, char const* const filename, int const argc, char** const argv) {
	ginc_set_args(argc, argv);
	current_filename = filename ? filename : "<stdin>";
	current_line = 1;
	current_column = 1;
	reset_type_aliases();
	root_node = NULL;

	cursor = pp_buf;
	limit = pp_buf + strlen(pp_buf);

	if (eng->verbose) {
		printf("GINC (Ga! Is Never Called) Compiler\n");
	}
	int exit_code = 0;
	if (yyparse() == 0) {
		if (eng->verbose) {
			printf("Parse successful!\n");
			printf("--- AST Dump ---\n");
			print_ast(root_node, 0);
		}
		if (generate_code_ex(root_node, eng->verbose, &exit_code) != 0) {
			exit_code = 1;
		}
	} else {
		exit_code = 1;
	}
	free(pp_buf);
	return exit_code;
}

int ginc_eval_file(GincEngine* const eng, char const* const filename, int const argc, char** const argv) {
	if (!eng || !filename) return 1;
	char* pp = ginc_preprocess_file(filename, (char const* const*)eng->include_dirs, eng->include_dir_count);
	return run_preprocessed(eng, pp, filename, argc, argv);
}

int ginc_eval_string(GincEngine* const eng, char const* const src, char const* const filename, int const argc, char** const argv) {
	if (!eng || !src) return 1;
	char* pp = ginc_preprocess_string(src, filename ? filename : "<string>", (char const* const*)eng->include_dirs, eng->include_dir_count);
	return run_preprocessed(eng, pp, filename ? filename : "<string>", argc, argv);
}
