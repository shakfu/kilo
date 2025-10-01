.PHONY: all clean test

# Detect Homebrew prefix
HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo "/usr/local")

# Try LuaJIT first, fall back to Lua
LUAJIT_PREFIX := $(shell brew --prefix luajit 2>/dev/null)
LUA_PREFIX := $(shell brew --prefix lua 2>/dev/null)

# Use LuaJIT if available, otherwise Lua
ifneq ($(LUAJIT_PREFIX),)
    LUA_INCDIR = $(LUAJIT_PREFIX)/include/luajit-2.1
    LUA_LIBDIR = $(LUAJIT_PREFIX)/lib
    LUA_LIB = -lluajit-5.1
    LUA_VARIANT = LuaJIT
#     $(info Using LuaJIT from $(LUAJIT_PREFIX))
else ifneq ($(LUA_PREFIX),)
    LUA_INCDIR = $(LUA_PREFIX)/include/lua
    LUA_LIBDIR = $(LUA_PREFIX)/lib
    LUA_LIB = -llua
    LUA_VARIANT = Lua
#     $(info Using Lua from $(LUA_PREFIX))
else
    $(error Neither Lua nor LuaJIT found. Install with: brew install luajit)
endif

# libcurl from Homebrew
CURL_PREFIX := $(shell brew --prefix curl 2>/dev/null)
ifneq ($(CURL_PREFIX),)
    CURL_INCDIR = $(CURL_PREFIX)/include
    CURL_LIBDIR = $(CURL_PREFIX)/lib
#     $(info Using libcurl from $(CURL_PREFIX))
else
    $(error libcurl not found. Install with: brew install curl)
endif

# Compiler flags
CFLAGS = -Wall -W -pedantic -std=c99 \
         -I$(LUA_INCDIR) \
         -I$(CURL_INCDIR)

# Linker flags (static linking where possible)
LDFLAGS = -L$(LUA_LIBDIR) -L$(CURL_LIBDIR) \
          $(LUA_LIB) \
          -lcurl \
          -lm -ldl -lpthread

all: kilo

kilo: kilo.c
	@echo "Building kilo with $(LUA_VARIANT) and LibCURL..."
	@$(CC) -o kilo kilo.c $(CFLAGS) $(LDFLAGS)
	@echo "Build complete!"

clean:
	@rm -f kilo

test:
	@echo "Running kilo tests..."
	@./kilo --version 2>&1 || true
	@echo "Tests complete."

# Show configuration
show-config:
	@echo "Homebrew prefix: $(HOMEBREW_PREFIX)"
	@echo "Lua include:     $(LUA_INCDIR)"
	@echo "Lua library:     $(LUA_LIBDIR)"
	@echo "Curl include:    $(CURL_INCDIR)"
	@echo "Curl library:    $(CURL_LIBDIR)"
