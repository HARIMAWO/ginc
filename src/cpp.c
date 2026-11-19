/*
 * Copyright 2026 HARIMAWO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "cpp.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void ginc_load_library(char const* path);

/* --- 動的文字列バッファ --- */
typedef struct {
	char* data;
	size_t len;
	size_t cap;
} StrBuf;

static void sb_init(StrBuf* const sb) {
	sb->cap = 4096;
	sb->len = 0;
	sb->data = malloc(sb->cap);
	if (!sb->data) {
		fprintf(stderr, "Ga! (CPP Error): Out of memory\n");
		exit(1);
	}
	sb->data[0] = '\0';
}

static void sb_ensure(StrBuf* const sb, size_t const extra) {
	if (sb->len + extra + 1 <= sb->cap) return;
	while (sb->len + extra + 1 > sb->cap) {
		sb->cap *= 2;
	}
	char* new_data = realloc(sb->data, sb->cap);
	if (!new_data) {
		fprintf(stderr, "Ga! (CPP Error): Out of memory\n");
		exit(1);
	}
	sb->data = new_data;
}

static void sb_append_char(StrBuf* const sb, char const c) {
	sb_ensure(sb, 1);
	sb->data[sb->len++] = c;
	sb->data[sb->len] = '\0';
}

static void sb_append_n(StrBuf* const sb, char const* const s, size_t const n) {
	if (n == 0) return;
	sb_ensure(sb, n);
	memcpy(sb->data + sb->len, s, n);
	sb->len += n;
	sb->data[sb->len] = '\0';
}

static void sb_append_str(StrBuf* const sb, char const* const s) {
	if (!s) return;
	sb_append_n(sb, s, strlen(s));
}

/* --- マクロ管理 --- */
#define MAX_MACRO_PARAMS 32

typedef struct Macro {
	char* name;
	int is_func_like;
	int is_variadic;
	int param_count;
	char* params[MAX_MACRO_PARAMS];
	char* body;
	struct Macro* next;
} Macro;

typedef struct OnceFile {
	char* path;
	struct OnceFile* next;
} OnceFile;

typedef struct {
	Macro* macros;
	OnceFile* once_files;
	char const* const* include_dirs;
	int include_dir_count;
	int include_depth;
} CppState;

static Macro* find_macro(CppState const* const st, char const* const name) {
	Macro* cur = st->macros;
	while (cur) {
		if (strcmp(cur->name, name) == 0) return cur;
		cur = cur->next;
	}
	return NULL;
}

static void free_macro_fields(Macro* const m) {
	free(m->name);
	free(m->body);
	for (int i = 0; i < m->param_count; i++) {
		free(m->params[i]);
	}
}

static void undef_macro(CppState* const st, char const* const name) {
	Macro** pp = &st->macros;
	while (*pp) {
		if (strcmp((*pp)->name, name) == 0) {
			Macro* target = *pp;
			*pp = target->next;
			free_macro_fields(target);
			free(target);
			return;
		}
		pp = &(*pp)->next;
	}
}

static void define_macro(CppState* const st, char const* const name, int const is_func_like, int const is_variadic, int const param_count, char* const* const params, char const* const body) {
	undef_macro(st, name);
	Macro* m = calloc(1, sizeof(Macro));
	m->name = strdup(name);
	m->is_func_like = is_func_like;
	m->is_variadic = is_variadic;
	m->param_count = param_count;
	for (int i = 0; i < param_count; i++) {
		m->params[i] = strdup(params[i]);
	}
	m->body = strdup(body ? body : "");
	m->next = st->macros;
	st->macros = m;
}

static int is_once_file(CppState const* const st, char const* const path) {
	OnceFile* cur = st->once_files;
	while (cur) {
		if (strcmp(cur->path, path) == 0) return 1;
		cur = cur->next;
	}
	return 0;
}

static void mark_once_file(CppState* const st, char const* const path) {
	if (is_once_file(st, path)) return;
	OnceFile* f = malloc(sizeof(OnceFile));
	f->path = strdup(path);
	f->next = st->once_files;
	st->once_files = f;
}

static void free_cpp_state(CppState* const st) {
	Macro* m = st->macros;
	while (m) {
		Macro* next = m->next;
		free_macro_fields(m);
		free(m);
		m = next;
	}
	OnceFile* f = st->once_files;
	while (f) {
		OnceFile* next = f->next;
		free(f->path);
		free(f);
		f = next;
	}
}

/* --- ユーティリティ --- */
static int is_ident_start(char const c) {
	return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(char const c) {
	return isalnum((unsigned char)c) || c == '_';
}

static char const* skip_ws_inline(char const* p) {
	while (*p == ' ' || *p == '\t' || *p == '\r') p++;
	return p;
}

static char* trim_dup(char const* start, char const* end) {
	while (start < end && (*start == ' ' || *start == '\t' || *start == '\r')) start++;
	while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
	size_t const len = (size_t)(end - start);
	char* res = malloc(len + 1);
	memcpy(res, start, len);
	res[len] = '\0';
	return res;
}

static char* read_file_all(char const* const path) {
	FILE* fp = fopen(path, "rb");
	if (!fp) return NULL;
	fseek(fp, 0, SEEK_END);
	long const fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (fsize < 0) {
		fclose(fp);
		return NULL;
	}
	char* buf = malloc((size_t)fsize + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	size_t const n = fread(buf, 1, (size_t)fsize, fp);
	buf[n] = '\0';
	fclose(fp);
	return buf;
}

/* --- コメント除去 & バックスラッシュ行継続 --- */
static char* normalize_source(char const* const src) {
	StrBuf sb;
	sb_init(&sb);
	char const* p = src;

	/* 先頭行の Shebang (#!) は空行に置換して行番号を維持 */
	if (p[0] == '#' && p[1] == '!') {
		while (*p && *p != '\n') p++;
	}

	while (*p) {
		if (p[0] == '\\' && p[1] == '\r' && p[2] == '\n') {
			p += 3;
			continue;
		}
		if (p[0] == '\\' && p[1] == '\n') {
			p += 2;
			continue;
		}
		if (p[0] == '"' || p[0] == '\'') {
			char const quote = p[0];
			sb_append_char(&sb, *p++);
			while (*p && *p != quote) {
				if (*p == '\\' && p[1]) {
					sb_append_char(&sb, *p++);
					sb_append_char(&sb, *p++);
				} else if (*p == '\n') {
					break;
				} else {
					sb_append_char(&sb, *p++);
				}
			}
			if (*p == quote) sb_append_char(&sb, *p++);
			continue;
		}
		if (p[0] == '/' && p[1] == '/') {
			p += 2;
			while (*p && *p != '\n') p++;
			continue;
		}
		if (p[0] == '/' && p[1] == '*') {
			p += 2;
			sb_append_char(&sb, ' ');
			while (*p && !(p[0] == '*' && p[1] == '/')) {
				if (*p == '\n') sb_append_char(&sb, '\n');
				p++;
			}
			if (p[0] == '*' && p[1] == '/') p += 2;
			continue;
		}
		sb_append_char(&sb, *p++);
	}
	return sb.data;
}

/* --- マクロ展開 --- */
static char* expand_text(CppState* const st, char const* const input, char const* const filename, int const line, char const* const* const active, int const active_count);

static int is_macro_active(char const* const name, char const* const* const active, int const active_count) {
	for (int i = 0; i < active_count; i++) {
		if (strcmp(active[i], name) == 0) return 1;
	}
	return 0;
}

static char* stringify_arg(char const* const arg) {
	StrBuf sb;
	sb_init(&sb);
	sb_append_char(&sb, '"');
	for (char const* p = arg; *p; p++) {
		if (*p == '"' || *p == '\\') sb_append_char(&sb, '\\');
		sb_append_char(&sb, *p);
	}
	sb_append_char(&sb, '"');
	return sb.data;
}

static char* substitute_func_macro(CppState* const st, Macro const* const m, char* const* const raw_args, char* const* const exp_args, char const* const raw_va, char const* const exp_va, char const* const filename, int const line, char const* const* const active, int const active_count) {
	StrBuf sb;
	sb_init(&sb);
	char const* p = m->body;

	while (*p) {
		/* , ##__VA_ARGS__ 対応（可変引数が空なら直前のカンマを除去） */
		if (p[0] == ',' ) {
			char const* q = skip_ws_inline(p + 1);
			if (q[0] == '#' && q[1] == '#') {
				q = skip_ws_inline(q + 2);
				if (strncmp(q, "__VA_ARGS__", 11) == 0 && !is_ident_char(q[11])) {
					if (!raw_va || raw_va[0] == '\0') {
						p = q + 11;
						continue;
					}
					sb_append_str(&sb, ", ");
					sb_append_str(&sb, exp_va);
					p = q + 11;
					continue;
				}
			}
		}
		/* トークン連結 ## の前後空白を除去して連結 */
		if (p[0] == '#' && p[1] == '#') {
			while (sb.len > 0 && (sb.data[sb.len - 1] == ' ' || sb.data[sb.len - 1] == '\t')) {
				sb.len--;
			}
			sb.data[sb.len] = '\0';
			p = skip_ws_inline(p + 2);
			if (is_ident_start(*p)) {
				char const* id_start = p;
				while (is_ident_char(*p)) p++;
				size_t const id_len = (size_t)(p - id_start);
				int matched = -1;
				for (int i = 0; i < m->param_count; i++) {
					if (strlen(m->params[i]) == id_len && strncmp(m->params[i], id_start, id_len) == 0) {
						matched = i;
						break;
					}
				}
				if (matched >= 0) {
					sb_append_str(&sb, raw_args[matched]);
				} else {
					sb_append_n(&sb, id_start, id_len);
				}
			}
			continue;
		}
		/* # 文字列化演算子 */
		if (p[0] == '#' && p[1] != '#') {
			char const* q = skip_ws_inline(p + 1);
			if (is_ident_start(*q)) {
				char const* id_start = q;
				while (is_ident_char(*q)) q++;
				size_t const id_len = (size_t)(q - id_start);
				int matched = -1;
				for (int i = 0; i < m->param_count; i++) {
					if (strlen(m->params[i]) == id_len && strncmp(m->params[i], id_start, id_len) == 0) {
						matched = i;
						break;
					}
				}
				if (matched >= 0) {
					char* s = stringify_arg(raw_args[matched]);
					sb_append_str(&sb, s);
					free(s);
					p = q;
					continue;
				}
				if (m->is_variadic && id_len == 11 && strncmp(id_start, "__VA_ARGS__", 11) == 0) {
					char* s = stringify_arg(raw_va ? raw_va : "");
					sb_append_str(&sb, s);
					free(s);
					p = q;
					continue;
				}
			}
		}
		if (p[0] == '"' || p[0] == '\'') {
			char const quote = p[0];
			sb_append_char(&sb, *p++);
			while (*p && *p != quote) {
				if (*p == '\\' && p[1]) {
					sb_append_char(&sb, *p++);
					sb_append_char(&sb, *p++);
				} else {
					sb_append_char(&sb, *p++);
				}
			}
			if (*p == quote) sb_append_char(&sb, *p++);
			continue;
		}
		if (is_ident_start(*p)) {
			char const* id_start = p;
			while (is_ident_char(*p)) p++;
			size_t const id_len = (size_t)(p - id_start);
			int matched = -1;
			for (int i = 0; i < m->param_count; i++) {
				if (strlen(m->params[i]) == id_len && strncmp(m->params[i], id_start, id_len) == 0) {
					matched = i;
					break;
				}
			}
			if (matched >= 0) {
				/* 直後が ## なら未展開引数を使用 */
				char const* look = skip_ws_inline(p);
				if (look[0] == '#' && look[1] == '#') {
					sb_append_str(&sb, raw_args[matched]);
				} else {
					sb_append_str(&sb, exp_args[matched]);
				}
			} else if (m->is_variadic && id_len == 11 && strncmp(id_start, "__VA_ARGS__", 11) == 0) {
				sb_append_str(&sb, exp_va ? exp_va : "");
			} else {
				sb_append_n(&sb, id_start, id_len);
			}
			continue;
		}
		sb_append_char(&sb, *p++);
	}

	char const* new_active[64];
	int new_count = active_count < 63 ? active_count : 63;
	for (int i = 0; i < new_count; i++) new_active[i] = active[i];
	new_active[new_count++] = m->name;

	char* res = expand_text(st, sb.data, filename, line, new_active, new_count);
	free(sb.data);
	return res;
}

static char* expand_text(CppState* const st, char const* const input, char const* const filename, int const line, char const* const* const active, int const active_count) {
	StrBuf sb;
	sb_init(&sb);
	char const* p = input;

	while (*p) {
		if (p[0] == '"' || p[0] == '\'') {
			char const quote = p[0];
			sb_append_char(&sb, *p++);
			while (*p && *p != quote) {
				if (*p == '\\' && p[1]) {
					sb_append_char(&sb, *p++);
					sb_append_char(&sb, *p++);
				} else {
					sb_append_char(&sb, *p++);
				}
			}
			if (*p == quote) sb_append_char(&sb, *p++);
			continue;
		}
		if (is_ident_start(*p)) {
			char const* id_start = p;
			while (is_ident_char(*p)) p++;
			size_t const id_len = (size_t)(p - id_start);
			char* id = malloc(id_len + 1);
			memcpy(id, id_start, id_len);
			id[id_len] = '\0';

			/* 組み込みマクロ */
			if (strcmp(id, "__LINE__") == 0) {
				char buf[32];
				snprintf(buf, sizeof(buf), "%d", line);
				sb_append_str(&sb, buf);
				free(id);
				continue;
			}
			if (strcmp(id, "__FILE__") == 0) {
				char* q = stringify_arg(filename);
				sb_append_str(&sb, q);
				free(q);
				free(id);
				continue;
			}

			Macro* const m = find_macro(st, id);
			if (m && !is_macro_active(id, active, active_count)) {
				if (!m->is_func_like) {
					char const* new_active[64];
					int new_count = active_count < 63 ? active_count : 63;
					for (int i = 0; i < new_count; i++) new_active[i] = active[i];
					new_active[new_count++] = m->name;
					char* exp = expand_text(st, m->body, filename, line, new_active, new_count);
					sb_append_str(&sb, exp);
					free(exp);
					free(id);
					continue;
				}
				char const* look = skip_ws_inline(p);
				if (*look == '(') {
					look++;
					char* raw_args[MAX_MACRO_PARAMS] = {0};
					char* exp_args[MAX_MACRO_PARAMS] = {0};
					int arg_count = 0;
					int depth = 1;
					char const* arg_start = look;
					while (*look && depth > 0) {
						if (*look == '"' || *look == '\'') {
							char const q = *look++;
							while (*look && *look != q) {
								if (*look == '\\' && look[1]) look += 2;
								else look++;
							}
							if (*look == q) look++;
							continue;
						}
						if (*look == '(') depth++;
						else if (*look == ')') {
							depth--;
							if (depth == 0) {
								if (arg_count > 0 || skip_ws_inline(arg_start) < look) {
									if (arg_count < MAX_MACRO_PARAMS) {
										raw_args[arg_count] = trim_dup(arg_start, look);
										exp_args[arg_count] = expand_text(st, raw_args[arg_count], filename, line, active, active_count);
										arg_count++;
									}
								}
								look++;
								break;
							}
						} else if (*look == ',' && depth == 1) {
							if (arg_count < MAX_MACRO_PARAMS) {
								raw_args[arg_count] = trim_dup(arg_start, look);
								exp_args[arg_count] = expand_text(st, raw_args[arg_count], filename, line, active, active_count);
								arg_count++;
							}
							look++;
							arg_start = look;
							continue;
						}
						look++;
					}
					if ((!m->is_variadic && arg_count == m->param_count) || (m->is_variadic && arg_count >= m->param_count)) {
						StrBuf raw_va_sb;
						StrBuf exp_va_sb;
						sb_init(&raw_va_sb);
						sb_init(&exp_va_sb);
						for (int i = m->param_count; i < arg_count; i++) {
							if (i > m->param_count) {
								sb_append_str(&raw_va_sb, ", ");
								sb_append_str(&exp_va_sb, ", ");
							}
							sb_append_str(&raw_va_sb, raw_args[i]);
							sb_append_str(&exp_va_sb, exp_args[i]);
						}
						char* exp = substitute_func_macro(st, m, raw_args, exp_args, raw_va_sb.data, exp_va_sb.data, filename, line, active, active_count);
						free(raw_va_sb.data);
						free(exp_va_sb.data);
						sb_append_str(&sb, exp);
						free(exp);
						for (int i = 0; i < arg_count; i++) {
							free(raw_args[i]);
							free(exp_args[i]);
						}
						p = look;
						free(id);
						continue;
					}
					for (int i = 0; i < arg_count; i++) {
						free(raw_args[i]);
						free(exp_args[i]);
					}
				}
			}

			sb_append_n(&sb, id_start, id_len);
			free(id);
			continue;
		}
		sb_append_char(&sb, *p++);
	}
	return sb.data;
}

/* --- #if 式評価器 --- */
typedef struct {
	char const* p;
	CppState* st;
} IfEval;

static long long eval_lor(IfEval* ev);

static void eval_skip_ws(IfEval* const ev) {
	while (*ev->p == ' ' || *ev->p == '\t') ev->p++;
}

static long long eval_primary(IfEval* const ev) {
	eval_skip_ws(ev);
	if (*ev->p == '(') {
		ev->p++;
		long long const v = eval_lor(ev);
		eval_skip_ws(ev);
		if (*ev->p == ')') ev->p++;
		return v;
	}
	if (strncmp(ev->p, "defined", 7) == 0 && !is_ident_char(ev->p[7])) {
		ev->p += 7;
		eval_skip_ws(ev);
		int has_paren = 0;
		if (*ev->p == '(') {
			has_paren = 1;
			ev->p++;
			eval_skip_ws(ev);
		}
		char const* s = ev->p;
		while (is_ident_char(*ev->p)) ev->p++;
		size_t const n = (size_t)(ev->p - s);
		char* id = malloc(n + 1);
		memcpy(id, s, n);
		id[n] = '\0';
		if (has_paren) {
			eval_skip_ws(ev);
			if (*ev->p == ')') ev->p++;
		}
		long long const res = (find_macro(ev->st, id) != NULL) ? 1 : 0;
		free(id);
		return res;
	}
	if (isdigit((unsigned char)*ev->p)) {
		char* endp = NULL;
		long long const v = strtoll(ev->p, &endp, 0);
		ev->p = endp;
		while (*ev->p == 'u' || *ev->p == 'U' || *ev->p == 'l' || *ev->p == 'L') ev->p++;
		return v;
	}
	if (is_ident_start(*ev->p)) {
		while (is_ident_char(*ev->p)) ev->p++;
		return 0; /* 未定義識別子は 0 */
	}
	return 0;
}

static long long eval_unary(IfEval* const ev) {
	eval_skip_ws(ev);
	if (*ev->p == '!') { ev->p++; return !eval_unary(ev); }
	if (*ev->p == '~') { ev->p++; return ~eval_unary(ev); }
	if (*ev->p == '-') { ev->p++; return -eval_unary(ev); }
	if (*ev->p == '+') { ev->p++; return eval_unary(ev); }
	return eval_primary(ev);
}

static long long eval_mul(IfEval* const ev) {
	long long v = eval_unary(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (*ev->p == '*') { ev->p++; v *= eval_unary(ev); }
		else if (*ev->p == '/') { ev->p++; long long r = eval_unary(ev); v = r ? (v / r) : 0; }
		else if (*ev->p == '%') { ev->p++; long long r = eval_unary(ev); v = r ? (v % r) : 0; }
		else break;
	}
	return v;
}

static long long eval_add(IfEval* const ev) {
	long long v = eval_mul(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (*ev->p == '+') { ev->p++; v += eval_mul(ev); }
		else if (*ev->p == '-') { ev->p++; v -= eval_mul(ev); }
		else break;
	}
	return v;
}

static long long eval_shift(IfEval* const ev) {
	long long v = eval_add(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (ev->p[0] == '<' && ev->p[1] == '<') { ev->p += 2; v <<= eval_add(ev); }
		else if (ev->p[0] == '>' && ev->p[1] == '>') { ev->p += 2; v >>= eval_add(ev); }
		else break;
	}
	return v;
}

static long long eval_rel(IfEval* const ev) {
	long long v = eval_shift(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (ev->p[0] == '<' && ev->p[1] == '=') { ev->p += 2; v = (v <= eval_shift(ev)); }
		else if (ev->p[0] == '>' && ev->p[1] == '=') { ev->p += 2; v = (v >= eval_shift(ev)); }
		else if (*ev->p == '<') { ev->p++; v = (v < eval_shift(ev)); }
		else if (*ev->p == '>') { ev->p++; v = (v > eval_shift(ev)); }
		else break;
	}
	return v;
}

static long long eval_eq(IfEval* const ev) {
	long long v = eval_rel(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (ev->p[0] == '=' && ev->p[1] == '=') { ev->p += 2; v = (v == eval_rel(ev)); }
		else if (ev->p[0] == '!' && ev->p[1] == '=') { ev->p += 2; v = (v != eval_rel(ev)); }
		else break;
	}
	return v;
}

static long long eval_band(IfEval* const ev) {
	long long v = eval_eq(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (ev->p[0] == '&' && ev->p[1] != '&') { ev->p++; v &= eval_eq(ev); }
		else break;
	}
	return v;
}

static long long eval_bxor(IfEval* const ev) {
	long long v = eval_band(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (*ev->p == '^') { ev->p++; v ^= eval_band(ev); }
		else break;
	}
	return v;
}

static long long eval_bor(IfEval* const ev) {
	long long v = eval_bxor(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (ev->p[0] == '|' && ev->p[1] != '|') { ev->p++; v |= eval_bxor(ev); }
		else break;
	}
	return v;
}

static long long eval_land(IfEval* const ev) {
	long long v = eval_bor(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (ev->p[0] == '&' && ev->p[1] == '&') { ev->p += 2; long long r = eval_bor(ev); v = (v && r); }
		else break;
	}
	return v;
}

static long long eval_lor(IfEval* const ev) {
	long long v = eval_land(ev);
	for (;;) {
		eval_skip_ws(ev);
		if (ev->p[0] == '|' && ev->p[1] == '|') { ev->p += 2; long long r = eval_land(ev); v = (v || r); }
		else break;
	}
	return v;
}

/* defined(X) を先に評価してから残りマクロ展開するため、defined(...) 以外の識別子を展開 */
static char* expand_for_if(CppState* const st, char const* const expr_str, char const* const filename, int const line) {
	StrBuf sb;
	sb_init(&sb);
	char const* p = expr_str;
	while (*p) {
		if (strncmp(p, "defined", 7) == 0 && !is_ident_char(p[7])) {
			sb_append_n(&sb, p, 7);
			p += 7;
			while (*p == ' ' || *p == '\t') sb_append_char(&sb, *p++);
			if (*p == '(') {
				sb_append_char(&sb, *p++);
				while (*p && *p != ')') sb_append_char(&sb, *p++);
				if (*p == ')') sb_append_char(&sb, *p++);
			} else {
				while (is_ident_char(*p)) sb_append_char(&sb, *p++);
			}
			continue;
		}
		if (is_ident_start(*p)) {
			char const* s = p;
			while (is_ident_char(*p)) p++;
			size_t const n = (size_t)(p - s);
			char* id = malloc(n + 1);
			memcpy(id, s, n);
			id[n] = '\0';
			Macro* const m = find_macro(st, id);
			if (m && !m->is_func_like) {
				char const* const act[1] = { m->name };
				char* exp = expand_text(st, m->body, filename, line, act, 1);
				sb_append_str(&sb, exp);
				free(exp);
			} else {
				sb_append_n(&sb, s, n);
			}
			free(id);
			continue;
		}
		sb_append_char(&sb, *p++);
	}
	return sb.data;
}

static int eval_if_condition(CppState* const st, char const* const expr_str, char const* const filename, int const line) {
	char* exp = expand_for_if(st, expr_str, filename, line);
	IfEval ev = { exp, st };
	long long const res = eval_lor(&ev);
	free(exp);
	return res != 0;
}

/* --- #include ファイル解決 --- */
static char* resolve_include_path(CppState const* const st, char const* const inc_name, int const is_system, char const* const cur_file) {
	char path_buf[1024];
	if (!is_system && cur_file) {
		char const* const slash = strrchr(cur_file, '/');
		if (slash) {
			size_t const dir_len = (size_t)(slash - cur_file);
			snprintf(path_buf, sizeof(path_buf), "%.*s/%s", (int)dir_len, cur_file, inc_name);
		} else {
			snprintf(path_buf, sizeof(path_buf), "%s", inc_name);
		}
		FILE* fp = fopen(path_buf, "rb");
		if (fp) {
			fclose(fp);
			return strdup(path_buf);
		}
	}
	for (int i = 0; i < st->include_dir_count; i++) {
		if (!st->include_dirs[i]) continue;
		snprintf(path_buf, sizeof(path_buf), "%s/%s", st->include_dirs[i], inc_name);
		FILE* fp = fopen(path_buf, "rb");
		if (fp) {
			fclose(fp);
			return strdup(path_buf);
		}
	}
	return NULL;
}

/* --- 行単位プリプロセス処理 --- */
#define MAX_COND_DEPTH 64

typedef struct {
	int parent_active;
	int active;
	int matched;
} CondFrame;

static void append_line_directive(StrBuf* const out, int const line, char const* const filename) {
	char buf[1024];
	snprintf(buf, sizeof(buf), "# %d \"%s\"\n", line, filename);
	sb_append_str(out, buf);
}

static void process_source(CppState* const st, StrBuf* const out, char const* const raw_src, char const* const filename) {
	if (st->include_depth > 64) {
		fprintf(stderr, "%s:1:1: Ga! (CPP Error): #include nested too deeply\n", filename);
		exit(1);
	}
	st->include_depth++;

	char* norm = normalize_source(raw_src);
	append_line_directive(out, 1, filename);

	CondFrame cond_stack[MAX_COND_DEPTH];
	int cond_depth = 0;
	int cur_active = 1;
	int line_num = 1;

	char const* p = norm;
	while (*p) {
		char const* line_start = p;
		while (*p && *p != '\n') p++;
		char const* line_end = p;
		if (*p == '\n') p++;

		char const* s = skip_ws_inline(line_start);
		if (s < line_end && *s == '#') {
			s = skip_ws_inline(s + 1);
			char const* dir_start = s;
			while (s < line_end && is_ident_char(*s)) s++;
			size_t const dir_len = (size_t)(s - dir_start);
			s = skip_ws_inline(s);

			if (dir_len == 2 && strncmp(dir_start, "if", 2) == 0) {
				char* expr_s = trim_dup(s, line_end);
				int cond = cur_active ? eval_if_condition(st, expr_s, filename, line_num) : 0;
				free(expr_s);
				cond_stack[cond_depth++] = (CondFrame){ cur_active, cur_active && cond, cond };
				cur_active = cur_active && cond;
			} else if (dir_len == 5 && strncmp(dir_start, "ifdef", 5) == 0) {
				char* id = trim_dup(s, line_end);
				int cond = cur_active ? (find_macro(st, id) != NULL) : 0;
				free(id);
				cond_stack[cond_depth++] = (CondFrame){ cur_active, cur_active && cond, cond };
				cur_active = cur_active && cond;
			} else if (dir_len == 6 && strncmp(dir_start, "ifndef", 6) == 0) {
				char* id = trim_dup(s, line_end);
				int cond = cur_active ? (find_macro(st, id) == NULL) : 0;
				free(id);
				cond_stack[cond_depth++] = (CondFrame){ cur_active, cur_active && cond, cond };
				cur_active = cur_active && cond;
			} else if (dir_len == 4 && strncmp(dir_start, "elif", 4) == 0) {
				if (cond_depth == 0) {
					fprintf(stderr, "%s:%d:1: Ga! (CPP Error): #elif without #if\n", filename, line_num);
					exit(1);
				}
				CondFrame* cf = &cond_stack[cond_depth - 1];
				char* expr_s = trim_dup(s, line_end);
				if (cf->parent_active && !cf->matched) {
					int cond = eval_if_condition(st, expr_s, filename, line_num);
					cf->active = cond;
					if (cond) cf->matched = 1;
				} else {
					cf->active = 0;
				}
				free(expr_s);
				cur_active = cf->active;
			} else if (dir_len == 4 && strncmp(dir_start, "else", 4) == 0) {
				if (cond_depth == 0) {
					fprintf(stderr, "%s:%d:1: Ga! (CPP Error): #else without #if\n", filename, line_num);
					exit(1);
				}
				CondFrame* cf = &cond_stack[cond_depth - 1];
				cf->active = (cf->parent_active && !cf->matched) ? 1 : 0;
				cf->matched = 1;
				cur_active = cf->active;
			} else if (dir_len == 5 && strncmp(dir_start, "endif", 5) == 0) {
				if (cond_depth == 0) {
					fprintf(stderr, "%s:%d:1: Ga! (CPP Error): #endif without #if\n", filename, line_num);
					exit(1);
				}
				cond_depth--;
				cur_active = (cond_depth > 0) ? cond_stack[cond_depth - 1].active : 1;
			} else if (cur_active) {
				if (dir_len == 6 && strncmp(dir_start, "define", 6) == 0) {
					if (s < line_end && is_ident_start(*s)) {
						char const* name_start = s;
						while (s < line_end && is_ident_char(*s)) s++;
						char* name = trim_dup(name_start, s);
						if (s < line_end && *s == '(') {
							s++;
							char* params[MAX_MACRO_PARAMS];
							int pcnt = 0;
							int is_var = 0;
							while (s < line_end && *s != ')') {
								s = skip_ws_inline(s);
								if (s + 2 < line_end && s[0] == '.' && s[1] == '.' && s[2] == '.') {
									is_var = 1;
									s += 3;
									s = skip_ws_inline(s);
									break;
								}
								char const* ps = s;
								while (s < line_end && is_ident_char(*s)) s++;
								if (s > ps && pcnt < MAX_MACRO_PARAMS) {
									params[pcnt++] = trim_dup(ps, s);
								}
								s = skip_ws_inline(s);
								if (s < line_end && *s == ',') s++;
							}
							if (s < line_end && *s == ')') s++;
							char* body = trim_dup(s, line_end);
							define_macro(st, name, 1, is_var, pcnt, params, body);
							for (int i = 0; i < pcnt; i++) free(params[i]);
							free(body);
						} else {
							char* body = trim_dup(s, line_end);
							define_macro(st, name, 0, 0, 0, NULL, body);
							free(body);
						}
						free(name);
					}
				} else if (dir_len == 5 && strncmp(dir_start, "undef", 5) == 0) {
					char* name = trim_dup(s, line_end);
					undef_macro(st, name);
					free(name);
				} else if (dir_len == 7 && strncmp(dir_start, "include", 7) == 0) {
					int is_sys = 0;
					char const* fname_start = NULL;
					char const* fname_end = NULL;
					if (s < line_end && *s == '"') {
						s++;
						fname_start = s;
						while (s < line_end && *s != '"') s++;
						fname_end = s;
					} else if (s < line_end && *s == '<') {
						is_sys = 1;
						s++;
						fname_start = s;
						while (s < line_end && *s != '>') s++;
						fname_end = s;
					}
					if (fname_start && fname_end > fname_start) {
						char* inc_name = trim_dup(fname_start, fname_end);
						char* resolved = resolve_include_path(st, inc_name, is_sys, filename);
						if (!resolved) {
							fprintf(stderr, "%s:%d:1: Ga! (CPP Error): '%s' file not found\n", filename, line_num, inc_name);
							free(inc_name);
							exit(1);
						}
						if (!is_once_file(st, resolved)) {
							char* inc_src = read_file_all(resolved);
							if (!inc_src) {
								fprintf(stderr, "%s:%d:1: Ga! (CPP Error): Failed to read '%s'\n", filename, line_num, resolved);
								free(resolved);
								free(inc_name);
								exit(1);
							}
							process_source(st, out, inc_src, resolved);
							free(inc_src);
							append_line_directive(out, line_num + 1, filename);
						}
						free(resolved);
						free(inc_name);
					}
				} else if (dir_len == 6 && strncmp(dir_start, "pragma", 6) == 0) {
					char* p_arg = trim_dup(s, line_end);
					if (strcmp(p_arg, "once") == 0) {
						mark_once_file(st, filename);
					} else if (strncmp(p_arg, "ginc_lib", 8) == 0) {
						char const* q = skip_ws_inline(p_arg + 8);
						if (*q == '"') {
							q++;
							char const* qs = q;
							while (*q && *q != '"') q++;
							char* lib_name = trim_dup(qs, q);
							ginc_load_library(lib_name);
							free(lib_name);
						}
					}
					free(p_arg);
				} else if (dir_len == 5 && strncmp(dir_start, "error", 5) == 0) {
					char* msg = trim_dup(s, line_end);
					fprintf(stderr, "%s:%d:1: Ga! (CPP Error): #error %s\n", filename, line_num, msg);
					free(msg);
					exit(1);
				} else if (dir_len == 7 && strncmp(dir_start, "warning", 7) == 0) {
					char* msg = trim_dup(s, line_end);
					fprintf(stderr, "%s:%d:1: Ga! (CPP Warning): #warning %s\n", filename, line_num, msg);
					free(msg);
				} else if (dir_len == 4 && strncmp(dir_start, "line", 4) == 0) {
					/* #line はそのまま通すか無視 */
				} else if (dir_len > 0 && !isdigit((unsigned char)dir_start[0])) {
					char* dname = trim_dup(dir_start, dir_start + dir_len);
					fprintf(stderr, "%s:%d:1: Ga! (CPP Error): Invalid preprocessing directive '#%s'\n", filename, line_num, dname);
					free(dname);
					exit(1);
				}
			}
			sb_append_char(out, '\n');
		} else {
			if (cur_active) {
				char* raw_line = malloc((size_t)(line_end - line_start) + 1);
				memcpy(raw_line, line_start, (size_t)(line_end - line_start));
				raw_line[line_end - line_start] = '\0';
				char* expanded = expand_text(st, raw_line, filename, line_num, NULL, 0);
				sb_append_str(out, expanded);
				free(expanded);
				free(raw_line);
			}
			sb_append_char(out, '\n');
		}
		line_num++;
	}

	if (cond_depth != 0) {
		fprintf(stderr, "%s:%d:1: Ga! (CPP Error): Unterminated #if directive\n", filename, line_num);
		exit(1);
	}

	free(norm);
	st->include_depth--;
}

static void init_builtin_macros(CppState* const st) {
	define_macro(st, "__GINC__", 0, 0, 0, NULL, "1");
	define_macro(st, "NULL", 0, 0, 0, NULL, "0");
}

char* ginc_preprocess_string(char const* const src, char const* const filename, char const* const* const include_dirs, int const include_dir_count) {
	CppState st;
	memset(&st, 0, sizeof(st));
	st.include_dirs = include_dirs;
	st.include_dir_count = include_dir_count;
	init_builtin_macros(&st);

	StrBuf out;
	sb_init(&out);
	process_source(&st, &out, src ? src : "", filename ? filename : "<stdin>");
	free_cpp_state(&st);
	return out.data;
}

char* ginc_preprocess_file(char const* const filename, char const* const* const include_dirs, int const include_dir_count) {
	char* src = read_file_all(filename);
	if (!src) {
		fprintf(stderr, "Ga! (CPP Error): Cannot open file '%s'\n", filename);
		exit(1);
	}
	char* res = ginc_preprocess_string(src, filename, include_dirs, include_dir_count);
	free(src);
	return res;
}
