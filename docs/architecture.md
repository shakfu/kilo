# Loki Architecture

## Overview

- Split the codebase into a reusable library (`libloki`), a Lua-enabled terminal editor (`loki-editor`), and a Lua-first REPL (`loki-repl`).
- Keep the tiny C core as the foundation while Lua layers remain first-class extensions.
- Host buffer management, async HTTP, and Lua bindings inside the library to prevent drift between executables.

## Library: `libloki`

- Extract current `loki.c` subsystems into headers under `include/loki/` and sources in `src/`.
- Publish a narrow API: editor state, async HTTP hooks, Lua bridge helpers, platform I/O adaptors.
- Offer struct-based configuration (e.g., `loki_editor_config`, `loki_repl_config`) so new consumers can opt in to optional features.
- Ship both static (`libloki.a`) and shared (`libloki.dylib`/`.so`) outputs; keep Lua registration functions internal.

## Executable: `loki-editor` (Editor)

- Link against `libloki`, providing only terminal UI, command handling, and platform glue.
- Preserve current startup sequence: project `.loki/` loads before `~/.loki/`; AI flows (`--complete`, `--explain`) call back into library helpers.
- Maintain CLI compatibility; record deltas in `README.md` and `CHANGELOG.md`.

## Executable: `loki-repl`

- Build a readline-backed REPL that preloads Loki’s Lua API, async helpers, and sample scripts.
- Support interactive shells and batch mode (`loki-repl <file.lua>`).
- Expose debug flags (e.g., `--trace-http`) that proxy to shared hooks, reusing the same config loader as the editor.

## Lua Integration

- Centralize Lua embedding inside `libloki`; both executables call `loki_lua_bootstrap(const loki_lua_opts *opts)` to obtain a configured `lua_State`.
- Support both Lua and LuaJIT at build time, determined by CMake feature detection; surface `loki_lua_runtime()` so clients can report which runtime is active.
- Expose registration helpers (`loki_lua_bind_editor`, `loki_lua_bind_http`) that load C modules into the state; keep internal tables static to match current export discipline.
- Extend the module search path to include project `.loki/` then user `~/.loki/`; allow optional `LOKI_LUA_PATH` for advanced overrides.
- Route async callbacks through the library so Lua coroutines work identically in editor and REPL contexts; share error reporting (`loki_lua_traceback`) to produce consistent diagnostics.
- Provide a minimal C API for embedding hosts to inject custom Lua modules while respecting sandboxing expectations.

## Build & Layout

- Adopt CMake as the primary build backend with a minimal Makefile frontend.
- Create entry points `src/main_editor.c` and `src/main_repl.c`; keep shared logic in `src/loki_core.c`.
- Define CMake targets:
  - `libloki` via `add_library` (default STATIC with option for SHARED).
  - `loki_editor` via `add_executable` linking `libloki` (installs as `loki-editor`).
  - `loki_repl` via `add_executable` linking `libloki`.
- Makefile convenience targets:
  - `make lib` → build `libloki`.
  - `make editor` → build editor linking `libloki` (alias: `make loki`).
  - `make repl` → build REPL linking `libloki`.
  - `make all` → build library and both executables.

- Keep `make show-config` reporting include/library paths; expand `make test` to invoke both binaries.

## Testing & QA

- Extend smoke tests: `./loki-editor --version`, `./loki-repl --version`, Lua fixtures hitting shared APIs.
- Add deterministic `.loki/tests/` scripts to guard editor and REPL behavior.
- Document manual checks for terminal rendering, AI callbacks, and REPL scripting before merges.

## Migration Plan

- First, isolate reusable logic into `src/loki_core.c` and new headers while keeping the current `loki` binary intact.
- Transition the editor to consume `libloki`; once stable, add `loki-repl` with shared configuration and logging.
- Only merge once docs (`README.md`, `CHANGELOG.md`, `docs/SYNTAX_LSP_RESEARCH.md`) reflect the new targets and API surface.

## Possible Future Considerations

- Ideally, the editor and the repl could be two parts of one application and the user would have a window split and access the features from both in one unified interface.

- Prototype a `loki-shell` binary that embeds both UI loops and switches between modes with a hotkey, leveraging the same library surfaces.

- Explore tmux- or wezterm-based orchestrations that launch `loki-editor` and `loki-repl` together until the unified UI lands.

## Review Checklist (to be completed by architecture reviewers)

- [x] Library API surface signed off (editor state, async bridge, Lua hooks)
- [x] CLI flag compatibility reviewed for `loki-editor` and `loki-repl`
- [x] Build system migration (CMake + Makefile) approved
- [x] Lua integration plan validated across Lua and LuaJIT
- [x] Testing strategy covers editor, REPL, and shared Lua fixtures
