LLVM_CONFIG = llvm-config-devel

LLVM_CFLAGS = `$(LLVM_CONFIG) --cflags`
# executionengine, mcjit, native を追加
LLVM_LDFLAGS = `$(LLVM_CONFIG) --ldflags --libs core analysis executionengine mcjit native` -lpthread -lz -lm

CC = clang
RE2C = re2c
BISON = bison
CFLAGS = -g -Wall -I./src $(LLVM_CFLAGS)

OBJS = src/main.o src/lexer.o src/parser.o src/ast.o src/codegen.o src/runtime.o

ginc: $(OBJS)
	$(CC) -o$@ $(OBJS) -Wl,-export-dynamic $(LLVM_LDFLAGS)

src/parser.c src/parser.h: src/parser.y
	$(BISON) -d -o src/parser.c src/parser.y

src/lexer.c: src/lexer.re src/parser.h
	$(RE2C) -o$@ src/lexer.re

src/lexer.o: src/lexer.c src/parser.h
src/parser.o: src/parser.c src/ast.h src/parser.h
src/main.o: src/main.c src/ast.h src/codegen.h
src/ast.o: src/ast.c src/ast.h
src/codegen.o: src/codegen.c src/codegen.h src/ast.h
src/runtime.o: src/runtime.c

clean:
	rm -f ginc src/*.o src/lexer.c src/parser.c src/parser.h
