-- JavaScript/TypeScript language definition
return loki.register_language({
    name = "JavaScript",
    extensions = {".js", ".jsx", ".ts", ".tsx", ".mjs"},
    keywords = {
        "async", "await", "break", "case", "catch", "class", "const", "continue",
        "debugger", "default", "delete", "do", "else", "export", "extends", "finally",
        "for", "from", "function", "if", "import", "in", "instanceof", "let", "new",
        "of", "return", "static", "super", "switch", "this", "throw", "try", "typeof",
        "var", "void", "while", "with", "yield"
    },
    types = {
        "Array", "Boolean", "Date", "Error", "Function", "JSON", "Math", "Number",
        "Object", "Promise", "RegExp", "String", "Symbol", "null", "undefined",
        "true", "false", "any", "string", "number", "boolean", "void"
    },
    line_comment = "//",
    block_comment_start = "/*",
    block_comment_end = "*/",
    separators = ",.()+-/*=~%<>[]{}:;?!|&^",
    highlight_strings = true,
    highlight_numbers = true
})
