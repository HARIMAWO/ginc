/*
 * Copyright 2026 HARIMAWO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern void* ginc_ptr_make(void* base, long long index, long long length, int elem_size, int kind);
extern char const* ginc_ptr_cstr(void* ptr);
extern void* ginc_ptr_elem_addr(void* ptr, long long offset);

#define GINC_PTR_MAGIC 0x47494E43U

typedef struct {
	unsigned int magic;
	void* base;
	long long index;
	long long length;
	int elem_size;
	int kind;
} GincPtrView;

static void* ginc_raw_ptr(void* ptr) {
	if (!ptr) return NULL;
	GincPtrView* p = (GincPtrView*)ptr;
	if (p->magic == GINC_PTR_MAGIC) {
		return ginc_ptr_elem_addr(ptr, 0);
	}
	return ptr;
}

/* --- <string.h> --- */
int ginc_strlen(void* s) {
	if (!s) return 0;
	return (int)strlen(ginc_ptr_cstr(s));
}

int ginc_strcmp(void* a, void* b) {
	return strcmp(ginc_ptr_cstr(a), ginc_ptr_cstr(b));
}

int ginc_strncmp(void* a, void* b, int n) {
	return strncmp(ginc_ptr_cstr(a), ginc_ptr_cstr(b), (size_t)n);
}

void* ginc_strcpy(void* dst, void* src) {
	char* d = (char*)ginc_ptr_elem_addr(dst, 0);
	char const* s = ginc_ptr_cstr(src);
	strcpy(d, s);
	return dst;
}

void* ginc_strchr(void* s, int c) {
	char const* base = ginc_ptr_cstr(s);
	char const* found = strchr(base, c);
	if (!found) return NULL;
	return ginc_ptr_make((void*)found, 0, (long long)strlen(found), 1, 2);
}

void* ginc_strstr(void* haystack, void* needle) {
	char const* h = ginc_ptr_cstr(haystack);
	char const* n = ginc_ptr_cstr(needle);
	char const* found = strstr(h, n);
	if (!found) return NULL;
	return ginc_ptr_make((void*)found, 0, (long long)strlen(found), 1, 2);
}

void* ginc_strcat(void* dst, void* src) {
	char* d = (char*)ginc_raw_ptr(dst);
	char const* s = ginc_ptr_cstr(src);
	strcat(d, s);
	return dst;
}

void* ginc_strncpy(void* dst, void* src, int n) {
	char* d = (char*)ginc_raw_ptr(dst);
	char const* s = ginc_ptr_cstr(src);
	strncpy(d, s, (size_t)n);
	return dst;
}

void* ginc_strdup(void* src) {
	char const* s = ginc_ptr_cstr(src);
	char* dup = strdup(s);
	return ginc_ptr_make(dup, 0, (long long)strlen(dup), 1, 2);
}

void* ginc_memcpy(void* dst, void* src, int n) {
	memcpy(ginc_raw_ptr(dst), ginc_raw_ptr(src), (size_t)n);
	return dst;
}

void* ginc_memset(void* dst, int c, int n) {
	memset(ginc_raw_ptr(dst), c, (size_t)n);
	return dst;
}

int ginc_memcmp(void* a, void* b, int n) {
	return memcmp(ginc_raw_ptr(a), ginc_raw_ptr(b), (size_t)n);
}

/* --- <stdlib.h> --- */
void* ginc_malloc(int size) {
	if (size <= 0) size = 1;
	void* mem = malloc((size_t)size);
	return ginc_ptr_make(mem, 0, (long long)size, 1, 1);
}

void* ginc_calloc(int nmemb, int size) {
	long long total = (long long)nmemb * (long long)size;
	if (total <= 0) total = 1;
	void* mem = calloc(1, (size_t)total);
	return ginc_ptr_make(mem, 0, total, 1, 1);
}

void ginc_free(void* ptr) {
	if (!ptr) return;
	GincPtrView* p = (GincPtrView*)ptr;
	if (p->magic == GINC_PTR_MAGIC) {
		if (p->base) free(p->base);
		free(p);
	}
}

int ginc_atoi(void* s) {
	return atoi(ginc_ptr_cstr(s));
}

int ginc_abs(int x) {
	return abs(x);
}

void ginc_exit(int code) {
	exit(code);
}

/* --- <stdio.h> --- */
int ginc_puts(void* s) {
	return puts(ginc_ptr_cstr(s));
}

int ginc_putchar(int c) {
	return putchar(c);
}

FILE* ginc_fopen(void* path, void* mode) {
	return fopen(ginc_ptr_cstr(path), ginc_ptr_cstr(mode));
}

int ginc_fclose(FILE* fp) {
	if (!fp) return -1;
	return fclose(fp);
}

int ginc_fputs(void* s, FILE* fp) {
	if (!fp) return -1;
	return fputs(ginc_ptr_cstr(s), fp);
}

void* ginc_fgets(void* buf, int size, FILE* fp) {
	if (!fp || !buf) return NULL;
	char* b = (char*)ginc_ptr_elem_addr(buf, 0);
	if (!fgets(b, size, fp)) return NULL;
	return buf;
}

static int ginc_vformat(FILE* out_fp, char* out_buf, size_t out_cap, char const* fmt, va_list ap) {
	int total = 0;
	char const* p = fmt ? fmt : "";

	while (*p) {
		if (*p != '%') {
			if (out_fp) {
				fputc(*p, out_fp);
			} else if (out_buf && (size_t)total + 1 < out_cap) {
				out_buf[total] = *p;
			}
			total++;
			p++;
			continue;
		}
		p++;
		if (*p == '%') {
			if (out_fp) {
				fputc('%', out_fp);
			} else if (out_buf && (size_t)total + 1 < out_cap) {
				out_buf[total] = '%';
			}
			total++;
			p++;
			continue;
		}

		char spec_fmt[64];
		size_t si = 0;
		spec_fmt[si++] = '%';
		while (*p && strchr("-+ #0", *p) && si < 50) spec_fmt[si++] = *p++;
		while (*p && (*p >= '0' && *p <= '9') && si < 50) spec_fmt[si++] = *p++;
		if (*p == '.' && si < 50) {
			spec_fmt[si++] = *p++;
			while (*p && (*p >= '0' && *p <= '9') && si < 50) spec_fmt[si++] = *p++;
		}
		int is_long = 0;
		int is_llong = 0;
		if (*p == 'l' && si < 50) {
			spec_fmt[si++] = *p++;
			is_long = 1;
			if (*p == 'l' && si < 50) {
				spec_fmt[si++] = *p++;
				is_llong = 1;
			}
		}
		char const conv = *p ? *p++ : '\0';
		spec_fmt[si++] = conv;
		spec_fmt[si] = '\0';

		char chunk[1024];
		int n = 0;
		if (conv == 's') {
			void* ptr = va_arg(ap, void*);
			char const* s = ginc_ptr_cstr(ptr);
			if (out_fp) {
				n = fprintf(out_fp, spec_fmt, s);
			} else {
				n = snprintf(out_buf && (size_t)total < out_cap ? out_buf + total : NULL,
					out_buf && (size_t)total < out_cap ? out_cap - (size_t)total : 0,
					spec_fmt, s);
			}
			if (n > 0) total += n;
			continue;
		} else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G') {
			double d = va_arg(ap, double);
			n = snprintf(chunk, sizeof(chunk), spec_fmt, d);
		} else if (conv == 'p') {
			void* ptr = va_arg(ap, void*);
			n = snprintf(chunk, sizeof(chunk), spec_fmt, ptr);
		} else if (is_llong || is_long) {
			long long v = is_llong ? va_arg(ap, long long) : (long long)va_arg(ap, long);
			if (is_llong) n = snprintf(chunk, sizeof(chunk), spec_fmt, v);
			else n = snprintf(chunk, sizeof(chunk), spec_fmt, (long)v);
		} else {
			int v = va_arg(ap, int);
			n = snprintf(chunk, sizeof(chunk), spec_fmt, v);
		}

		if (n > 0) {
			if (out_fp) {
				fwrite(chunk, 1, (size_t)n, out_fp);
			} else if (out_buf && (size_t)total < out_cap) {
				size_t rem = out_cap - (size_t)total - 1;
				size_t cp = ((size_t)n < rem) ? (size_t)n : rem;
				memcpy(out_buf + total, chunk, cp);
			}
			total += n;
		}
	}

	if (!out_fp && out_buf && out_cap > 0) {
		size_t end_pos = ((size_t)total < out_cap) ? (size_t)total : (out_cap - 1);
		out_buf[end_pos] = '\0';
	}
	return total;
}

int ginc_printf(void* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int const res = ginc_vformat(stdout, NULL, 0, ginc_ptr_cstr(fmt), ap);
	va_end(ap);
	return res;
}

int ginc_fprintf(FILE* fp, void* fmt, ...) {
	if (!fp) return -1;
	va_list ap;
	va_start(ap, fmt);
	int const res = ginc_vformat(fp, NULL, 0, ginc_ptr_cstr(fmt), ap);
	va_end(ap);
	return res;
}

int ginc_sprintf(void* dst, void* fmt, ...) {
	char* buf = (char*)ginc_raw_ptr(dst);
	va_list ap;
	va_start(ap, fmt);
	int const res = ginc_vformat(NULL, buf, 4096, ginc_ptr_cstr(fmt), ap);
	va_end(ap);
	return res;
}

int ginc_snprintf(void* dst, int size, void* fmt, ...) {
	char* buf = (char*)ginc_raw_ptr(dst);
	va_list ap;
	va_start(ap, fmt);
	int const res = ginc_vformat(NULL, buf, size > 0 ? (size_t)size : 0, ginc_ptr_cstr(fmt), ap);
	va_end(ap);
	return res;
}

int ginc_fread(void* ptr, int size, int nmemb, FILE* fp) {
	if (!fp || !ptr) return 0;
	return (int)fread(ginc_raw_ptr(ptr), (size_t)size, (size_t)nmemb, fp);
}

int ginc_fwrite(void* ptr, int size, int nmemb, FILE* fp) {
	if (!fp || !ptr) return 0;
	return (int)fwrite(ginc_raw_ptr(ptr), (size_t)size, (size_t)nmemb, fp);
}

int ginc_fseek(FILE* fp, long offset, int whence) {
	if (!fp) return -1;
	return fseek(fp, offset, whence);
}

long ginc_ftell(FILE* fp) {
	if (!fp) return -1;
	return ftell(fp);
}

int ginc_fflush(FILE* fp) {
	return fflush(fp);
}

int ginc_getchar(void) {
	return getchar();
}

/* --- <ctype.h> --- */
int ginc_isalpha(int c) { return isalpha((unsigned char)c); }
int ginc_isdigit(int c) { return isdigit((unsigned char)c); }
int ginc_isalnum(int c) { return isalnum((unsigned char)c); }
int ginc_isspace(int c) { return isspace((unsigned char)c); }
int ginc_isupper(int c) { return isupper((unsigned char)c); }
int ginc_islower(int c) { return islower((unsigned char)c); }
int ginc_toupper(int c) { return toupper((unsigned char)c); }
int ginc_tolower(int c) { return tolower((unsigned char)c); }

/* --- <time.h> --- */
long ginc_time(void* tloc) {
	time_t now = time(NULL);
	if (tloc) {
		long* p = (long*)ginc_raw_ptr(tloc);
		*p = (long)now;
	}
	return (long)now;
}

long ginc_clock(void) {
	return (long)clock();
}

int ginc_sleep(int seconds) {
	return (int)sleep((unsigned int)seconds);
}

int ginc_usleep(int usec) {
	return usleep((useconds_t)usec);
}
