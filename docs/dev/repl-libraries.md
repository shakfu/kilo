# REPL Line Editing Libraries for C

**Date:** 2025-10-11
**Research for:** loki-repl line editing enhancement

## Overview

This document surveys available line editing libraries for C-based REPLs, comparing features, licenses, and trade-offs. The goal is to provide command history, cursor movement, and line editing capabilities for the loki Lua REPL.

## Current Implementation

**Status:** Using editline (libedit) with readline fallback

```c
#if defined(LOKI_HAVE_EDITLINE)
    #include <editline/readline.h>
#elif defined(LOKI_HAVE_READLINE)
    #include <readline/readline.h>
#endif
```

**Detection priority:**

1. editline (BSD license, built-in on macOS/BSD)
2. readline (GPL, Homebrew on macOS)
3. basic getline() fallback

## Available Libraries

### 1. linenoise

**Author:** Salvatore Sanfilippo (antirez) - creator of Redis and original kilo editor
**License:** BSD 2-Clause
**Repository:** <https://github.com/antirez/linenoise>
**Size:** ~1100 lines of code, single .c/.h file pair
**Dependencies:** None

**Features:**

- [x] Command history with persistence
- [x] Line editing (arrows, home/end, Ctrl-A/E/K/U/W)
- [x] Tab completion (customizable callback)
- [x] Hints/suggestions (fish shell style)
- [x] Multi-line editing
- [x] UTF-8 support
- [x] Raw mode terminal handling
- [x] History search (Ctrl-R)

**Platforms:**

- Linux [x]
- macOS [x]
- BSD [x]
- Windows [x] (native console API, no ANSI emulation needed)

**Used by:**

- Redis CLI
- MongoDB shell
- LevelDB tools
- Many embedded systems

**Integration example:**

```c
#include "linenoise.h"

void completion_callback(const char *buf, linenoiseCompletions *lc) {
    if (buf[0] == 'p') {
        linenoiseAddCompletion(lc, "print");
    }
}

int main() {
    linenoiseSetCompletionCallback(completion_callback);
    linenoiseHistoryLoad(".loki/repl_history");
    linenoiseHistorySetMaxLen(1000);

    char *line;
    while ((line = linenoise("loki> ")) != NULL) {
        linenoiseHistoryAdd(line);
        // Execute Lua...
        linenoiseFree(line);
    }

    linenoiseHistorySave(".loki/repl_history");
    return 0;
}
```

**Advantages:**

- Embeddable - just copy 2 files
- Zero dependencies
- Tiny footprint (~50KB compiled)
- Same author as kilo (philosophical alignment)
- Cross-platform including Windows
- Battle-tested in production (Redis, MongoDB)

**Disadvantages:**

- Less feature-rich than readline
- Smaller community than readline/editline
- No vi mode

**Recommendation:** [+][+][+][+][+] **Best fit for loki's philosophy**

---

### 2. editline (libedit)

**Maintainer:** NetBSD project
**License:** BSD 3-Clause
**Repository:** <https://github.com/NetBSD/libedit>
**Size:** ~200KB library
**Dependencies:** curses/ncurses

**Features:**

- [x] Full readline API compatibility
- [x] Command history with persistence
- [x] Line editing (all standard shortcuts)
- [x] Tab completion
- [x] Vi and Emacs modes
- [x] UTF-8 support
- [x] Signal handling

**Platforms:**

- macOS [x] (built-in system library)
- FreeBSD [x] (built-in)
- OpenBSD [x] (built-in)
- NetBSD [x] (built-in)
- Linux [x] (package: libedit-dev)
- Windows [X]

**Used by:**

- macOS system utilities
- LLDB debugger
- Fish shell (original)
- Many BSD tools

**Integration example:**

```c
#include <editline/readline.h>

int main() {
    using_history();
    read_history(".loki/repl_history");
    stifle_history(1000);

    char *line;
    while ((line = readline("loki> ")) != NULL) {
        add_history(line);
        // Execute Lua...
        free(line);
    }

    write_history(".loki/repl_history");
    return 0;
}
```

**Advantages:**

- Built-in on macOS and BSDs (no extra dependency)
- BSD license (more permissive than GPL)
- Readline-compatible API (easy migration)
- Well-maintained by NetBSD project

**Disadvantages:**

- External library (not embeddable)
- No Windows support
- Requires curses/ncurses
- Larger binary size

**Recommendation:** [+][+][+][+] **Current choice - good for Unix systems**

---

### 3. GNU Readline

**Maintainer:** Free Software Foundation
**License:** GPL v3
**Repository:** <https://git.savannah.gnu.org/cgit/readline.git>
**Size:** ~500KB library
**Dependencies:** curses/ncurses

**Features:**

- [x] Most comprehensive feature set
- [x] Command history with timestamps
- [x] Line editing (extensive)
- [x] Tab completion (programmable)
- [x] Vi and Emacs modes
- [x] Macro system
- [x] Custom key bindings
- [x] UTF-8 support
- [x] History search
- [x] Undo/redo

**Platforms:**

- Linux [x] (usually pre-installed)
- macOS [x] (via Homebrew, keg-only)
- BSD [x] (via packages)
- Windows [X] (MinGW only)

**Used by:**

- Bash shell
- GDB debugger
- Python REPL
- MySQL CLI
- PostgreSQL psql
- Hundreds of other tools

**Integration example:**

```c
#include <readline/readline.h>
#include <readline/history.h>

int main() {
    using_history();
    read_history(".loki/repl_history");
    stifle_history(1000);

    char *line;
    while ((line = readline("loki> ")) != NULL) {
        add_history(line);
        // Execute Lua...
        free(line);
    }

    write_history(".loki/repl_history");
    return 0;
}
```

**Advantages:**

- Industry standard
- Most feature-rich
- Excellent documentation
- Huge user base

**Disadvantages:**

- GPL license (viral for linking)
- Large library size
- Not embeddable
- No Windows support
- macOS requires Homebrew (keg-only due to GPL)

**Recommendation:** [+][+][+] **Good fallback, licensing concerns**

---

### 4. replxx

**Author:** Marcin Konarski
**License:** BSD 3-Clause
**Repository:** <https://github.com/AmokHuginnsson/replxx>
**Language:** C++ with C API
**Size:** Medium (~10K lines)

**Features:**

- [x] UTF-8 support
- [x] Syntax highlighting (callback-based)
- [x] Hints and completions
- [x] History with search
- [x] Multi-line editing
- [x] Color support
- [x] Vi and Emacs modes
- [x] Incremental search

**Platforms:**

- Linux [x]
- macOS [x]
- Windows [x]
- BSD [x]

**Integration example:**

```c
#include "replxx.h"

int main() {
    Replxx* rx = replxx_init();
    replxx_history_load(rx, ".loki/repl_history");

    const char* line;
    while ((line = replxx_input(rx, "loki> ")) != NULL) {
        replxx_history_add(rx, line);
        // Execute Lua...
    }

    replxx_history_save(rx, ".loki/repl_history");
    replxx_end(rx);
    return 0;
}
```

**Advantages:**

- Modern design
- Syntax highlighting support
- Cross-platform including Windows
- BSD license
- Active maintenance

**Disadvantages:**

- C++ dependency (even for C API)
- Not single-file embeddable
- Smaller community
- Less battle-tested

**Recommendation:** [+][+][+] **Good for feature-rich REPLs**

---

### 5. isocline

**Author:** Daan Leijen (Microsoft Research)
**License:** MIT
**Repository:** <https://github.com/daanx/isocline>
**Size:** Single header file (~5000 lines)
**Dependencies:** None

**Features:**

- [x] UTF-8 support
- [x] ANSI color and styling
- [x] History with persistence
- [x] Tab completion
- [x] Incremental search
- [x] Multi-line editing
- [x] Works without ANSI terminal support
- [x] Hints (inline suggestions)

**Platforms:**

- Windows [x] (native console, no ANSI needed)
- Linux [x]
- macOS [x]
- BSD [x]

**Integration example:**

```c
#include "isocline.h"

int main() {
    ic_set_history(".loki/repl_history", -1);

    char* line;
    while ((line = ic_readline("loki> ")) != NULL) {
        // Execute Lua...
        free(line);
    }

    return 0;
}
```

**Advantages:**

- Header-only (easy integration)
- MIT license (very permissive)
- Works on Windows without ANSI emulation
- Modern design
- Good documentation

**Disadvantages:**

- Relatively new (less battle-tested)
- Smaller community
- Single-header = longer compile times

**Recommendation:** [+][+][+][+] **Excellent for cross-platform**

---

### 6. tecla

**Author:** Martin Shepherd (Caltech)
**License:** Modified MIT (permissive)
**Repository:** <http://www.astro.caltech.edu/~mcs/tecla/>
**Size:** Medium library

**Features:**

- [x] History with persistence
- [x] Line editing
- [x] Tab completion
- [x] Vi and Emacs modes
- [x] Signal safety

**Platforms:**

- Linux [x]
- Unix [x]
- Some Windows support

**Used by:**

- Astronomy software
- Scientific computing tools

**Advantages:**

- Permissive license
- Signal-safe (useful for scientific computing)
- Stable and mature

**Disadvantages:**

- Less modern
- Smaller community
- Not actively maintained
- Dated website/documentation

**Recommendation:** [+][+] **Dated, better alternatives exist**

---

## Comparison Matrix

| Feature | linenoise | editline | readline | replxx | isocline |
|---------|-----------|----------|----------|--------|----------|
| **License** | BSD-2 | BSD-3 | GPL-3 | BSD-3 | MIT |
| **Size** | Tiny | Medium | Large | Medium | Small |
| **Embeddable** | [x] Yes | [X] No | [X] No | [X] No | [x] Yes |
| **Windows** | [x] Yes | [X] No | [X] No | [x] Yes | [x] Yes |
| **macOS built-in** | [X] | [x] | [X] | [X] | [X] |
| **UTF-8** | [x] | [x] | [x] | [x] | [x] |
| **Syntax colors** | [X] | [X] | [X] | [x] | [x] |
| **Hints** | [x] | [X] | [X] | [x] | [x] |
| **Multi-line** | [x] | [x] | [x] | [x] | [x] |
| **Vi mode** | [X] | [x] | [x] | [x] | [X] |
| **Dependencies** | None | curses | curses | C++ STL | None |
| **Battle-tested** | [x][x][x] | [x][x] | [x][x][x] | [x] | [x] |

## Platform Availability

### macOS

- [x] **editline**: Built-in system library (`/usr/lib/libedit.dylib`)
- [x] **readline**: Homebrew (`brew install readline`, keg-only)
- [x] **linenoise**: Embed source
- [x] **isocline**: Embed source
- [x] **replxx**: Build from source

### Linux (Debian/Ubuntu)

- [x] **editline**: `apt-get install libedit-dev`
- [x] **readline**: `apt-get install libreadline-dev`
- [x] **linenoise**: Embed source
- [x] **isocline**: Embed source
- [x] **replxx**: Build from source

### FreeBSD/OpenBSD

- [x] **editline**: Built-in system library
- [x] **readline**: `pkg install readline`
- [x] **linenoise**: Embed source
- [x] **isocline**: Embed source
- [x] **replxx**: Build from source

### Windows

- [X] **editline**: Not available
- [X] **readline**: MinGW only
- [x] **linenoise**: Native Windows console API
- [x] **isocline**: Native Windows console
- [x] **replxx**: Native support

## Recommendations for Loki

### Current Strategy: [x] Good

```text
1st: editline (BSD, built-in on macOS/BSD)
2nd: readline (GPL, fallback)
3rd: getline (basic, no features)
```

**Pros:**

- Works out-of-box on macOS
- No code to maintain
- Good feature set

**Cons:**

- External dependency
- No Windows support
- GPL in fallback path

### Recommended Addition: linenoise

**Add as 4th tier before getline:**

```text
1st: editline (BSD, built-in on macOS/BSD)
2nd: readline (GPL, Homebrew/Linux)
3rd: linenoise (BSD, embedded)
4th: getline (basic fallback)
```

**Why add linenoise:**

1. **No external dependency** - embed 2 files in `src/`
2. **Windows support** - works on all platforms
3. **Same author as kilo** - philosophical alignment
4. **Tiny size** - adds ~50KB to binary
5. **Better than getline** - full history/editing
6. **BSD license** - no GPL concerns

**Implementation:**

```text
src/
├── linenoise.c        # Add these
├── linenoise.h        # Add these
├── loki_core.c
└── main_repl.c

CMakeLists.txt:
- Check for editline (prefer)
- Check for readline (fallback)
- Use embedded linenoise (always available)
- Remove basic getline path
```

### Alternative: Full linenoise

**Replace editline/readline entirely:**

```text
1st: linenoise (embedded, always available)
```

**Pros:**

- No external dependencies
- Works everywhere (including Windows)
- Smaller binary
- Full control over features
- Consistent behavior across platforms
- Same author as kilo

**Cons:**

- More code to maintain (2 files)
- Lose system integration on macOS
- Less feature-rich than readline

**When to choose:**

- Targeting cross-platform (Windows support needed)
- Want zero dependencies
- Prefer minimal philosophy
- Need consistent behavior everywhere

### Alternative: Header-only isocline

**For maximum portability:**

```text
1st: isocline (header-only, embedded)
```

**Pros:**

- Single header file
- MIT license
- Windows support without ANSI
- Modern design
- No build complexity

**Cons:**

- Longer compile times
- Less battle-tested
- MIT vs BSD (minor)

## License Considerations

### For Distribution

**GPL (readline):**

- Cannot link into proprietary software
- Must provide source code
- [x] OK for open-source projects
- [!] May affect downstream users

**BSD (editline, linenoise, replxx):**

- [x] Can link into proprietary software
- [x] No source disclosure required
- [x] Only need to include license notice
- [x] Most permissive

**MIT (isocline):**

- [x] Similar to BSD, slightly simpler
- [x] Very permissive
- [x] No patent clause (unlike BSD-3)

### For Loki

Current license: **Unknown** (should be specified in LICENSE file)

**Recommendation:**

- If BSD/MIT: Current strategy (editline/readline) is fine
- If GPL-compatible: No concerns
- If proprietary/commercial: Remove readline fallback, use editline + linenoise

## Implementation Roadmap

### Phase 1: Current (Completed)

- [x] editline detection
- [x] readline fallback
- [x] Basic getline fallback
- [x] History persistence
- [x] Cross-platform build

### Phase 2: Add linenoise (Recommended)

1. Add `src/linenoise.c` and `src/linenoise.h` from upstream
2. Update CMakeLists.txt to prefer embedded linenoise over getline
3. Add `LOKI_HAVE_LINENOISE` compilation flag
4. Update `src/main_repl.c` with linenoise support
5. Test on all platforms
6. Update documentation

**Effort:** ~2 hours
**Benefit:** Windows support, better fallback than getline

### Phase 3: Optional syntax highlighting (Future)

- Add callback-based syntax highlighting
- Requires replxx or isocline (or custom editline extension)
- Would need to switch primary library

### Phase 4: Tab completion (Future)

- Implement completion for Lua globals
- Complete `loki.*` API functions
- Complete table keys/methods
- All libraries support this via callbacks

## Testing Strategy

### Test Matrix

| Library | Linux | macOS | FreeBSD | Windows |
|---------|-------|-------|---------|---------|
| editline | [x] | [x] | [x] | N/A |
| readline | [x] | [x] | [x] | N/A |
| linenoise | [x] | [x] | [x] | [x] |
| isocline | [x] | [x] | [x] | [x] |

### Test Cases

1. Command history (up/down arrows)
2. Line editing (left/right, home/end)
3. History persistence across sessions
4. UTF-8 input (emoji, Unicode)
5. Multi-line Lua (functions, tables)
6. Ctrl-C interrupt handling
7. Large history files
8. Tab completion (when implemented)

## Conclusion

**Current implementation (editline/readline):** Good for Unix systems

**Recommended enhancement:** Add linenoise as tier-3 fallback

- Provides Windows support
- Better than basic getline
- Only ~2 files to add
- Same author as kilo (philosophical fit)

**Alternative paths:**

1. **Minimal:** Keep current (editline/readline/getline)
2. **Portable:** Add linenoise tier
3. **Full embedded:** Switch entirely to linenoise
4. **Modern:** Switch to isocline (header-only, cross-platform)

**Decision factors:**

- Windows support needed? → Add linenoise or isocline
- Zero dependencies wanted? → Switch to linenoise or isocline
- System integration valued? → Keep editline/readline
- Syntax highlighting wanted? → Consider replxx or isocline

## References

- linenoise: <https://github.com/antirez/linenoise>
- editline: <https://github.com/NetBSD/libedit>
- readline: <https://tiswww.case.edu/php/chet/readline/rltop.html>
- replxx: <https://github.com/AmokHuginnsson/replxx>
- isocline: <https://github.com/daanx/isocline>
- tecla: <http://www.astro.caltech.edu/~mcs/tecla/>

---

**Document Status:** Complete
**Last Updated:** 2025-10-11
**Maintainer:** Loki project
