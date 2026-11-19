#pragma once
#pragma ginc_lib "libginc_std.so"

extern int ginc_isalpha(int c);
extern int ginc_isdigit(int c);
extern int ginc_isalnum(int c);
extern int ginc_isspace(int c);
extern int ginc_isupper(int c);
extern int ginc_islower(int c);
extern int ginc_toupper(int c);
extern int ginc_tolower(int c);

#define isalpha ginc_isalpha
#define isdigit ginc_isdigit
#define isalnum ginc_isalnum
#define isspace ginc_isspace
#define isupper ginc_isupper
#define islower ginc_islower
#define toupper ginc_toupper
#define tolower ginc_tolower
