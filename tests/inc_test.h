#pragma once

#define BASE_VAL 100
#define ADD(a, b) ((a) + (b))
#define SQR(x) ((x) * (x))
#define MAKE_VAR(prefix, num) prefix##num
#define STR(x) #x

int header_add(int x, int y) {
	return x + y;
}
