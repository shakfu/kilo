-- Java language definition
return loki.register_language({
    name = "Java",
    extensions = {".java"},
    keywords = {
        "abstract", "assert", "break", "case", "catch", "class", "const",
        "continue", "default", "do", "else", "enum", "extends", "final",
        "finally", "for", "goto", "if", "implements", "import", "instanceof",
        "interface", "native", "new", "package", "private", "protected",
        "public", "return", "static", "strictfp", "super", "switch",
        "synchronized", "this", "throw", "throws", "transient", "try",
        "volatile", "while"
    },
    types = {
        "boolean", "byte", "char", "double", "float", "int", "long", "short",
        "void", "Boolean", "Byte", "Character", "Double", "Float", "Integer",
        "Long", "Short", "String", "Object", "true", "false", "null"
    },
    line_comment = "//",
    block_comment_start = "/*",
    block_comment_end = "*/",
    separators = ",.()+-/*=~%<>[]{}:;?!|&^",
    highlight_strings = true,
    highlight_numbers = true
})
