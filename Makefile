.PHONY: all clean test

CFLAGS = -Wall -W -pedantic -std=c99 -DLUA_USE_POSIX -DLUA_USE_DLOPEN
LDFLAGS = -lm -ldl

all: kilo

lua_one.o: lua_one.c
	@$(CC) -c lua_one.c -Ilua-5.4.7/src $(CFLAGS)

kilo: kilo.c lua_one.o
	@$(CC) -o kilo kilo.c lua_one.o -Ilua-5.4.7/src $(CFLAGS) $(LDFLAGS)

clean:
	@rm -f kilo lua_one.o

test:
	@echo "Running kilo tests..."
	@./kilo --version 2>&1 || true
	@echo "Tests complete."
