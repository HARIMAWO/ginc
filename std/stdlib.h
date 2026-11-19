#pragma once
#pragma ginc_lib "libginc_std.so"

extern void* ginc_malloc(int size);
extern void* ginc_calloc(int nmemb, int size);
extern void ginc_free(void* ptr);
extern int ginc_atoi(char* s);
extern int ginc_abs(int x);
extern void ginc_exit(int code);

#define malloc ginc_malloc
#define calloc ginc_calloc
#define free ginc_free
#define atoi ginc_atoi
#define abs ginc_abs
#define exit ginc_exit
