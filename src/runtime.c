#include <dlfcn.h>
#include <regex.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ginc_config.h"

#define MAX_TRY 100
static void* try_stack[MAX_TRY];
static int try_depth = 0;

void ginc_push_try(void* jmpbuf) {
	if (try_depth >= MAX_TRY) {
		fprintf(stderr, "Ga! Try stack overflow\n");
		exit(1);
	}
	try_stack[try_depth++] = jmpbuf;
}

void ginc_pop_try() {
	if (try_depth > 0) try_depth--;
}

void ginc_throw(int val) {
	if (val == 0) val = 1;
	if (try_depth > 0) {
		void* jmpbuf = try_stack[--try_depth];
		_longjmp(*((jmp_buf*)jmpbuf), val);
	} else {
		fprintf(stderr, "Ga! Uncaught exception: %d\n", val);
		exit(1);
	}
}

int ginc_regex_match(char const* const str, char const* const pattern) {
	regex_t regex;
	int ret = 0;

	if (regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) == 0) {
		if (regexec(&regex, str, 0, NULL, 0) == 0) {
			ret = 1;
		}
		regfree(&regex);
	}
	return ret;
}

char* ginc_str_concat(char const* const s1, char const* const s2) {
	size_t const len1 = strlen(s1);
	size_t const len2 = strlen(s2);
	char* const result = malloc(len1 + len2 + 1);

	if (!result) {
		fprintf(stderr, "Ga! (Runtime Error): Out of memory in ginc_str_concat\n");
		exit(1);
	}

	strcpy(result, s1);
	strcat(result, s2);
	return result;
}

#define HASH_SIZE 128
typedef enum { VAL_INT, VAL_STR, VAL_FUNC, VAL_NULL, VAL_DOUBLE } ValType;
typedef struct { ValType type; union { int i; char* s; void* f; double d; } data; } GincValue;
typedef struct HashNode { char* key; GincValue* value; struct HashNode* next; } HashNode;
typedef struct { HashNode* buckets[HASH_SIZE]; } HashTable;

unsigned long ginc_hash_func(char const* str) {
	unsigned long hash = 5381;
	int c;
	while ((c = *str++)) hash = ((hash << 5) + hash) + c;
	return hash % HASH_SIZE;
}

void* ginc_hash_create() {
	return calloc(1, sizeof(HashTable));
}

GincValue* ginc_make_null() {
	GincValue* val = malloc(sizeof(GincValue));
	val->type = VAL_NULL;
	return val;
}

GincValue* ginc_make_int(int v) {
	GincValue* val = malloc(sizeof(GincValue));
	val->type = VAL_INT;
	val->data.i = v;
	return val;
}

GincValue* ginc_make_double(double v) {
	GincValue* val = malloc(sizeof(GincValue));
	val->type = VAL_DOUBLE;
	val->data.d = v;
	return val;
}

double ginc_unwrap_double(GincValue* val) {
	if (!val) return 0.0;
	if (val->type == VAL_DOUBLE) return val->data.d;
	if (val->type == VAL_INT) return (double)val->data.i;
	return 0.0;
}

GincValue* ginc_make_str(char const* v) {
	if (!v) return NULL;
	GincValue* val = malloc(sizeof(GincValue));
	val->type = VAL_STR;
	val->data.s = strdup(v);
	return val;
}

GincValue* ginc_make_func(void* f) {
	GincValue* val = malloc(sizeof(GincValue));
	val->type = VAL_FUNC;
	val->data.f = f;
	return val;
}

void* ginc_unwrap_func(GincValue* val) {
	if (val && val->type == VAL_FUNC) return val->data.f;
	return NULL;
}

void ginc_print_val(GincValue* val) {
	if (!val) {
		printf("null\n");
		return;
	}
	if (val->type == VAL_NULL) {
		printf("null\n");
	} else if (val->type == VAL_INT) {
		printf("%d\n", val->data.i);
	} else if (val->type == VAL_DOUBLE) {
		printf("%f\n", val->data.d);
	} else if (val->type == VAL_STR) {
		printf("%s\n", val->data.s);
	}
}

void ginc_hash_set(void* ht_ptr, char const* key, GincValue* val) {
	HashTable* ht = (HashTable*)ht_ptr;
	unsigned long idx = ginc_hash_func(key);
	HashNode* node = ht->buckets[idx];
	while (node) {
		if (strcmp(node->key, key) == 0) {
			node->value = val;
			return;
		}
		node = node->next;
	}
	node = malloc(sizeof(HashNode));
	node->key = strdup(key);
	node->value = val;
	node->next = ht->buckets[idx];
	ht->buckets[idx] = node;
}

GincValue* ginc_hash_get(void* ht_ptr, char const* key) {
	HashTable* ht = (HashTable*)ht_ptr;
	unsigned long idx = ginc_hash_func(key);
	HashNode* node = ht->buckets[idx];
	while (node) {
		if (strcmp(node->key, key) == 0) return node->value;
		node = node->next;
	}
	ginc_throw(102);
	return NULL;
}

int ginc_unwrap_int(GincValue* val) {
	if (!val) return 0;
	if (val->type == VAL_NULL) return 0;
	if (val->type == VAL_INT) return val->data.i;
	if (val->type == VAL_STR) return atoi(val->data.s);
	return 0;
}

// 例外レジスタ
static int ginc_exc_type = 0; // 1: int, 2: obj (pointer)
static int ginc_exc_int = 0;
static void* ginc_exc_obj = NULL;

void ginc_throw_int(int val) {
	ginc_exc_type = 1;
	ginc_exc_int = val;
	ginc_throw(1);
}

void ginc_throw_obj(void* obj) {
	ginc_exc_type = 2;
	ginc_exc_obj = obj;
	ginc_throw(1);
}

int ginc_check_catch(int target_type) {
	// target_type: TYPE_INT(0), TYPE_STR(1), TYPE_VAR(2)
	if (target_type == 0 && ginc_exc_type == 1) return 1;
	if (target_type == 2 && ginc_exc_type == 2) return 1;
	return 0;
}

int ginc_get_exc_int() {
	return ginc_exc_int;
}

void* ginc_get_exc_obj() {
	return ginc_exc_obj;
}

/* --- ファットポインタ (参照 + 配列/文字列カーソル) --- */
typedef enum {
	GPTR_SCALAR = 0,
	GPTR_ARRAY = 1,
	GPTR_CSTR = 2
} GincPtrKind;

#define GINC_PTR_MAGIC 0x47494E43U

typedef struct {
	unsigned int magic;
	void* base;
	long long index;
	long long length;
	int elem_size;
	int kind;
} GincPtr;

void* ginc_ptr_make(void* base, long long index, long long length, int elem_size, int kind) {
	GincPtr* p = malloc(sizeof(GincPtr));
	p->magic = GINC_PTR_MAGIC;
	p->base = base;
	p->index = index;
	p->length = length;
	p->elem_size = elem_size;
	p->kind = kind;
	return p;
}

void* ginc_ptr_make_cstr(char const* s) {
	if (!s) return NULL;
	return ginc_ptr_make((void*)s, 0, (long long)strlen(s), 1, GPTR_CSTR);
}

void* ginc_ptr_add(void* ptr, long long offset) {
	GincPtr* p = (GincPtr*)ptr;
	if (!p) {
		ginc_throw(103);
		return NULL;
	}
	if (p->kind == GPTR_SCALAR) {
		if (offset == 0) return ginc_ptr_make(p->base, -1, 0, p->elem_size, GPTR_SCALAR);
		fprintf(stderr, "Ga! (Runtime Error): Pointer arithmetic on scalar reference is not allowed\n");
		ginc_throw(103);
		return NULL;
	}
	return ginc_ptr_make(p->base, p->index + offset, p->length, p->elem_size, p->kind);
}

long long ginc_ptr_diff(void* a_ptr, void* b_ptr) {
	GincPtr* a = (GincPtr*)a_ptr;
	GincPtr* b = (GincPtr*)b_ptr;
	if (!a || !b || a->base != b->base) {
		fprintf(stderr, "Ga! (Runtime Error): Invalid pointer difference\n");
		ginc_throw(103);
		return 0;
	}
	return a->index - b->index;
}

void* ginc_ptr_elem_addr(void* ptr, long long offset) {
	GincPtr* p = (GincPtr*)ptr;
	if (!p || !p->base) {
		fprintf(stderr, "Ga! (Runtime Error): Null pointer dereference\n");
		ginc_throw(103);
		return NULL;
	}
	if (p->kind == GPTR_SCALAR) {
		if (offset != 0) {
			fprintf(stderr, "Ga! (Runtime Error): Out-of-bounds offset on scalar pointer\n");
			ginc_throw(103);
			return NULL;
		}
		return p->base;
	}
	long long const idx = p->index + offset;
	if (p->kind == GPTR_CSTR) {
		if (idx < 0 || idx > p->length) {
			fprintf(stderr, "Ga! (Runtime Error): String pointer out of bounds (index=%lld, len=%lld)\n", idx, p->length);
			ginc_throw(103);
			return NULL;
		}
		return (char*)p->base + idx;
	}
	if (idx < 0 || idx >= p->length) {
		fprintf(stderr, "Ga! (Runtime Error): Array pointer out of bounds (index=%lld, len=%lld)\n", idx, p->length);
		ginc_throw(103);
		return NULL;
	}
	return (char*)p->base + idx * (long long)p->elem_size;
}

char const* ginc_ptr_cstr(void* ptr) {
	if (!ptr) return "(null)";
	GincPtr* p = (GincPtr*)ptr;
	if (p->magic != GINC_PTR_MAGIC) return (char const*)ptr;
	if (!p->base) return "(null)";
	if (p->kind == GPTR_CSTR) {
		if (p->index < 0 || p->index > p->length) return "";
		return (char const*)p->base + p->index;
	}
	if (p->kind == GPTR_ARRAY && p->elem_size == 1) {
		if (p->index < 0 || p->index >= p->length) return "";
		return (char const*)p->base + p->index;
	}
	return (char const*)p->base;
}

static int saved_argc = 0;
static char** saved_argv = NULL;

void ginc_set_args(int argc, char** argv) {
	saved_argc = argc;
	saved_argv = argv;
}

int ginc_get_argc(void) {
	return saved_argc;
}

void* ginc_get_argv(void) {
	return ginc_ptr_make(saved_argv, 0, (long long)saved_argc, (int)sizeof(char*), GPTR_ARRAY);
}

void* ginc_ptr_retype(void* ptr, int elem_size) {
	GincPtr* p = (GincPtr*)ptr;
	if (!p || elem_size <= 0) return ptr;
	if (p->kind == GPTR_ARRAY && p->elem_size != elem_size) {
		long long const total_bytes = p->length * (long long)p->elem_size;
		p->elem_size = elem_size;
		p->length = total_bytes / (long long)elem_size;
	}
	return p;
}

#define MAX_LOADED_LIBS 32
static void* loaded_libs[MAX_LOADED_LIBS];
static int loaded_lib_count = 0;

#define MAX_LIB_DIRS 32
static char* extra_lib_dirs[MAX_LIB_DIRS];
static int extra_lib_dir_count = 0;
static char* runtime_exe_dir = NULL;

void ginc_add_lib_dir_impl(char const* dir) {
	if (!dir || !*dir || extra_lib_dir_count >= MAX_LIB_DIRS) return;
	extra_lib_dirs[extra_lib_dir_count++] = strdup(dir);
}

void ginc_set_exe_dir(char const* dir) {
	if (!dir || !*dir) return;
	free(runtime_exe_dir);
	runtime_exe_dir = strdup(dir);
}

static void* try_dlopen_in_dir(char const* dir, char const* lib_name) {
	if (!dir || !*dir) return NULL;
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s/%s", dir, lib_name);
	return dlopen(buf, RTLD_NOW | RTLD_GLOBAL);
}

void ginc_load_library(char const* path) {
	if (!path || !*path) return;
	void* handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);

	/* 1. -L オプション指定ディレクトリ */
	for (int i = 0; !handle && i < extra_lib_dir_count; i++) {
		handle = try_dlopen_in_dir(extra_lib_dirs[i], path);
	}

	/* 2. 環境変数 GINC_LIB_PATH (コロン区切り) */
	if (!handle) {
		char const* env_lib = getenv("GINC_LIB_PATH");
		if (env_lib && *env_lib) {
			char* copy = strdup(env_lib);
			char* tok = strtok(copy, ":");
			while (tok && !handle) {
				handle = try_dlopen_in_dir(tok, path);
				tok = strtok(NULL, ":");
			}
			free(copy);
		}
	}

	/* 3. カレント・実行バイナリ相対パス */
	if (!handle) handle = try_dlopen_in_dir("./std", path);
	if (!handle && runtime_exe_dir) {
		char rel1[1024];
		char rel2[1024];
		char rel3[1024];
		snprintf(rel1, sizeof(rel1), "%s/std", runtime_exe_dir);
		snprintf(rel2, sizeof(rel2), "%s/../lib/ginc/lib", runtime_exe_dir);
		snprintf(rel3, sizeof(rel3), "%s/../ginc/lib", runtime_exe_dir);
		handle = try_dlopen_in_dir(rel1, path);
		if (!handle) handle = try_dlopen_in_dir(rel2, path);
		if (!handle) handle = try_dlopen_in_dir(rel3, path);
	}

	/* 4. アカウントローカル (~/.local/<host>/ginc/lib, ~/.local/ginc/lib) */
	if (!handle) {
		char const* home = getenv("HOME");
		if (home && *home) {
			char host_buf[256] = {0};
			if (gethostname(host_buf, sizeof(host_buf) - 1) == 0 && host_buf[0]) {
				char* dot = strchr(host_buf, '.');
				if (dot) *dot = '\0';
				char host_lib[1024];
				snprintf(host_lib, sizeof(host_lib), "%s/.local/%s/ginc/lib", home, host_buf);
				handle = try_dlopen_in_dir(host_lib, path);
			}
			if (!handle) {
				char local_lib[1024];
				snprintf(local_lib, sizeof(local_lib), "%s/.local/ginc/lib", home);
				handle = try_dlopen_in_dir(local_lib, path);
			}
		}
	}

	/* 5. ビルド時 PREFIX デフォルト */
	if (!handle) handle = try_dlopen_in_dir(GINC_DEFAULT_LIBDIR, path);

	if (handle) {
		if (loaded_lib_count < MAX_LOADED_LIBS) {
			loaded_libs[loaded_lib_count++] = handle;
		}
		return;
	}

	fprintf(stderr, "Ga! (Runtime Error): Failed to load library '%s': %s\n", path, dlerror());
	exit(1);
}

void* ginc_resolve_symbol(char const* name) {
	if (!name) return NULL;
	for (int i = loaded_lib_count - 1; i >= 0; i--) {
		void* sym = dlsym(loaded_libs[i], name);
		if (sym) return sym;
	}
	return dlsym(RTLD_DEFAULT, name);
}
