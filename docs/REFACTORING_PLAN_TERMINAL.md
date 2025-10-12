# Refactoring Plan: Extract Terminal I/O Module

## Goal
Split terminal-specific code from `loki_core.c` into a dedicated `loki_terminal.c` module to improve modularity and reduce file size.

---

## Current State

**loki_core.c:** 1,337 LOC (31.3% of codebase)
- Contains terminal I/O mixed with core editing logic
- Makes testing harder (requires terminal for tests)
- Violates single responsibility principle

---

## New Module Structure

### Files to Create

1. **src/loki_terminal.h** - Terminal I/O interface (public within loki)
2. **src/loki_terminal.c** - Terminal I/O implementation

### Functions to Move

#### From "Low level terminal handling" section (lines ~145-337)

**Terminal Mode Management:**
```c
int enable_raw_mode(editor_ctx_t *ctx, int fd);
void disable_raw_mode(editor_ctx_t *ctx, int fd);
```

**Input Reading:**
```c
int editor_read_key(int fd);  // → terminal_read_key(int fd)
```

**Terminal Queries:**
```c
int get_cursor_position(int ifd, int ofd, int *rows, int *cols);
int get_window_size(int ifd, int ofd, int *rows, int *cols);
```

#### From "Terminal update" section (lines ~934-1153)

**Screen Buffer Management:**
```c
void ab_append(struct abuf *ab, const char *s, int len);
void ab_free(struct abuf *ab);
```

#### From window resize handling (lines ~1182-1213)

**Window Management:**
```c
void update_window_size(editor_ctx_t *ctx);
void handle_sig_win_ch(int sig);  // Signal handler
void handle_windows_resize(editor_ctx_t *ctx);  // Check and handle
```

**Total Functions Moving:** 9 functions (~200-250 LOC)

---

## Detailed Implementation

### Step 1: Create Header File

**File:** `src/loki_terminal.h`

```c
/* loki_terminal.h - Terminal I/O abstraction layer
 *
 * This module provides low-level terminal operations including:
 * - Raw mode management (disabling canonical mode, echo, etc.)
 * - Key reading with escape sequence parsing
 * - Window size detection and monitoring
 * - Screen buffer management for efficient rendering
 *
 * These functions are platform-specific (POSIX) and isolate terminal
 * dependencies from the core editor logic.
 */

#ifndef LOKI_TERMINAL_H
#define LOKI_TERMINAL_H

#include "loki_internal.h"  /* For editor_ctx_t, abuf */

/* ======================= Terminal Mode Management ========================= */

/* Enable raw mode on the given file descriptor.
 * Raw mode disables canonical input, echo, and signal generation.
 * Returns 0 on success, -1 on failure (sets errno to ENOTTY). */
int terminal_enable_raw_mode(editor_ctx_t *ctx, int fd);

/* Disable raw mode, restoring terminal to original state.
 * Should be called before exit to avoid leaving terminal in bad state. */
void terminal_disable_raw_mode(editor_ctx_t *ctx, int fd);

/* ======================= Input Reading ==================================== */

/* Read a single key from the terminal, handling escape sequences.
 * Blocks until a key is available or timeout occurs.
 * Returns:
 *   - ASCII value for normal keys (0-127)
 *   - KEY_* constants for special keys (arrows, function keys, etc.)
 *   - Exits on EOF after timeout */
int terminal_read_key(int fd);

/* ======================= Window Size Detection ============================ */

/* Get current terminal window size in rows and columns.
 * First tries ioctl(TIOCGWINSZ), falls back to VT100 cursor queries.
 * Returns 0 on success, -1 on failure. */
int terminal_get_window_size(int ifd, int ofd, int *rows, int *cols);

/* Query cursor position using VT100 escape sequences.
 * Used as fallback when ioctl fails.
 * Returns 0 on success, -1 on failure. */
int terminal_get_cursor_position(int ifd, int ofd, int *rows, int *cols);

/* Update editor context with current window size.
 * Adjusts screenrows/screencols and handles REPL layout.
 * Should be called on initialization and after SIGWINCH. */
void terminal_update_window_size(editor_ctx_t *ctx);

/* Check if window size changed and update if needed.
 * Reads the winsize_changed flag set by signal handler.
 * Safe to call in main loop (signal handler only sets flag). */
void terminal_handle_resize(editor_ctx_t *ctx);

/* ======================= Screen Buffer ==================================== */

/* Append string to screen buffer for efficient rendering.
 * Buffers all VT100 escape sequences and content, then flushes
 * in a single write() call to minimize flicker.
 * Exits on allocation failure after attempting cleanup. */
void terminal_buffer_append(struct abuf *ab, const char *s, int len);

/* Free screen buffer memory. */
void terminal_buffer_free(struct abuf *ab);

/* ======================= Signal Handling ================================== */

/* Signal handler for SIGWINCH (window size change).
 * Async-signal-safe: only sets a flag, actual handling in terminal_handle_resize().
 * Should be registered with signal(SIGWINCH, terminal_sig_winch_handler). */
void terminal_sig_winch_handler(int sig);

#endif /* LOKI_TERMINAL_H */
```

---

### Step 2: Create Implementation File

**File:** `src/loki_terminal.c`

```c
/* loki_terminal.c - Terminal I/O implementation
 *
 * Platform-specific terminal operations for POSIX systems.
 * Uses termios for raw mode, ioctl for window size, and VT100
 * escape sequences for advanced features.
 */

#include "loki_terminal.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ======================= Static State ===================================== */

/* Original terminal state (saved before entering raw mode) */
static struct termios orig_termios;

/* Flag set by signal handler when window size changes */
static volatile sig_atomic_t winsize_changed = 0;

/* ======================= Terminal Mode Management ========================= */

void terminal_disable_raw_mode(editor_ctx_t *ctx, int fd) {
    if (ctx && ctx->rawmode) {
        tcsetattr(fd, TCSAFLUSH, &orig_termios);
        ctx->rawmode = 0;
    }
}

int terminal_enable_raw_mode(editor_ctx_t *ctx, int fd) {
    struct termios raw;

    if (!ctx) return -1;
    if (ctx->rawmode) return 0;  /* Already enabled */
    if (!isatty(fd)) goto fatal;
    if (tcgetattr(fd, &orig_termios) == -1) goto fatal;

    raw = orig_termios;

    /* Input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /* Output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);

    /* Control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);

    /* Local modes - echo off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /* Control chars - set return condition: min number of bytes and timer */
    raw.c_cc[VMIN] = 0;   /* Return each byte, or zero for timeout */
    raw.c_cc[VTIME] = 1;  /* 100 ms timeout (unit is tens of second) */

    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) goto fatal;
    ctx->rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

/* ======================= Input Reading ==================================== */

int terminal_read_key(int fd) {
    int nread;
    char c, seq[6];
    int retries = 0;

    /* Wait for input with timeout */
    while ((nread = read(fd, &c, 1)) == 0) {
        if (++retries > 1000) {
            /* After ~100 seconds of no input, assume stdin is closed */
            fprintf(stderr, "\nNo input received, exiting.\n");
            exit(0);
        }
    }
    if (nread == -1) exit(1);

    while(1) {
        switch(c) {
        case ESC:    /* Escape sequence */
            /* If this is just an ESC, we'll timeout here */
            if (read(fd, seq, 1) == 0) return ESC;
            if (read(fd, seq+1, 1) == 0) return ESC;

            /* ESC [ sequences */
            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    /* Extended escape, read additional byte */
                    if (read(fd, seq+2, 1) == 0) return ESC;
                    if (seq[2] == '~') {
                        switch(seq[1]) {
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        }
                    } else if (seq[2] == ';') {
                        /* ESC[1;2X for Shift+Arrow */
                        if (read(fd, seq+3, 1) == 0) return ESC;
                        if (read(fd, seq+4, 1) == 0) return ESC;
                        if (seq[1] == '1' && seq[3] == '2') {
                            switch(seq[4]) {
                            case 'A': return SHIFT_ARROW_UP;
                            case 'B': return SHIFT_ARROW_DOWN;
                            case 'C': return SHIFT_ARROW_RIGHT;
                            case 'D': return SHIFT_ARROW_LEFT;
                            }
                        }
                    }
                } else {
                    switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                    }
                }
            }
            /* ESC O sequences */
            else if (seq[0] == 'O') {
                switch(seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
            break;
        default:
            return c;
        }
    }
}

/* ======================= Window Size Detection ============================ */

int terminal_get_cursor_position(int ifd, int ofd, int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    /* Report cursor location */
    if (write(ofd, "\x1b[6n", 4) != 4) return -1;

    /* Read the response: ESC [ rows ; cols R */
    while (i < sizeof(buf)-1) {
        if (read(ifd, buf+i, 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    /* Parse it */
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf+2, "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

int terminal_get_window_size(int ifd, int ofd, int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* ioctl() failed. Try to query the terminal itself. */
        int orig_row, orig_col, retval;

        /* Get the initial position so we can restore it later */
        retval = terminal_get_cursor_position(ifd, ofd, &orig_row, &orig_col);
        if (retval == -1) goto failed;

        /* Go to right/bottom margin and get position */
        if (write(ofd, "\x1b[999C\x1b[999B", 12) != 12) goto failed;
        retval = terminal_get_cursor_position(ifd, ofd, rows, cols);
        if (retval == -1) goto failed;

        /* Restore position */
        char seq[32];
        snprintf(seq, 32, "\x1b[%d;%dH", orig_row, orig_col);
        if (write(ofd, seq, strlen(seq)) == -1) {
            /* Can't recover... */
        }
        return 0;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }

failed:
    return -1;
}

void terminal_update_window_size(editor_ctx_t *ctx) {
    int rows, cols;
    if (terminal_get_window_size(STDIN_FILENO, STDOUT_FILENO,
                                  &rows, &cols) == -1) {
        /* If we can't get terminal size, use defaults */
        rows = 24;
        cols = 80;
    }
    ctx->screencols = cols;
    rows -= STATUS_ROWS;
    if (rows < 1) rows = 1;
    ctx->screenrows_total = rows;
    ctx->screenrows = ctx->screenrows_total;
}

/* ======================= Signal Handling ================================== */

void terminal_sig_winch_handler(int sig __attribute__((unused))) {
    /* Signal handler must be async-signal-safe.
     * Just set a flag and handle resize in main loop. */
    winsize_changed = 1;
}

void terminal_handle_resize(editor_ctx_t *ctx) {
    if (winsize_changed) {
        winsize_changed = 0;
        terminal_update_window_size(ctx);
        if (ctx->cy > ctx->screenrows) ctx->cy = ctx->screenrows - 1;
        if (ctx->cx > ctx->screencols) ctx->cx = ctx->screencols - 1;
    }
}

/* ======================= Screen Buffer ==================================== */

void terminal_buffer_append(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) {
        /* Out of memory - attempt to restore terminal and exit cleanly */
        write(STDOUT_FILENO, "\x1b[2J", 4);  /* Clear screen */
        write(STDOUT_FILENO, "\x1b[H", 3);   /* Go home */
        perror("Out of memory during screen refresh");
        exit(1);
    }
    memcpy(new + ab->len, s, len);
    ab->b = new;
    ab->len += len;
}

void terminal_buffer_free(struct abuf *ab) {
    free(ab->b);
}
```

---

### Step 3: Update `loki_core.c`

**Changes needed:**

1. **Add include:**
```c
#include "loki_terminal.h"
```

2. **Remove functions** (moved to loki_terminal.c):
   - `enable_raw_mode()`
   - `disable_raw_mode()`
   - `editor_read_key()`
   - `get_cursor_position()`
   - `get_window_size()`
   - `update_window_size()`
   - `handle_sig_win_ch()`
   - `handle_windows_resize()`
   - `ab_append()`
   - `ab_free()`

3. **Update function calls** throughout file:
```c
// Old:
enable_raw_mode(&E, STDIN_FILENO);
int c = editor_read_key(fd);
ab_append(&ab, "\x1b[H", 3);
handle_windows_resize(&E);

// New:
terminal_enable_raw_mode(&E, STDIN_FILENO);
int c = terminal_read_key(fd);
terminal_buffer_append(&ab, "\x1b[H", 3);
terminal_handle_resize(&E);
```

4. **Update signal registration** in `init_editor()`:
```c
signal(SIGWINCH, terminal_sig_winch_handler);
```

---

### Step 4: Update `loki_internal.h`

**Remove from loki_internal.h:**
```c
int enable_raw_mode(editor_ctx_t *ctx, int fd);
void disable_raw_mode(editor_ctx_t *ctx, int fd);
int editor_read_key(int fd);
void handle_windows_resize(editor_ctx_t *ctx);
void ab_append(struct abuf *ab, const char *s, int len);
void ab_free(struct abuf *ab);
```

These are now in `loki_terminal.h`.

---

### Step 5: Update Build System

**CMakeLists.txt:**
```cmake
set(LOKI_SOURCES
    src/loki_core.c
    src/loki_editor.c
    src/loki_lua.c
    src/loki_modal.c
    src/loki_search.c
    src/loki_selection.c
    src/loki_languages.c
    src/loki_terminal.c      # ADD THIS
    src/main_editor.c
)
```

**Makefile (if you have one):**
```makefile
SOURCES = src/loki_core.c \
          src/loki_editor.c \
          src/loki_lua.c \
          src/loki_modal.c \
          src/loki_search.c \
          src/loki_selection.c \
          src/loki_languages.c \
          src/loki_terminal.c    # ADD THIS
```

---

## Benefits After Refactoring

### 1. **Smaller, More Focused Files**
- `loki_core.c`: 1,337 → ~1,100 LOC (17% reduction)
- `loki_terminal.c`: ~240 LOC (new)

### 2. **Better Testability**
```c
// Can now test terminal functions in isolation
void test_escape_sequence_parsing(void) {
    // Mock terminal input
    // Test terminal_read_key() with various escape sequences
}
```

### 3. **Platform Abstraction Layer**
Makes it easier to add Windows support later:
```c
#ifdef _WIN32
    #include "loki_terminal_win32.h"
#else
    #include "loki_terminal_posix.h"
#endif
```

### 4. **Clearer Dependencies**
- Core editor logic doesn't directly call termios functions
- Terminal code is isolated to one module

### 5. **Easier Maintenance**
- Terminal-related bugs are easier to locate
- Changes to VT100 handling don't affect core logic

---

## Testing Strategy

### Unit Tests to Add

**test_terminal.c:**
```c
// Test raw mode enable/disable
void test_raw_mode_toggle(void);

// Test escape sequence parsing
void test_arrow_key_parsing(void);
void test_page_up_down_parsing(void);
void test_shift_arrow_parsing(void);

// Test screen buffer
void test_buffer_append_and_free(void);
void test_buffer_overflow_protection(void);

// Test window size detection
void test_window_size_fallback(void);
```

---

## Migration Checklist

- [ ] Create `src/loki_terminal.h` with interface
- [ ] Create `src/loki_terminal.c` with implementation
- [ ] Move functions from `loki_core.c` to `loki_terminal.c`
- [ ] Update `loki_core.c` to use new API
- [ ] Update `loki_editor.c` to include new header
- [ ] Update `loki_modal.c` to use `terminal_read_key()`
- [ ] Update `loki_internal.h` to remove moved declarations
- [ ] Update CMakeLists.txt to include new file
- [ ] Update Makefile (if exists)
- [ ] Compile and test
- [ ] Run existing tests (should still pass)
- [ ] Add new unit tests for terminal module
- [ ] Update CLAUDE.md with new architecture
- [ ] Update CODE_REVIEW.md metrics

---

## Estimated Effort

- **Time:** 2-3 hours
- **Risk:** Low (mostly mechanical refactoring)
- **Files Modified:** 5-6 files
- **Lines Added:** ~300 (new module + headers)
- **Lines Removed:** ~250 (moved from loki_core.c)
- **Net Change:** ~50 LOC increase (due to new header comments/docs)

---

## Alternative: Further Splitting

If we want to be even more modular, we could split further:

```
src/loki_terminal.c     → Terminal I/O core
src/loki_terminal_vt100.c → VT100 escape sequence handling
src/loki_screen.c       → Screen rendering/refresh
```

But this might be **over-engineering** for a ~4K LOC project.

---

## Conclusion

This refactoring improves modularity without changing external behavior.
It's a **safe, mechanical transformation** that makes the codebase easier
to test and maintain.

**Recommendation:** Proceed with basic split (single loki_terminal.c module).
Further splitting can be done later if complexity grows.
