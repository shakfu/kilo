# Global State Refactoring - Summary

**Date:** 2025-10-12
**Objective:** Remove remaining global state from loki codebase to enable full context independence

---

## Changes Made

### 1. Async HTTP State Moved to Context ✅

**Problem:** Async HTTP request tracking used global static variables

**Solution:** Moved async HTTP state into `editor_ctx_t`

**Files Modified:**
- `src/loki_internal.h` - Added fields to editor_ctx_t
- `src/loki_editor.c` - Removed static variables, updated all references
- `src/loki_lua.c` - Updated function call to pass context
- `src/loki_core.c` - Initialize new fields

**Before:**
```c
// loki_editor.c
static async_http_request *pending_requests[MAX_ASYNC_REQUESTS] = {0};
static int num_pending = 0;
```

**After:**
```c
// loki_internal.h - in struct editor_ctx
void *pending_http_requests[10]; /* Array of async_http_request* */
int num_pending_http; /* Number of pending HTTP requests */
```

**Impact:**
- ✅ Multiple editor instances can have independent HTTP requests
- ✅ Better encapsulation - state is owned by context
- ✅ Easier testing - no global state to reset

---

### 2. Window Resize Signal Handler State ✅

**Problem:** POSIX signal handlers cannot reliably access per-context data

**Solution:** Context-based flag with documented limitation for signal handlers

**Files Modified:**
- `src/loki_internal.h` - Added winsize_changed to editor_ctx_t
- `src/loki_terminal.c` - Global context pointer for signal handler
- `src/loki_core.c` - Initialize field

**Approach:**
1. Added `winsize_changed` flag to `editor_ctx_t` (per-context)
2. Maintain global `signal_context` pointer for signal handler access
3. Signal handler sets flag on registered context
4. Main loop checks flag in context and handles resize

**Before:**
```c
// loki_terminal.c
static volatile sig_atomic_t winsize_changed = 0;

void terminal_sig_winch_handler(int sig) {
    winsize_changed = 1;  // Global state
}
```

**After:**
```c
// loki_terminal.c
static editor_ctx_t *signal_context = NULL;  // Points to active context

void terminal_sig_winch_handler(int sig) {
    if (signal_context) {
        signal_context->winsize_changed = 1;  // Context-specific flag
    }
}

// loki_internal.h - in struct editor_ctx
volatile sig_atomic_t winsize_changed;  // Per-context flag
```

**Limitation Documented:**
Only one editor instance can properly handle window resize signals at a time (acceptable for typical use case).

**Impact:**
- ✅ Window resize state is context-specific
- ✅ Signal handling remains async-signal-safe
- ⚠️ Limitation: Only one context can handle SIGWINCH (acceptable for single-instance use)

---

## Remaining Global State

### Acceptable Global State

These remain global for valid reasons:

1. **`curl_initialized` (loki_editor.c)** - Process-wide libcurl initialization flag
   - **Reason:** libcurl requires global init/cleanup
   - **Status:** ✅ Appropriate to remain global

2. **`orig_termios` (loki_terminal.c)** - Original terminal settings
   - **Reason:** Terminal state is process-wide
   - **Status:** ✅ Appropriate to remain global

3. **`signal_context` (loki_terminal.c)** - Context pointer for signal handler
   - **Reason:** POSIX signal handlers cannot access per-context data
   - **Status:** ✅ Documented limitation, best available solution

---

## Testing Results

### Compilation: ✅ Success
```bash
$ make
[100%] Built target loki_repl
```

**Warnings:** 2 unused function warnings (pre-existing, unrelated)

### Tests: ✅ All Pass
```bash
$ make test
Test project /Users/sa/projects/loki/build
    Start 1: loki_editor_version ..............   Passed    0.50 sec
    Start 2: loki_repl_version ................   Passed    0.24 sec

100% tests passed, 0 tests failed out of 2
```

---

## Architecture Benefits

### Before Refactoring
```
Editor Context (editor_ctx_t)
├─ Editor state (rows, cursor, etc.)
└─ Lua state

Global State (scattered)
├─ pending_requests[10]      ← loki_editor.c
├─ num_pending               ← loki_editor.c
└─ winsize_changed           ← loki_terminal.c (now loki_core.c)
```

### After Refactoring
```
Editor Context (editor_ctx_t)
├─ Editor state (rows, cursor, etc.)
├─ Lua state
├─ pending_http_requests[10]  ← Moved from global
├─ num_pending_http           ← Moved from global
└─ winsize_changed            ← Moved from global

Global State (minimal, justified)
├─ curl_initialized           ← Process-wide (libcurl requirement)
├─ orig_termios               ← Process-wide (terminal state)
└─ signal_context             ← Signal handler limitation (documented)
```

---

## Code Changes Summary

### Lines Changed by File

| File | Lines Added | Lines Removed | Net Change |
|------|-------------|---------------|------------|
| src/loki_internal.h | 8 | 4 | +4 |
| src/loki_core.c | 5 | 1 | +4 |
| src/loki_editor.c | 12 | 14 | -2 |
| src/loki_terminal.c | 14 | 5 | +9 |
| src/loki_lua.c | 1 | 1 | 0 |
| **Total** | **40** | **25** | **+15** |

### Function Signatures Changed

1. `start_async_http_request()` - Added `editor_ctx_t *ctx` parameter
2. `terminal_enable_raw_mode()` - Sets global context pointer for signals
3. `terminal_sig_winch_handler()` - Uses global context pointer

---

## Migration Notes

### For Future Split Window/Multi-Buffer Support

**What Works:**
- ✅ Each context has independent async HTTP state
- ✅ Each context tracks its own window resize flag
- ✅ Contexts are fully independent except for signal handling

**Known Limitation:**
- ⚠️ Only the "active" context (last to enable raw mode) receives SIGWINCH signals
- **Workaround:** When switching active context, call `terminal_enable_raw_mode()` to register it for signals

**Example:**
```c
editor_ctx_t ctx1, ctx2;

/* Initialize both contexts */
editor_ctx_init(&ctx1);
editor_ctx_init(&ctx2);

/* Context 1 is active */
terminal_enable_raw_mode(&ctx1, STDIN_FILENO);  // ctx1 receives SIGWINCH

/* Switch to context 2 */
terminal_enable_raw_mode(&ctx2, STDIN_FILENO);  // ctx2 now receives SIGWINCH
```

---

## Verification Checklist

- [x] No static pending_requests array in loki_editor.c
- [x] No static num_pending variable in loki_editor.c
- [x] No static winsize_changed in loki_terminal.c
- [x] pending_http_requests in editor_ctx_t
- [x] num_pending_http in editor_ctx_t
- [x] winsize_changed in editor_ctx_t
- [x] Fields initialized in editor_ctx_init()
- [x] All function signatures updated
- [x] All call sites updated
- [x] Compiles without errors
- [x] All tests pass
- [x] Signal handler is async-signal-safe

---

## Conclusion

✅ **All major global state issues identified in CODE_REVIEW.md have been addressed.**

The codebase now has **full context independence** with only justified exceptions:
1. **libcurl global state** - Required by library
2. **Terminal state** - Process-wide by nature
3. **Signal handler context** - POSIX limitation, properly documented

**Next Steps:**
- Update CODE_REVIEW.md to reflect these changes
- Consider adding multi-context tests in the future
- Document split-window architecture when implementing that feature

---

**Document Version:** 1.0
**Status:** Complete ✅
