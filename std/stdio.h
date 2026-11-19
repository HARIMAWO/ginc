#pragma once
#pragma ginc_lib "libginc_std.so"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern int ginc_printf(char* fmt, ...);
extern int ginc_fprintf(FILE* fp, char* fmt, ...);
extern int ginc_sprintf(char* dst, char* fmt, ...);
extern int ginc_snprintf(char* dst, int size, char* fmt, ...);
extern int ginc_puts(char* s);
extern int ginc_putchar(int c);
extern int ginc_getchar(void);
extern FILE* ginc_fopen(char* path, char* mode);
extern int ginc_fclose(FILE* fp);
extern int ginc_fputs(char* s, FILE* fp);
extern char* ginc_fgets(char* buf, int size, FILE* fp);
extern int ginc_fread(void* ptr, int size, int nmemb, FILE* fp);
extern int ginc_fwrite(void* ptr, int size, int nmemb, FILE* fp);
extern int ginc_fseek(FILE* fp, long offset, int whence);
extern long ginc_ftell(FILE* fp);
extern int ginc_fflush(FILE* fp);

#define printf ginc_printf
#define fprintf ginc_fprintf
#define sprintf ginc_sprintf
#define snprintf ginc_snprintf
#define puts ginc_puts
#define putchar ginc_putchar
#define getchar ginc_getchar
#define fopen ginc_fopen
#define fclose ginc_fclose
#define fputs ginc_fputs
#define fgets ginc_fgets
#define fread ginc_fread
#define fwrite ginc_fwrite
#define fseek ginc_fseek
#define ftell ginc_ftell
#define fflush ginc_fflush
