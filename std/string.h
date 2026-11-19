#pragma once
#pragma ginc_lib "libginc_std.so"

extern int ginc_strlen(char* s);
extern int ginc_strcmp(char* a, char* b);
extern int ginc_strncmp(char* a, char* b, int n);
extern char* ginc_strcpy(char* dst, char* src);
extern char* ginc_strncpy(char* dst, char* src, int n);
extern char* ginc_strcat(char* dst, char* src);
extern char* ginc_strchr(char* s, int c);
extern char* ginc_strstr(char* haystack, char* needle);
extern char* ginc_strdup(char* src);
extern void* ginc_memcpy(void* dst, void* src, int n);
extern void* ginc_memset(void* dst, int c, int n);
extern int ginc_memcmp(void* a, void* b, int n);

#define strlen ginc_strlen
#define strcmp ginc_strcmp
#define strncmp ginc_strncmp
#define strcpy ginc_strcpy
#define strncpy ginc_strncpy
#define strcat ginc_strcat
#define strchr ginc_strchr
#define strstr ginc_strstr
#define strdup ginc_strdup
#define memcpy ginc_memcpy
#define memset ginc_memset
#define memcmp ginc_memcmp
