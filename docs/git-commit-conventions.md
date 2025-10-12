# Git Commit Conventions

This guide defines the commit message format for the Loki project. Following these conventions improves project history readability, enables automatic changelog generation, and makes code archaeology easier.

---

## Format

```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

### Components

**Type** (required): The kind of change
**Scope** (optional): The module or component affected
**Description** (required): Short summary of the change (imperative mood, lowercase, no period)

---

## Types

| Type | Usage | Example |
|------|-------|---------|
| `feat` | New feature | `feat(modal): add word forward/backward motion (w/b)` |
| `fix` | Bug fix | `fix(undo): prevent crash when undoing empty buffer` |
| `docs` | Documentation only | `docs(security): add HTTP rate limiting explanation` |
| `style` | Code style (formatting, whitespace) | `style(core): fix indentation in editor_refresh_screen` |
| `refactor` | Code restructuring (no behavior change) | `refactor(search): extract validation into helper function` |
| `perf` | Performance improvement | `perf(syntax): cache compiled regex patterns` |
| `test` | Add or modify tests | `test(modal): add visual mode delete selection tests` |
| `build` | Build system or dependencies | `build(cmake): add fuzzing support with AFL` |
| `ci` | CI/CD changes | `ci(github): add Valgrind memory leak detection` |
| `chore` | Maintenance tasks | `chore(deps): update libcurl to 8.5.0` |
| `revert` | Revert a previous commit | `revert: feat(modal): add word motions` |

---

## Scopes

Common scopes in Loki:

| Scope | Module/Component |
|-------|------------------|
| `core` | loki_core.c - Core editor functionality |
| `modal` | loki_modal.c - Modal editing (vim modes) |
| `undo` | loki_undo.c - Undo/redo system |
| `search` | loki_search.c - Search functionality |
| `selection` | loki_selection.c - Visual selection, clipboard |
| `syntax` | loki_languages.c - Syntax highlighting |
| `lua` | loki_lua.c - Lua C API bindings |
| `editor` | loki_editor.c - Main editor loop |
| `terminal` | loki_terminal.c - Terminal I/O |
| `command` | loki_command.c - Command mode |
| `http` | Async HTTP functionality |
| `tests` | Test suite |
| `docs` | Documentation |
| `build` | Build system (CMake, Makefile) |
| `ci` | Continuous integration |

Use `*` for changes affecting multiple scopes: `refactor(*): rename editor_ctx to editor_context`

Omit scope for project-wide changes: `chore: update copyright year`

---

## Description Guidelines

**DO:**
- Use imperative mood ("add feature" not "added feature")
- Start with lowercase
- Keep under 72 characters
- Be specific about what changed
- Omit trailing period

**Examples:**
- ✅ `feat(modal): add visual block mode (Ctrl-V)`
- ✅ `fix(search): handle regex errors gracefully`
- ✅ `perf(render): reduce screen refresh time by 40%`

**DON'T:**
- ❌ `feat(modal): Added visual block mode.` (wrong tense, period)
- ❌ `fix: bug fix` (too vague)
- ❌ `snap` (no context)
- ❌ `wip` (work in progress - don't commit)
- ❌ `feat(modal): implemented visual block mode with ctrl-v and added tests and updated docs` (too long)

---

## Examples

### Simple Changes

```bash
# Add a new feature
git commit -m "feat(clipboard): add paste command (p/P in normal mode)"

# Fix a bug
git commit -m "fix(undo): prevent buffer overflow in operation history"

# Update documentation
git commit -m "docs(readme): add security warning about init.lua"

# Improve performance
git commit -m "perf(syntax): cache regex compilation for 2x speedup"
```

### Changes with Body

```bash
git commit -m "feat(buffers): implement multiple buffer support" -m "
- Add buffer manager with Ctrl-T/Ctrl-W/Ctrl-Tab keybindings
- Store up to 10 open buffers simultaneously
- Display buffer list in status bar
- Each buffer maintains independent cursor and viewport

Closes #42"
```

### Breaking Changes

Use `BREAKING CHANGE:` footer for incompatible API changes:

```bash
git commit -m "feat(lua)!: change async_http callback signature" -m "
BREAKING CHANGE: async_http callbacks now receive table instead of positional args.

Before:
  function callback(status, body, error)

After:
  function callback(response)
    -- response.status, response.body, response.error
"
```

Note the `!` after type/scope indicates breaking change.

---

## Multi-Change Commits

If a commit involves multiple changes, use the most significant type:

```bash
# Primary change is a feature, but also includes tests and docs
git commit -m "feat(indent): add auto-indent module" -m "
Automatically copy indentation from previous line on Enter.
Supports electric dedent for closing braces.

- Implement indent detection (tabs vs spaces)
- Add configuration for indent behavior
- Include comprehensive test suite
- Document API in loki/core.h
"
```

---

## When NOT to Use Conventional Commits

**Merge commits**: Use default merge message
```bash
git merge feature-branch
# Message: "Merge branch 'feature-branch'"
```

**Revert commits**: Use `git revert` default
```bash
git revert abc123
# Message: "Revert 'feat(modal): add word motions'"
```

**Fixup commits** (interactive rebase): Use `fixup!` prefix
```bash
git commit --fixup=abc123
# Message: "fixup! feat(modal): add word motions"
```

---

## Bad Examples (Don't Do This)

These are actual commit messages from Loki history that we want to avoid:

```bash
❌ "snap"                    # No context whatsoever
❌ "more tests"              # What tests? For what?
❌ "added undo/redo"         # Which part? What specifically changed?
❌ "dropped thirdparty"      # Why? What was removed?
❌ "wip"                     # Don't commit work in progress
❌ "fix stuff"               # What stuff?
❌ "update"                  # Update what?
❌ "asdf"                    # Not even trying
```

**Why these are bad:**
- No searchable keywords (`git log --grep="undo"` won't find "snap")
- Can't generate changelog
- Makes bisecting difficult
- No context for code archaeology
- Unprofessional appearance

---

## Good Examples

Here's what the recent Loki commits should have looked like:

```bash
# Instead of: "added undo/redo" (repeated 3 times)
feat(undo): implement operation grouping with time-based heuristics
feat(undo): add circular buffer with memory limits
feat(undo): integrate undo/redo with modal editing (u/Ctrl-R)

# Instead of: "more tests"
test(undo): add operation grouping edge case tests
test(modal): verify visual mode state transitions

# Instead of: "dropped thirdparty"
refactor(build): remove vendored dependencies, use system libraries

# Instead of: "snap"
fix(modal): prevent null pointer dereference in visual mode
refactor(core): extract syntax highlighting to separate module
test(search): add incremental search boundary tests
```

---

## Tools and Automation

### Commitlint (Optional)

Install commitlint to validate commit messages:

```bash
npm install --save-dev @commitlint/cli @commitlint/config-conventional
echo "module.exports = {extends: ['@conventional-commits']}" > commitlint.config.js
```

Add to git hook:
```bash
# .git/hooks/commit-msg
#!/bin/sh
npx commitlint --edit $1
```

### Commit Message Template

Create a template:

```bash
cat > ~/.gitmessage << 'EOF'
# <type>(<scope>): <description>
#
# <body>
#
# <footer>
#
# Types: feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert
# Scopes: core, modal, undo, search, selection, syntax, lua, editor, terminal, command, http, tests, docs, build, ci
#
# Examples:
#   feat(modal): add word forward/backward motion (w/b)
#   fix(undo): prevent crash when undoing empty buffer
#   docs(security): add HTTP rate limiting explanation
#
# Remember:
#   - Use imperative mood ("add" not "added")
#   - Lowercase description, no period
#   - Keep subject line under 72 chars
#   - Separate subject from body with blank line
EOF

git config --global commit.template ~/.gitmessage
```

Now `git commit` opens editor with template.

---

## Changelog Generation

Conventional commits enable automatic changelog generation:

```bash
# Using conventional-changelog
npm install -g conventional-changelog-cli
conventional-changelog -p angular -i CHANGELOG.md -s
```

Example output:
```markdown
## [0.5.0] - 2025-10-15

### Features

- **modal**: add word forward/backward motion (w/b) (a1b2c3d)
- **clipboard**: add paste command (p/P in normal mode) (d4e5f6)
- **buffers**: implement multiple buffer support (g7h8i9)

### Bug Fixes

- **undo**: prevent crash when undoing empty buffer (j0k1l2)
- **search**: handle regex errors gracefully (m3n4o5)

### Performance

- **syntax**: cache regex compilation for 2x speedup (p6q7r8)
```

---

## Summary

**Good commit message:**
```
feat(modal): add word forward/backward motion (w/b)

Implement vim-style w and b commands for word-wise navigation.
Respects punctuation and whitespace as word boundaries.

- Add word boundary detection in modal_process_normal_mode_key
- Support count prefix (5w jumps 5 words forward)
- Include tests for edge cases (start/end of line, empty lines)

Closes #67
```

**Components:**
1. ✅ Type: `feat` (new feature)
2. ✅ Scope: `modal` (modal editing module)
3. ✅ Description: Concise, imperative, specific
4. ✅ Body: Explains what and why
5. ✅ Footer: References issue

**Benefits:**
- Searchable (`git log --grep="modal"`)
- Understandable (know what changed without reading code)
- Professional (looks like serious project)
- Automatable (changelog generation)
- Bisectable (easy to find when feature was added)

---

## Quick Reference

```bash
# Feature
git commit -m "feat(scope): add new capability"

# Bug fix
git commit -m "fix(scope): resolve specific issue"

# Documentation
git commit -m "docs(scope): update or add documentation"

# Refactoring
git commit -m "refactor(scope): restructure without behavior change"

# Tests
git commit -m "test(scope): add or update tests"

# Performance
git commit -m "perf(scope): improve performance"

# Style/Formatting
git commit -m "style(scope): fix formatting"

# Build/Dependencies
git commit -m "build(scope): update build system or dependencies"

# Chores/Maintenance
git commit -m "chore(scope): maintenance tasks"
```

---

## Questions?

**Q: What if my change doesn't fit a type?**
A: Use `chore` for miscellaneous changes or `refactor` for code improvements.

**Q: Can I use multiple types?**
A: No, choose the most significant type. Use the body to explain additional changes.

**Q: What if I forget the convention?**
A: Use the template (see Tools section) or refer to recent commits for examples.

**Q: Do I need a scope?**
A: Optional but recommended. Omit for project-wide changes.

**Q: How do I commit WIP changes?**
A: Don't commit work in progress to main branches. Use feature branches and proper commits even there.

**Q: Can I fix commit messages after pushing?**
A: Only if you haven't pushed to shared branches. Use `git commit --amend` or `git rebase -i` locally.

---

**Remember: Good commit messages are a gift to your future self and your collaborators.**

Write commits as if the person reading them is a developer who needs to understand what changed and why - because that's exactly who will read them (including you, six months from now).

---

**Further Reading:**
- [Conventional Commits](https://www.conventionalcommits.org/)
- [How to Write a Git Commit Message](https://chris.beams.io/posts/git-commit/)
- [Angular Commit Message Format](https://github.com/angular/angular/blob/main/CONTRIBUTING.md#commit)
