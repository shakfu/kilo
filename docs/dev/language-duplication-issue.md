# Language Definition Duplication Issue

**Status:** ⚠️ ARCHITECTURAL DEBT
**Severity:** MEDIUM
**Discovered:** 2025-10-12
**Affects:** Language syntax highlighting definitions

---

## Problem Statement

Language definitions are **duplicated** between C code and Lua scripts:

```text
src/loki_languages.c (C code)           .loki/languages/*.lua (Lua scripts)
├── Python (keywords, extensions)   ←→  ├── python.lua (DUPLICATE)
├── Lua (keywords, extensions)      ←→  ├── lua.lua (DUPLICATE)
├── JavaScript                      ←→  ├── javascript.lua (DUPLICATE)
└── Rust                            ←→  └── rust.lua (DUPLICATE)
```

**Example Duplication:**

**C code (src/loki_languages.c:52-63):**

```c
char *Python_HL_extensions[] = {".py",".pyw",NULL};
char *Python_HL_keywords[] = {
    "False","None","True","and","as","assert","async","await","break",
    "class","continue","def","del","elif","else","except","finally",
    "for","from","global","if","import","in","is","lambda","nonlocal",
    "not","or","pass","raise","return","try","while","with","yield",

    "int|","float|","str|","bool|","list|","dict|","tuple|","set|",
    "frozenset|","bytes|","bytearray|","object|","type|",NULL
};
```

**Lua code (.loki/languages/python.lua:2-15):**

```lua
loki.register_language({
    name = "Python",
    extensions = {".py", ".pyw"},
    keywords = {
        "and", "as", "assert", "async", "await", "break", "class", "continue",
        "def", "del", "elif", "else", "except", "finally", "for", "from",
        "global", "if", "import", "in", "is", "lambda", "nonlocal", "not",
        "or", "pass", "raise", "return", "try", "while", "with", "yield"
    },
    types = {
        "int", "str", "list", "dict", "tuple", "set", "bool", "float",
        "bytes", "bytearray", "complex", "frozenset", "object", "None",
        "True", "False", "self", "cls"
    },
    ...
})
```

---

## Current State

### Languages Defined in BOTH Locations (Duplicated)

| Language | C Source | Lua Source | Status |
|----------|----------|------------|--------|
| Python | `loki_languages.c:52` | `python.lua` | ❌ DUPLICATE |
| Lua | `loki_languages.c:67` | `lua.lua` | ❌ DUPLICATE |
| JavaScript | `loki_languages.c:109` | `javascript.lua` | ❌ DUPLICATE |
| Rust | `loki_languages.c` | `rust.lua` | ❌ DUPLICATE |

### Languages in C Only

- C/C++ (`.c`, `.h`, `.cpp`, `.hpp`, `.cc`)
- Cython (`.pyx`, `.pxd`, `.pxi`)
- Markdown (`.md`, `.markdown`)
- TypeScript (`.ts`, `.tsx`)
- Swift (`.swift`)
- SQL (`.sql`)
- Shell (`.sh`, `.bash`)

### Languages in Lua Only

- Go (`.go`)
- Java (`.java`)

---

## Impact

### Maintenance Burden

1. **Double updates required**: Adding a keyword means updating both C and Lua
2. **Consistency risk**: Definitions can diverge (already have - Lua has `complex` and `self`, C doesn't)
3. **Testing complexity**: Must test both paths work identically
4. **Documentation confusion**: Which is authoritative?

### Current Inconsistencies Detected

**Python types differ:**

```c
// C version (loki_languages.c:60-62)
"int|","float|","str|","bool|","list|","dict|","tuple|","set|",
"frozenset|","bytes|","bytearray|","object|","type|",NULL

// Lua version (python.lua:11-14) - HAS MORE TYPES
"int", "str", "list", "dict", "tuple", "set", "bool", "float",
"bytes", "bytearray", "complex", "frozenset", "object", "None",
"True", "False", "self", "cls"
```

**Lua keywords differ:**

```c
// C version (loki_languages.c:74-78) - built-in functions as types
"assert|","collectgarbage|","dofile|","error|","getmetatable|",
"ipairs|","load|","loadfile|","next|","pairs|","pcall|","print|",
...

// Lua version (lua.lua:10-12) - types only
"string", "number", "boolean", "table", "function", "thread",
"userdata", "self"
```

**This proves the duplication is already causing problems!**

---

## Root Cause

The duplication exists because of **unclear architecture evolution**:

1. **Original design**: All languages compiled into C (fast, no dependencies)
2. **Added feature**: Dynamic language registration via Lua (user-extensible)
3. **Migration started**: Some languages moved to Lua
4. **Migration incomplete**: Didn't remove C definitions
5. **Result**: Languages exist in both places

**No clear policy** on:

- Which languages should be built-in vs dynamic?
- What happens if a language is defined in both?
- Which definition takes precedence?

---

## Solution Options

### Option 1: All Languages in Lua (Recommended) ⭐

**Move all language definitions to Lua, keep only C infrastructure.**

**Pros:**

- ✅ Single source of truth (`.loki/languages/*.lua`)
- ✅ User-extensible without recompilation
- ✅ Easy to add/modify languages
- ✅ Consistent with project philosophy (Lua-powered)
- ✅ Already have the infrastructure (`loki.register_language()`)

**Cons:**

- ⚠️ Requires `.loki/languages/` directory to exist
- ⚠️ Small startup time to load languages (negligible)
- ⚠️ Editor won't work without Lua files (but already requires Lua anyway)

**Implementation:**

```c
// src/loki_languages.c - MINIMAL VERSION
// Keep ONLY the registration infrastructure, NO language definitions

struct t_editor_syntax HLDB[] = {
    // Empty - all languages loaded from Lua
    {NULL, NULL, "", "", "", NULL, 0, HL_TYPE_C}
};

void init_builtin_languages(void) {
    // Load languages from .loki/languages/ or fallback locations
    // This is already implemented via languages.load_all() in init.lua
}
```

```lua
-- .loki/init.lua - Load all languages
languages = require("languages")
languages.load_all()  -- Already does this!
```

**Files to change:**

1. Delete from `src/loki_languages.c`:
   - Python definition (lines 50-63)
   - Lua definition (lines 65-79)
   - JavaScript definition (lines 107-124)
   - Rust definition
2. Keep in `.loki/languages/`:
   - All existing `.lua` files
3. Add to `.loki/languages/`:
   - `c.lua` (move from C)
   - `markdown.lua` (move from C)
   - `cython.lua` (move from C)

**Fallback for missing `.loki/`:**

```c
// If .loki/languages/ doesn't exist, provide minimal defaults
void load_emergency_defaults(void) {
    // Register C and Markdown only (absolute essentials)
    // User sees warning: "No language definitions found, install .loki/"
}
```

---

### Option 2: All Languages in C

**Move all language definitions to C, remove Lua definitions.**

**Pros:**

- ✅ Fast (compiled-in, no runtime loading)
- ✅ No dependencies on `.loki/` directory
- ✅ Single source of truth (C code)

**Cons:**

- ❌ Not user-extensible (requires recompilation)
- ❌ Bloats binary (more keywords = larger executable)
- ❌ Against project philosophy (Lua-powered extensibility)
- ❌ Wastes the `loki.register_language()` infrastructure
- ❌ Go and Java would need to move to C

**Not Recommended:** Conflicts with the "Lua-powered" vision.

---

### Option 3: Hybrid with Clear Rules

**Keep minimal essential languages in C, everything else in Lua.**

**C definitions (Essential for editor to function):**

- C/C++ (for editing loki itself)
- Markdown (for docs)

**Lua definitions (Everything else):**

- Python, Lua, JavaScript, TypeScript, Rust, Go, Java, etc.

**Precedence rule:** Lua definitions override C definitions (if both exist)

**Pros:**

- ✅ Editor works without `.loki/` (C/Markdown highlighting)
- ✅ User-extensible (add languages via Lua)
- ✅ Clear separation (essentials vs extensions)

**Cons:**

- ⚠️ Two mechanisms to maintain
- ⚠️ Need precedence/override logic
- ⚠️ Still some duplication risk if not disciplined

**Implementation:**

```c
// src/loki_languages.c - MINIMAL ESSENTIALS ONLY
struct t_editor_syntax HLDB[] = {
    // C/C++ - essential for editing loki source
    {C_HL_extensions, C_HL_keywords, "//", "/*", "*/",
     ",.()+-/*=~%<>[];", HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS, HL_TYPE_C},

    // Markdown - essential for docs
    {MD_HL_extensions, NULL, "", "", "", "", 0, HL_TYPE_MARKDOWN},

    // Terminator
    {NULL, NULL, "", "", "", NULL, 0, HL_TYPE_C}
};
```

```lua
-- .loki/languages/ - ALL OTHER LANGUAGES
-- python.lua, lua.lua, javascript.lua, go.lua, rust.lua, java.lua, etc.
```

---

### Option 4: C Definitions as Templates, Lua as Overrides

**C provides default language definitions, Lua can override/extend them.**

**Pros:**

- ✅ Works out of box (C defaults)
- ✅ Customizable (Lua overrides)
- ✅ Graceful degradation (no `.loki/` needed)

**Cons:**

- ⚠️ Complex precedence logic
- ⚠️ Still have duplication in codebase
- ⚠️ Harder to reason about (which definition is active?)

**Not Recommended:** Adds complexity without clear benefit.

---

## Recommended Solution: Option 1 (All Languages in Lua)

### Rationale

1. **Aligns with project vision**: "Lua-powered text editor"
2. **Already have infrastructure**: `loki.register_language()` works perfectly
3. **User-extensible**: Add languages without recompilation
4. **Single source of truth**: All definitions in `.loki/languages/`
5. **Cleaner architecture**: C provides infrastructure, Lua provides data

### Migration Plan

#### Phase 1: Document Current State ✅

- [x] Identify all duplicated languages
- [x] Document inconsistencies
- [x] Create this analysis document

#### Phase 2: Remove C Definitions (Week 1)

**Step 1: Remove duplicates from C**

```bash
# Edit src/loki_languages.c
# Delete these sections:
# - Python definition (lines 50-63, 276-278)
# - Lua definition (lines 65-79, 284-287)
# - JavaScript definition (lines 107-124)
# - Rust definition
# Keep only: C/C++, Cython, Markdown (essentials)
```

**Step 2: Ensure Lua versions are complete**

```bash
# Verify .loki/languages/ has:
# - python.lua ✓
# - lua.lua ✓
# - javascript.lua ✓
# - rust.lua ✓
# - go.lua ✓
# - java.lua ✓
```

**Step 3: Test that nothing breaks**

```bash
make test
./build/loki-editor test.py  # Should still highlight Python
./build/loki-editor test.lua # Should still highlight Lua
```

#### Phase 3: Move Remaining C Definitions to Lua (Week 2)

**Create new Lua files:**

- `.loki/languages/c.lua` (move C/C++ definition)
- `.loki/languages/markdown.lua` (already exists in modules?)
- `.loki/languages/cython.lua`
- `.loki/languages/typescript.lua`
- `.loki/languages/swift.lua`
- `.loki/languages/sql.lua`
- `.loki/languages/shell.lua`

**Empty out C definitions:**

```c
// src/loki_languages.c - INFRASTRUCTURE ONLY
struct t_editor_syntax HLDB[] = {
    // All languages loaded from Lua
    {NULL, NULL, "", "", "", NULL, 0, HL_TYPE_C}
};
```

#### Phase 4: Add Fallback Mechanism (Week 2)

**For systems without `.loki/` directory:**

```c
// src/loki_languages.c
void load_emergency_defaults(editor_ctx_t *ctx) {
    if (!language_registry_has_any()) {
        // Register absolute minimum: C and plaintext
        editor_set_status_msg(ctx,
            "Warning: No language definitions found. "
            "Install .loki/languages/ for syntax highlighting.");

        // Register C minimally (so loki source is readable)
        register_emergency_c_language();
    }
}
```

**In init.lua:**

```lua
-- Provide helpful error if languages don't load
local lang_count = languages.load_all()
if lang_count == 0 then
    loki.status("Warning: No languages loaded from .loki/languages/")
end
```

#### Phase 5: Update Documentation (Week 2)

**Update files:**

- `CLAUDE.md` - Document new architecture
- `README.md` - Installation now requires `.loki/languages/`
- `.loki/languages/README.md` - Explain how to add languages
- `docs/dev/language_api_design.md` - Update with new approach

**Installation instructions:**

```bash
# README.md - Installation section
git clone https://github.com/user/loki
cd loki
make

# Install language definitions (required for syntax highlighting)
cp -r .loki.example .loki
# OR create symlink to system location:
ln -s /usr/share/loki/languages ~/.loki/languages
```

#### Phase 6: Validation (Week 3)

**Tests to run:**

- [ ] All test suites pass (`make test`)
- [ ] Python highlighting works (`.py` files)
- [ ] Lua highlighting works (`.lua` files)
- [ ] JavaScript highlighting works (`.js` files)
- [ ] C highlighting works (`.c` files)
- [ ] Editor works without `.loki/` (with warning)
- [ ] Editor works with partial `.loki/` (some languages)
- [ ] Custom user languages can be added

**Benchmarks:**

- [ ] Startup time with 20 languages < 50ms
- [ ] No performance regression in highlighting

---

## Alternative Quick Fix (If Full Migration is Deferred)

If the full migration is too much work right now, apply this **immediate fix**:

### Delete Lua Duplicates, Keep C Definitions

**Rationale:** C definitions are already compiled and tested. Remove Lua duplicates to eliminate duplication immediately.

**Action:**

```bash
# Delete these files (they duplicate C definitions):
rm .loki/languages/python.lua
rm .loki/languages/lua.lua
rm .loki/languages/javascript.lua
rm .loki/languages/rust.lua  # if it duplicates C

# Keep only truly dynamic languages:
# .loki/languages/go.lua (not in C)
# .loki/languages/java.lua (not in C)
```

**Update `.loki/init.lua`:**

```lua
-- languages.load_all() will now only load Go and Java
-- Python, Lua, JavaScript, Rust come from C (compiled-in)
local lang_count = languages.load_all()
loki.status(string.format("Loaded %d dynamic languages", lang_count))
```

**Pros:**

- ✅ 15 minutes to implement
- ✅ Eliminates duplication immediately
- ✅ No risk (C definitions already work)

**Cons:**

- ⚠️ Doesn't achieve "all in Lua" vision
- ⚠️ Temporary fix, not long-term solution

**Use this if:** You want to eliminate duplication NOW and migrate properly later.

---

## Decision Record

**Date:** TBD
**Decision:** [To be decided by project maintainer]
**Chosen Approach:** [Option 1 / Option 3 / Quick Fix]
**Rationale:** [Why this approach was chosen]
**Migration Timeline:** [Start date - End date]
**Responsible:** [Who will implement]

---

## References

- **Source Code:**
  - `src/loki_languages.c` - C language definitions
  - `.loki/languages/*.lua` - Lua language definitions
  - `src/loki_lua.c` - `loki.register_language()` implementation

- **Documentation:**
  - `CLAUDE.md:137-158` - Language registration API
  - `.loki/languages/README.md` - How to add languages
  - `docs/dev/language_api_design.md` - API design rationale

- **Related Issues:**
  - Code Review (CODE_REVIEW.md) - Identified this issue

---

## Next Steps

1. **Decide** which option to pursue (recommend Option 1)
2. **Create task list** for chosen option
3. **Schedule** migration work
4. **Implement** in phases
5. **Test** thoroughly
6. **Document** new architecture
7. **Update** installation instructions

**Estimated Effort:**

- Option 1 (Full migration): 12-16 hours
- Option 3 (Hybrid): 8-10 hours
- Quick Fix: 1 hour

**Recommended Priority:** MEDIUM (not blocking, but should be fixed soon)

---

**Conclusion:** This duplication is fixable architectural debt. The recommended solution (Option 1: All Languages in Lua) aligns with the project's vision of being Lua-powered and user-extensible. It will result in cleaner architecture, easier maintenance, and better user experience.
