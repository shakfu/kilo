# Loki Editor Roadmap

This document outlines the planned features and enhancements for the Loki editor.

## Core Editor Enhancements (C Implementation)

These changes focus on improving the fundamental editing experience and providing a solid foundation for more advanced features.

- [ ] **Undo/Redo Functionality**: Implement a robust undo/redo stack to allow users to easily revert and re-apply changes.
- [ ] **File Browser**: Add a simple built-in file browser to open files without exiting the editor.
- [ ] **Line Numbers**: Implement an optional gutter to display line numbers.
- [ ] **Expanded Syntax Highlighting Engine**:
  - [ ] Add support for more languages (e.g., Python, JavaScript, Lua, Shell).
  - [ ] Enhance the engine to support regex-based patterns for more sophisticated highlighting.
- [ ] **Auto-indentation**: Automatically indent new lines to match the indentation of the previous line.
- [ ] **Bracket/Pair Matching**: Highlight matching brackets `()`, `{}`, `[]`.
- [ ] **Tabbed Editing**: Allow multiple files to be open in tabs.
- [ ] **Theming/Color Customization**: Allow users to define editor colors via `init.lua`.

### Lua API and Scripting Enhancements

These suggestions focus on making the Lua scripting environment more powerful and flexible, enabling deeper customization.

- [ ] **Language Server Protocol (LSP) Integration**:
  - [ ] Implement a client for the Language Server Protocol to provide IDE-like features (completion, diagnostics, go-to-definition).
  - [ ] Expose LSP data to Lua for custom UI rendering.
- [ ] **Expanded Lua API**:
  - [ ] **Key-binding API**: Allow users to define custom key bindings in `init.lua`.
  - [ ] **Buffer Manipulation API**: Expose more functions for direct buffer manipulation from Lua (e.g., `set_line`, `delete_line`, `get_selection`).
  - [ ] **Configuration API**: Allow more editor settings to be configured from Lua.
- [ ] **Improved Autocompletion**:
  - [ ] Implement a basic word completion system based on the current buffer content.
  - [ ] Add support for user-defined snippets.
- [ ] **Enhanced REPL**:
  - [ ] Add tab-completion for the `loki` API and global variables.

### AI Feature Enhancements

Building on the existing asynchronous AI capabilities.

- [ ] **Streaming AI Responses**: Modify the Lua AI helpers to handle streaming responses, allowing text to be inserted as it arrives.
- [x] **Context-Aware Prompting**: Improve AI functions to send only relevant context to the API instead of the whole file.
- [ ] **New AI Commands**:
  - [ ] `ai_refactor`: Refactor a selection of code based on a user prompt.
  - [ ] `ai_document`: Generate documentation for a function or code block.
  - [ ] `ai_commit_msg`: Generate a git commit message based on staged changes.
