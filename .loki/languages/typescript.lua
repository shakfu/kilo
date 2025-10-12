-- TypeScript language definition
return loki.register_language({
    name = "TypeScript",
    extensions = {".ts", ".tsx"},
    keywords = {
        -- JavaScript Keywords
        "break", "case", "catch", "class", "const", "continue", "debugger", "default",
        "delete", "do", "else", "export", "extends", "finally", "for", "function",
        "if", "import", "in", "instanceof", "let", "new", "return", "super", "switch",
        "this", "throw", "try", "typeof", "var", "void", "while", "with", "yield",
        "async", "await", "of", "true", "false", "null", "undefined",

        -- TypeScript Specific
        "interface", "type", "enum", "namespace", "module", "declare", "abstract",
        "implements", "private", "protected", "public", "readonly", "static",
        "get", "set", "as", "keyof", "infer", "is", "asserts",
    },
    types = {
        -- TypeScript Types
        "string", "number", "boolean", "object", "any", "unknown", "never",
        "void", "bigint", "symbol", "Array", "Promise", "Record", "Partial",
        "Required", "Pick", "Omit", "Exclude", "Extract", "NonNullable",
    },
    line_comment = "//",
    block_comment_start = "/*",
    block_comment_end = "*/",
    separators = ",.()+-/*=~%<>[]{}:;",
    highlight_strings = true,
    highlight_numbers = true
})
