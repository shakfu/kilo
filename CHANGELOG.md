# CHANGELOG

All notable project-wide changes will be documented in this file. Note that each subproject has its own CHANGELOG.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and [Commons Changelog](https://common-changelog.org). This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Types of Changes

- Added: for new features.
- Changed: for changes in existing functionality.
- Deprecated: for soon-to-be removed features.
- Removed: for now removed features.
- Fixed: for any bug fixes.
- Security: in case of vulnerabilities.

---

## [0.1.1]

### Security
- Fixed multiple buffer overflow vulnerabilities in syntax highlighting that could read/write beyond allocated buffers
- Fixed incorrect buffer size usage in comment highlighting preventing memory corruption
- Fixed keyword matching that could read past end of buffer

### Added
- Added binary file detection that refuses to open files with null bytes
- Added configurable separator list per syntax definition for better language support

### Fixed
- Fixed cursor position calculation bug causing incorrect cursor movement when wrapping from end of long lines
- Added NULL checks for all memory allocations to prevent crashes on allocation failure
- Fixed incomplete CRLF newline stripping for files with Windows line endings
- Fixed silent allocation failures in screen rendering that caused corrupted display
- Fixed infinite loop possibility in editorReadKey when stdin reaches EOF
- Fixed missing null terminator maintenance in character deletion operations
- Fixed integer overflow in row allocation for extremely large files
- Removed dead code in nonprint character calculation
- Fixed typos in comments: "commen" → "comment", "remainign" → "remaining", "escluding" → "excluding"
- Fixed typo in user-facing welcome message: "verison" → "version"
- Fixed number highlighting to only accept decimal points followed by digits (no trailing periods)

### Changed
- Refactored signal handler for window resize to be async-signal-safe (now uses flag instead of direct I/O)
- Moved atexit() registration to beginning of main() to ensure terminal cleanup on all exit paths
- Improved error messages for out-of-memory conditions
- Made separators configurable per syntax by adding field to editorSyntax structure
- Documented non-reentrant global state limitation in code comments


## [0.1.0] - Initial Release

- Project created
