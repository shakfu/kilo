# Syntax Highlighting and LSP Integration Research

**Date:** 2025-10-02
**For:** Loki Text Editor v0.4.0
**Current State:** Basic regex-based syntax highlighting for C/C++ only

## Executive Summary

This document evaluates high-performance syntax highlighting solutions and LSP integration options for kilo, a minimalist 1.9K-line C text editor. The goal is to find solutions that maintain kilo's philosophy of simplicity while adding modern editor capabilities.

### Current Implementation

loki uses a basic hand-coded syntax highlighter (~150 lines):
- **Method**: Manual character-by-character parsing with regex-like patterns
- **Features**: Keywords, strings, numbers, single/multi-line comments
- **Languages**: C/C++ only (hardcoded)
- **Performance**: O(n) per line, lazy evaluation, cached in `erow.hl`
- **Binary Impact**: Zero (built-in)
- **Extensibility**: Requires C code changes for each language

**Key limitations:**
1. Only one language definition (C/C++)
2. No support for complex syntax patterns
3. Manual maintenance required
4. No semantic highlighting

---

## Syntax Highlighting Solutions

### Option 1: Tree-sitter (Recommended for Advanced Use)

**What it is:** Incremental parser generator that creates concrete syntax trees. Used by Neovim, Helix, Zed, GitHub.com, and Atom.

**Pros:**
- [x] Extremely accurate syntax highlighting (AST-based, not regex)
- [x] Incremental parsing (only re-parses changed sections)
- [x] High performance even on large files
- [x] 100+ languages with community grammars
- [x] Written in pure C11, embeddable
- [x] Supports language injection (e.g., SQL in Python strings)
- [x] Can power additional features (code folding, navigation, selection)
- [x] Active development and community

**Cons:**
- [X] Significant complexity overhead (~10K lines of integration code in real editors)
- [X] Requires language grammar files (.so or .a per language)
- [X] Binary size: ~300-500KB core + ~100-200KB per language grammar
- [X] Requires query files for highlight rules (additional complexity)
- [X] Learning curve for query syntax
- [X] May be overkill for a 1.9K-line editor

**Integration Approach:**

```c
#include <tree_sitter/api.h>

// Link with language parsers
extern const TSLanguage *tree_sitter_c(void);
extern const TSLanguage *tree_sitter_python(void);

// Basic usage
TSParser *parser = ts_parser_new();
ts_parser_set_language(parser, tree_sitter_c());
TSTree *tree = ts_parser_parse_string(parser, NULL, source, strlen(source));

// Incremental edit
TSInputEdit edit = {
    .start_byte = 5,
    .old_end_byte = 10,
    .new_end_byte = 15,
    .start_point = {0, 5},
    .old_end_point = {0, 10},
    .new_end_point = {0, 15}
};
ts_tree_edit(tree, &edit);
```

**Compilation:**
```bash
clang -I tree-sitter/lib/include \
      kilo.c \
      tree-sitter-c/src/parser.c \
      -ltree-sitter \
      -o kilo
```

**Estimated Impact:**
- Binary size: +400-600KB (core + C grammar)
- Code complexity: +500-1000 lines for basic integration
- Runtime overhead: Minimal (faster than regex for complex grammars)
- Memory: ~1-2MB for typical syntax trees

**Verdict:** Excellent for a "kilo-plus" fork focused on modern features, but probably too heavy for maintaining minimalism.

---

### Option 2: TextMate Grammars + Oniguruma (Middle Ground)

**What it is:** Regex-based pattern matching system used by VS Code, Sublime Text, TextMate.

**Pros:**
- [x] 200+ languages with existing grammar files (JSON)
- [x] Simpler than Tree-sitter
- [x] Industry standard (VS Code compatibility)
- [x] Grammar files are redistributable JSON
- [x] Well-documented pattern syntax

**Cons:**
- [X] Requires Oniguruma regex library (~200KB)
- [X] Slower than Tree-sitter for complex grammars
- [X] Less accurate (regex limitations)
- [X] No incremental parsing
- [X] Grammar files can be very complex (1000+ lines)
- [X] Need JSON parser for grammar loading

**Libraries:**
- **Oniguruma** (C): ~150KB library, BSDL license
- **vscode-textmate** (TypeScript/JS): Reference implementation (not C)

**Estimated Impact:**
- Binary size: +200-300KB (Oniguruma + JSON parser)
- Code complexity: +300-500 lines
- Runtime overhead: Moderate (regex matching per line)

**Verdict:** Good compromise between power and simplicity, but still requires significant dependencies.

---

### Option 3: Syntect (Rust-based, Reference Only)

**What it is:** Rust library used by `bat` (cat clone) and other tools. Parses Sublime Text syntax definitions.

**Pros:**
- [x] Battle-tested (bat has millions of users)
- [x] Sublime Text syntax compatibility
- [x] Good performance

**Cons:**
- [X] Written in Rust (not suitable for C codebase)
- [X] Would require FFI bindings
- [X] Large Rust stdlib overhead

**Verdict:** Not suitable for C integration, but interesting reference implementation.

---

### Option 4: Enhanced Manual Highlighting (Minimal Approach)

**What it is:** Improve kilo's current approach with a data-driven design instead of full parsing library.

**Pros:**
- [x] Minimal code addition (~200-300 lines)
- [x] No external dependencies
- [x] Zero binary size increase (or +10KB for embedded grammars)
- [x] Full control and simplicity
- [x] Easy to understand and maintain

**Cons:**
- [X] Limited accuracy (regex-like patterns only)
- [X] Manual language definition required
- [X] No advanced features (semantic highlighting, injection)
- [X] Does not scale to 100+ languages

**Proposed Implementation:**

Create a simple data structure for language definitions:

```c
typedef struct {
    const char *name;
    const char **extensions;
    const char **keywords;
    const char **types;
    const char *line_comment;
    const char *block_comment_start;
    const char *block_comment_end;
    const char *string_chars;
    const char *separators;
    int flags;
} language_def;

// Define multiple languages
language_def langs[] = {
    {
        .name = "C",
        .extensions = (const char*[]){".c", ".h", NULL},
        .keywords = (const char*[]){"if", "else", "for", NULL},
        // ... rest
    },
    {
        .name = "Python",
        .extensions = (const char*[]){".py", NULL},
        .keywords = (const char*[]){"def", "class", "import", NULL},
        .line_comment = "#",
        .block_comment_start = "\"\"\"",
        .block_comment_end = "\"\"\"",
        // ...
    }
};
```

Load language definitions from external files:

```c
// .kilo/languages/python.lang
name: Python
extensions: .py .pyw
keywords: def class import if else for while
types: int str list dict
line_comment: #
block_comment: """ """
string_chars: "'"
```

**Estimated Impact:**
- Binary size: +0-20KB (if languages embedded), or 0 if loaded from files
- Code complexity: +200-300 lines
- Runtime overhead: Same as current (O(n) per line)

**Verdict:** Best option for maintaining kilo's minimalist philosophy while adding multi-language support.

---

## LSP Integration Solutions

### What is LSP?

Language Server Protocol provides:
- Auto-completion
- Go-to-definition
- Find references
- Diagnostics (errors/warnings)
- Hover information
- Code actions (refactoring)

**Architecture:** Editor (client) ↔ JSON-RPC ↔ Language Server (one per language)

---

### Option 1: lsp-framework (C++ Recommended)

**Repository:** https://github.com/leon-bckl/lsp-framework

**Pros:**
- [x] Modern C++20, type-safe
- [x] Zero dependencies (except CMake + C++20 compiler)
- [x] Automatic JSON serialization/deserialization
- [x] Supports both stdio and socket communication
- [x] Both client and server support
- [x] Static library (embeddable)
- [x] Minimal threading (async handlers only)

**Cons:**
- [X] Requires C++20 compiler (may limit portability)
- [X] Increases binary size significantly
- [X] Learning curve for LSP protocol
- [X] Adds complexity to UI (need to display diagnostics, completions, etc.)

**Integration Example:**

```cpp
#include <lsp/connection.h>
#include <lsp/message_handler.h>

// Create connection
lsp::Connection conn(lsp::ConnectionType::Stdio);

// Create message handler
lsp::MessageHandler handler;

// Register callbacks
handler.on_notification<lsp::PublishDiagnosticsNotification>(
    [](const auto& params) {
        // Display diagnostics in status bar
        for (const auto& diag : params.diagnostics) {
            fprintf(stderr, "Error at line %d: %s\n",
                    diag.range.start.line,
                    diag.message.c_str());
        }
    }
);

// Main loop
while (true) {
    conn.process_messages(handler);
    // ... handle editor input
}
```

**Estimated Impact:**
- Binary size: +500KB-1MB (LSP framework + protocol types)
- Code complexity: +800-1500 lines (protocol handling, UI integration)
- Requires C++ compiler (can link with C code)
- Need language servers installed (e.g., clangd, pyright)

**Verdict:** Powerful but heavy. Only worth it for a feature-rich fork of kilo.

---

### Option 2: LspCpp

**Repository:** https://github.com/kuafuwang/LspCpp

**Pros:**
- [x] C++ implementation
- [x] Full LSP support

**Cons:**
- [X] Depends on Boost, RapidJSON, utfcpp, uri
- [X] Heavy dependency chain
- [X] Less actively maintained

**Verdict:** Too many dependencies for kilo's philosophy.

---

### Option 3: Manual JSON-RPC over stdio (Minimal Approach)

**What it is:** Implement a minimal LSP client manually using just JSON parsing.

**Pros:**
- [x] Full control
- [x] Minimal code
- [x] Can cherry-pick LSP features (e.g., only diagnostics)

**Cons:**
- [X] Still need JSON parser (cJSON ~300 lines, or json-c ~50KB)
- [X] Significant protocol complexity
- [X] Easy to get wrong
- [X] Maintenance burden

**Example using cJSON:**

```c
#include "cJSON.h"

void send_lsp_initialize() {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "jsonrpc", 2.0);
    cJSON_AddNumberToObject(root, "id", 1);
    cJSON_AddStringToObject(root, "method", "initialize");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "processId", getpid());
    cJSON_AddItemToObject(root, "params", params);

    char *json = cJSON_Print(root);
    printf("Content-Length: %ld\r\n\r\n%s", strlen(json), json);
    fflush(stdout);

    cJSON_Delete(root);
    free(json);
}
```

**Estimated Impact:**
- Binary size: +50-100KB (JSON parser)
- Code complexity: +500-800 lines
- Limited LSP feature subset feasible

**Verdict:** Possible for basic LSP features, but still significant effort.

---

### Option 4: Lua LSP Client (Hybrid Approach)

**What it is:** Implement LSP client logic in Lua using kilo's existing Lua integration.

**Pros:**
- [x] Leverages existing Lua runtime
- [x] No additional C dependencies
- [x] Easy to prototype and iterate
- [x] Can use Lua JSON libraries
- [x] Async I/O already available in kilo

**Cons:**
- [X] Performance overhead for JSON parsing in Lua
- [X] Lua JSON library needed (can be pure Lua)
- [X] Still complex protocol handling

**Example Lua implementation:**

```lua
-- .kilo/lsp.lua
local json = require('dkjson')  -- Pure Lua JSON library

function lsp_initialize()
    local msg = {
        jsonrpc = "2.0",
        id = 1,
        method = "initialize",
        params = {
            processId = get_process_id(),
            rootUri = "file://" .. get_cwd(),
            capabilities = {}
        }
    }

    local json_str = json.encode(msg)
    local header = string.format("Content-Length: %d\r\n\r\n", #json_str)

    -- Send to LSP server via pipe
    lsp_send(header .. json_str)
end

function lsp_handle_diagnostics(params)
    for _, diag in ipairs(params.diagnostics) do
        kilo.status(string.format("Line %d: %s",
                                 diag.range.start.line + 1,
                                 diag.message))
    end
end
```

**Estimated Impact:**
- Binary size: +0KB (pure Lua implementation)
- Code complexity: +100 lines C (pipe I/O), +400 lines Lua (protocol)
- Performance: Acceptable for typical LSP usage

**Verdict:** Creative approach that fits kilo's Lua-scriptable design. Best LSP option for kilo.

---

## Recommendations

### For Maintaining loki's Minimalism

**1. Enhanced Manual Syntax Highlighting (Option 4)**
- Add data-driven language definitions
- Load from `.kilo/languages/*.lang` files
- Support 10-20 popular languages
- Keep implementation under 300 lines
- **Impact:** Minimal, in line with kilo's philosophy

**2. Lua LSP Client (Option 4)**
- Implement LSP protocol in Lua
- Focus on diagnostics + completion only
- Use existing async HTTP infrastructure for communication
- Make it optional via configuration
- **Impact:** Zero binary size, moderate Lua code

**Implementation priority:**
1. Add multi-language syntax highlighting first (easier, more visible)
2. Add LSP support later as experimental Lua module

---

### For a "loki Plus" Fork

If forking kilo for a more feature-rich editor:

**1. Tree-sitter for Syntax Highlighting**
- Best-in-class accuracy and performance
- Enables advanced features (folding, smart selection)
- Wide language support

**2. lsp-framework for LSP**
- Clean C++20 API
- Minimal dependencies
- Production-ready

**Trade-off:** Binary grows from ~72KB to ~1-2MB, but gains modern editor capabilities.

---

## Next Steps

### Immediate (Phase 1): Multi-language Syntax Highlighting

1. Refactor current syntax highlighting to be data-driven
2. Create `.kilo/languages/` directory structure
3. Add 5-10 language definitions (Python, JS, Rust, Go, etc.)
4. Test with existing files

**Estimated time:** 2-4 hours
**Lines of code:** ~250 lines
**Risk:** Low

### Future (Phase 2): Experimental LSP Support

1. Create `.kilo/lsp.lua` module
2. Implement basic JSON-RPC in Lua
3. Add diagnostics display in status bar
4. Test with clangd (C/C++ server)

**Estimated time:** 8-12 hours
**Lines of code:** ~100 C, ~400 Lua
**Risk:** Medium (protocol complexity)

---

## References

- **Tree-sitter:** https://tree-sitter.github.io/
- **Tree-sitter C API:** https://tree-sitter.github.io/tree-sitter/using-parsers/1-getting-started.html
- **LSP Specification:** https://microsoft.github.io/language-server-protocol/
- **lsp-framework:** https://github.com/leon-bckl/lsp-framework
- **Helix Editor:** https://helix-editor.com/ (Tree-sitter + LSP reference)
- **Bat (syntect):** https://github.com/sharkdp/bat
- **TextMate Grammars:** https://macromates.com/manual/en/language_grammars
- **Oniguruma:** https://github.com/kkos/oniguruma

---

## Appendix: Current loki Syntax Highlighting Code

Current implementation (~150 lines in `kilo.c`):

**Data structures:**
```c
struct t_editor_syntax {
    char **filematch;         // File extensions
    char **keywords;          // Keyword list
    char singleline_comment_start[2];  // "//"
    char multiline_comment_start[3];   // "/*"
    char multiline_comment_end[3];     // "*/"
    char *separators;         // Configurable per language
    int flags;                // HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
};

typedef struct t_erow {
    char *chars;              // Raw text
    char *render;             // Rendered (tabs expanded)
    unsigned char *hl;        // Highlight type per character
    int hl_oc;                // Had open comment at line end
} t_erow;
```

**Highlighting algorithm:**
- Single pass, character-by-character
- State machine: `in_string`, `in_comment`, `prev_sep`
- Multi-line comment tracking via `hl_oc` in previous row
- Keyword matching: memcmp with separator checking
- O(n) complexity per line
- Lazy: only runs on row update

**Current languages:** C/C++ only

**Total LOC:** ~150 lines (including `editor_update_syntax()` + color mapping)

---

## Conclusion

loki's current syntax highlighting is elegant but limited to C/C++. The recommended path forward:

1. **Short term:** Enhance current approach with data-driven language definitions (low complexity, high value)
2. **Medium term:** Experimental Lua-based LSP client (leverages existing infrastructure)
3. **Long term:** Consider Tree-sitter for a feature-rich fork if kilo's scope expands

This preserves kilo's minimalist philosophy while enabling practical multi-language editing.
