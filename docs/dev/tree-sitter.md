# Tree-Sitter Analysis

> I've been looking at the tree-sitter parsing and high-lighting solution.
How would this be integrated in an experimental variant of existing code?

⏺ I'll analyze how tree-sitter could be integrated into loki as an
  experimental variant while preserving the existing simple highlighting
  system.

## Tree-Sitter Integration Analysis

### Current Architecture

Existing syntax highlighting (src/loki_languages.c, src/loki_core.c):
- Per-row pattern matching with editor_update_syntax()
- Simple keyword/string/comment detection
- ~160 lines of core logic
- Zero external dependencies
- Fast but limited accuracy

### Tree-Sitter Architecture

Tree-sitter provides:
- Incremental parsing - rebuilds only changed portions of syntax tree
- Grammar-based highlighting - accurate token classification (e.g.,
function names vs variables)
- Query system - S-expression patterns for extracting highlight regions
- Language grammars - Pre-built parsers as shared libraries (.so/.dylib)

### Integration Strategy: Dual-Mode Architecture

#### Proposal: Keep both systems, allow opt-in per language.

```c
/* loki_internal.h - Add tree-sitter support flag */
struct t_editor_syntax {
    char **filematch;
    char **keywords;
    /* ... existing fields ... */

    /* Tree-sitter support (optional) */
    int use_treesitter;              /* Flag: 1 = use tree-sitter, 0 =
legacy */
    const char *ts_language_name;    /* e.g., "c", "python", "javascript"
*/
    const char *ts_highlight_query;  /* Optional: custom highlight query
*/
};

/* New tree-sitter state in editor context */
struct editor_ctx {
    /* ... existing fields ... */

    /* Tree-sitter parser state (NULL if not using tree-sitter) */
    void *ts_parser;      /* TSParser* - opaque to avoid header dependency
 */
    void *ts_tree;        /* TSTree* - current parse tree */
    void *ts_query;       /* TSQuery* - highlight query */
};
```

### Implementation Modules

New file: src/loki_treesitter.c (~300-400 lines)
```c
/* loki_treesitter.c - Tree-sitter integration layer */
#ifdef LOKI_HAVE_TREESITTER

#include <tree_sitter/api.h>
#include "loki_treesitter.h"
#include "loki_internal.h"

/* Language grammar declarations (linked as .so/.dylib) */
extern const TSLanguage *tree_sitter_c(void);
extern const TSLanguage *tree_sitter_python(void);
extern const TSLanguage *tree_sitter_javascript(void);

/* Map language name to grammar */
static const TSLanguage* ts_get_language(const char *name) {
    if (strcmp(name, "c") == 0) return tree_sitter_c();
    if (strcmp(name, "python") == 0) return tree_sitter_python();
    if (strcmp(name, "javascript") == 0) return tree_sitter_javascript();
    return NULL;
}

/* Initialize tree-sitter parser for current file */
int ts_init_parser(editor_ctx_t *ctx) {
    if (!ctx->syntax || !ctx->syntax->use_treesitter) {
        return 0;  /* Not using tree-sitter for this file */
    }

    const TSLanguage *lang =
ts_get_language(ctx->syntax->ts_language_name);
    if (!lang) {
        /* Fallback to legacy highlighting */
        ctx->syntax->use_treesitter = 0;
        return 0;
    }

    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, lang);
    ctx->ts_parser = parser;

    /* Parse entire buffer initially */
    char *source = editor_rows_to_string(ctx, NULL);
    if (source) {
        TSTree *tree = ts_parser_parse_string(parser, NULL, source,
strlen(source));
        ctx->ts_tree = tree;
        free(source);
    }

    /* Load highlight query (queries/highlights.scm from grammar) */
    /* This would load a query like:
     *   (function_declarator declarator: (identifier) @function)
     *   (string_literal) @string
     *   (comment) @comment
     */
    ts_load_highlight_query(ctx, ctx->syntax->ts_language_name);

    return 1;
}

/* Update tree after edit */
void ts_update_tree(editor_ctx_t *ctx, int start_row, int start_col,
                    int old_end_row, int old_end_col,
                    int new_end_row, int new_end_col) {
    if (!ctx->ts_tree) return;

    TSInputEdit edit = {
        .start_byte = /* calculate from row/col */,
        .old_end_byte = /* calculate */,
        .new_end_byte = /* calculate */,
        /* ... point coordinates ... */
    };

    ts_tree_edit(ctx->ts_tree, &edit);

    /* Reparse with edit hint (incremental) */
    char *source = editor_rows_to_string(ctx, NULL);
    TSTree *new_tree = ts_parser_parse_string(ctx->ts_parser,
ctx->ts_tree,
                                               source, strlen(source));
    ts_tree_delete(ctx->ts_tree);
    ctx->ts_tree = new_tree;
    free(source);
}

/* Apply tree-sitter highlights to row */
void ts_highlight_row(editor_ctx_t *ctx, t_erow *row) {
    if (!ctx->ts_tree || !ctx->ts_query) {
        return;  /* Fallback to legacy */
    }

    TSNode root = ts_tree_root_node(ctx->ts_tree);
    TSQueryCursor *cursor = ts_query_cursor_new();

    /* Query captures for this row's byte range */
    uint32_t start_byte = /* calculate row start byte offset */;
    uint32_t end_byte = start_byte + row->rsize;

    ts_query_cursor_exec(cursor, ctx->ts_query, root);
    ts_query_cursor_set_byte_range(cursor, start_byte, end_byte);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        for (int i = 0; i < match.capture_count; i++) {
            TSQueryCapture cap = match.captures[i];

            /* Get capture name (e.g., "function", "string", "comment") */
            uint32_t length;
            const char *name = ts_query_capture_name_for_id(ctx->ts_query,

                                                             cap.index,
&length);

            /* Map tree-sitter capture to HL_* constant */
            int hl_type = ts_capture_to_hl(name);

            /* Apply highlight to range */
            TSNode node = cap.node;
            uint32_t node_start = ts_node_start_byte(node);
            uint32_t node_end = ts_node_end_byte(node);

            /* Convert byte offsets to column positions in this row */
            int col_start = node_start - start_byte;
            int col_end = node_end - start_byte;

            /* Clamp to row bounds */
            if (col_start < 0) col_start = 0;
            if (col_end > row->rsize) col_end = row->rsize;

            /* Apply highlight */
            memset(row->hl + col_start, hl_type, col_end - col_start);
        }
    }

    ts_query_cursor_delete(cursor);
}

/* Map tree-sitter capture names to existing HL_* constants */
static int ts_capture_to_hl(const char *capture_name) {
    if (strcmp(capture_name, "function") == 0) return HL_KEYWORD1;
    if (strcmp(capture_name, "type") == 0) return HL_KEYWORD2;
    if (strcmp(capture_name, "string") == 0) return HL_STRING;
    if (strcmp(capture_name, "comment") == 0) return HL_COMMENT;
    if (strcmp(capture_name, "number") == 0) return HL_NUMBER;
    if (strcmp(capture_name, "keyword") == 0) return HL_KEYWORD1;
    if (strcmp(capture_name, "operator") == 0) return HL_NORMAL;
    if (strcmp(capture_name, "variable") == 0) return HL_NORMAL;
    return HL_NORMAL;
}

/* Cleanup */
void ts_cleanup_parser(editor_ctx_t *ctx) {
    if (ctx->ts_query) {
        ts_query_delete(ctx->ts_query);
        ctx->ts_query = NULL;
    }
    if (ctx->ts_tree) {
        ts_tree_delete(ctx->ts_tree);
        ctx->ts_tree = NULL;
    }
    if (ctx->ts_parser) {
        ts_parser_delete(ctx->ts_parser);
        ctx->ts_parser = NULL;
    }
}

#endif /* LOKI_HAVE_TREESITTER */

Modified Core Integration

In src/loki_core.c:

/* editor_update_syntax() - dispatch to tree-sitter or legacy */
void editor_update_syntax(editor_ctx_t *ctx, t_erow *row) {
#ifdef LOKI_HAVE_TREESITTER
    if (ctx->syntax && ctx->syntax->use_treesitter && ctx->ts_tree) {
        /* Use tree-sitter highlighting */
        ts_highlight_row(ctx, row);
        return;
    }
#endif

    /* Fallback to legacy regex-style highlighting */
    editor_update_syntax_legacy(ctx, row);  /* Rename existing function */
}

/* Character insertion - notify tree-sitter of edit */
void editor_insert_char(editor_ctx_t *ctx, int c) {
    /* ... existing insertion logic ... */

#ifdef LOKI_HAVE_TREESITTER
    if (ctx->ts_tree) {
        /* Incremental update: inserted 1 char at (cy, cx-1) */
        ts_update_tree(ctx, ctx->cy, ctx->cx - 1,
                       ctx->cy, ctx->cx - 1,  /* old end = start */
                       ctx->cy, ctx->cx);      /* new end = start + 1 char
 */
    }
#endif
}

/* Similar updates needed for editor_del_char(), editor_insert_newline(),
etc. */
```

## Build System Integration

### CMakeLists.txt additions:

```cmake
# Optional tree-sitter support
option(LOKI_USE_TREESITTER "Enable tree-sitter syntax highlighting" OFF)

if (LOKI_USE_TREESITTER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TREESITTER REQUIRED tree-sitter)

    # Find language grammars (as shared libraries)
    find_library(TS_C tree-sitter-c PATHS /usr/local/lib)
    find_library(TS_PYTHON tree-sitter-python PATHS /usr/local/lib)
    find_library(TS_JS tree-sitter-javascript PATHS /usr/local/lib)

    target_compile_definitions(libloki PRIVATE LOKI_HAVE_TREESITTER=1)
    target_include_directories(libloki PRIVATE ${TREESITTER_INCLUDE_DIRS})
    target_link_libraries(libloki PRIVATE
        ${TREESITTER_LIBRARIES}
        ${TS_C} ${TS_PYTHON} ${TS_JS}
    )

    target_sources(libloki PRIVATE src/loki_treesitter.c)
endif()
```

```sh
Usage:
# Build without tree-sitter (default, lightweight)
cmake -B build && cmake --build build

# Build with tree-sitter (experimental)
cmake -B build -DLOKI_USE_TREESITTER=ON && cmake --build build
```

### Language Definition Updates

In src/loki_languages.c:

```c
/* C language with tree-sitter support */
static struct t_editor_syntax HLDB[] = {
    {
        /* ... existing C syntax definition ... */
        .use_treesitter = 1,  /* Enable tree-sitter for C */
        .ts_language_name = "c",
        .ts_highlight_query = NULL  /* Use default from grammar */
    },
    {
        /* ... Python definition ... */
        .use_treesitter = 1,
        .ts_language_name = "python",
        .ts_highlight_query = NULL
    },
    {
        /* ... Lua definition - tree-sitter not available yet */
        .use_treesitter = 0,  /* Fallback to legacy */
        .ts_language_name = NULL,
        .ts_highlight_query = NULL
    }
};
```

### Trade-offs Analysis

| Aspect           | Legacy Highlighting             | Tree-Sitter
                   |
|------------------|---------------------------------|--------------------
-------------------|
| Accuracy         | ~70% (keywords, strings, basic) | ~95%
(grammar-aware)                  |
| Performance      | O(n) per row, very fast         | O(log n)
incremental, slight overhead |
| Dependencies     | Zero                            | libtree-sitter +
grammar .so files    |
| Binary Size      | +0 KB                           | +500 KB (parser +
grammars)           |
| Complexity       | ~160 lines                      | ~400 lines +
integration              |
| Language Support | 8 built-in                      | Needs grammar for
each language       |
| Maintenance      | Manual keyword lists            | Grammar updates
from upstream         |


### Critical Design Questions

1. Dependency Philosophy
- Loki is currently dependency-free (except Lua/curl, added later)
- Tree-sitter adds significant dependency weight
- Alternative: Embed tree-sitter as amalgamation? (Like Lua was)
- Challenge: Grammar files are separate .so libs, can't easily embed

2. Incremental Update Complexity
- Every edit operation needs byte offset calculations
- Tree-sitter uses byte offsets, loki uses row/col
- Risk: Conversion bugs could cause incorrect highlighting
- Mitigation: Full reparse on complex edits (still fast for <10K line
files)

3. Grammar Distribution
- Where do grammar .so files come from?
- Option A: System package manager (brew install tree-sitter-c)
- Option B: Bundle with loki distribution
- Option C: Download on demand (security risk)

4. Fallback Strategy
- What if grammar not found at runtime?
- Proposal: Silent fallback to legacy highlighting
- Test: if (!ts_get_language(name)) { use_treesitter = 0; }

### Recommended Implementation Path

Phase 1: Proof of Concept (~2-3 days)
1. Add tree-sitter for C language only
2. Hard-code grammar loading (no dynamic discovery)
3. Full reparse on every edit (no incremental yet)
4. CMake flag LOKI_USE_TREESITTER defaults to OFF
5. Test with existing test suite (should still pass)

Phase 2: Incremental Updates (~2-3 days)
1. Implement ts_update_tree() for single-char edits
2. Add byte offset tracking to editor_ctx_t
3. Handle newline insertions/deletions
4. Benchmark: measure performance impact

Phase 3: Multi-Language (~1-2 days)
1. Add Python, JavaScript grammars
2. Implement grammar discovery (check /usr/local/lib for .so files)
3. Graceful fallback if grammar missing

Phase 4: Query Customization (~1-2 days)
1. Load highlight queries from .loki/queries/<lang>/highlights.scm
2. Allow users to customize capture → HL_* mappings
3. Document query syntax for custom languages

## Alternative: Hybrid Approach

Instead of full tree-sitter integration, query tree-sitter for specific
features only:

```c
/* Use tree-sitter for structural analysis, keep legacy for highlighting
*/
void editor_update_row_hybrid(editor_ctx_t *ctx, t_erow *row) {
    /* Legacy highlighting (fast) */
    editor_update_syntax_legacy(ctx, row);

    /* Tree-sitter enhancements (accurate) */
    if (ctx->ts_tree) {
        /* Override specific tokens with tree-sitter accuracy */
        ts_enhance_function_names(ctx, row);  /* function calls vs
variables */
        ts_enhance_types(ctx, row);           /* user-defined types */
        /* Leave keywords, strings, comments to legacy (already good) */
    }
}
```

Benefits:
- Best of both worlds: speed + accuracy where it matters
- Smaller tree-sitter footprint
- Simpler byte offset management

### Testing Strategy

New test file: tests/test_treesitter.c (if implemented)

```c
TEST(treesitter_init) {
    editor_ctx_t ctx;
    editor_ctx_init(&ctx);

    ctx.syntax = &HLDB[0];  /* C language */
    ASSERT_EQ(ts_init_parser(&ctx), 1);
    ASSERT_TRUE(ctx.ts_parser != NULL);
    ASSERT_TRUE(ctx.ts_tree != NULL);

    ts_cleanup_parser(&ctx);
    editor_ctx_free(&ctx);
}

TEST(treesitter_highlight_function) {
    /* Parse: "int main(void) { return 0; }" */
    /* Verify: "main" is HL_KEYWORD1 (function name) */
    /* Legacy would leave it HL_NORMAL */
}

TEST(treesitter_incremental_edit) {
    /* Start with valid C code */
    /* Insert character */
    /* Verify tree updated correctly */
    /* Verify highlights still accurate */
}
```
Lua API Extensions

Allow scripts to query syntax tree:

```lua
-- .loki/init.lua
function goto_function_definition()
    local tree = loki.get_syntax_tree()  -- TSTree wrapper
    local node = tree:named_descendant_for_point(loki.get_cursor())

    if node:type() == "function_declarator" then
        -- Jump to function definition
        local start_row, start_col = node:start_point()
        loki.set_cursor(start_row, start_col)
    end
end
```

This would enable IDE-like features without LSP complexity.

---

## My Recommendation

Don't integrate tree-sitter yet. Here's why:

1. Scope creep: Loki is a minimalist editor (<3K lines core). Tree-sitter
adds ~500+ lines + dependencies.

2. Maintenance burden: Grammar updates, platform compatibility, library
versioning.

3. Diminishing returns: Current highlighting is "good enough" for 90% of
use cases. Tree-sitter's accuracy gain doesn't justify complexity for an
editor focused on simplicity.

4. Better alternative: If accuracy is critical, users should use
LSP-enabled editors (Neovim, VS Code). Loki's niche is lightweight,
dependency-light editing.

However, if you want to experiment:
- Create loki-ts fork/branch
- Implement Phase 1 POC for C only
- Measure real-world performance impact
- Decide based on data, not theory

Would you like me to implement a minimal Phase 1 POC to evaluate
feasibility, or would you prefer to explore other enhancement directions
(LSP client, multiple buffers, undo/redo)?
