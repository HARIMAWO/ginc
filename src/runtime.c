#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
