#include <regex.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
typedef enum { VAL_INT, VAL_STR, VAL_FUNC } ValType;
typedef struct { ValType type; union { int i; char* s; void* f; } data; } GincValue;
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

GincValue* ginc_make_int(int v) {
	GincValue* val = malloc(sizeof(GincValue));
	val->type = VAL_INT;
	val->data.i = v;
	return val;
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
	if (val->type == VAL_INT) {
		printf("%d\n", val->data.i);
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
