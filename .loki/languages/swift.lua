-- Swift language definition
return loki.register_language({
    name = "Swift",
    extensions = {".swift"},
    keywords = {
        -- Swift Keywords
        "associatedtype", "class", "deinit", "enum", "extension", "fileprivate", "func",
        "import", "init", "inout", "internal", "let", "open", "operator", "private",
        "protocol", "public", "static", "struct", "subscript", "typealias", "var",
        "break", "case", "continue", "default", "defer", "do", "else", "fallthrough",
        "for", "guard", "if", "in", "repeat", "return", "switch", "where", "while",
        "as", "catch", "false", "is", "nil", "rethrows", "super", "self", "Self",
        "throw", "throws", "true", "try", "async", "await", "some", "any",
    },
    types = {
        -- Swift Types
        "Int", "Double", "Float", "Bool", "String", "Character", "Array",
        "Dictionary", "Set", "Optional", "Result", "Error", "AnyObject",
        "AnyClass", "Protocol", "Codable", "Hashable", "Equatable",
        "Comparable", "Collection", "Sequence",
    },
    line_comment = "//",
    block_comment_start = "/*",
    block_comment_end = "*/",
    separators = ",.()+-/*=~%<>[]{}:;",
    highlight_strings = true,
    highlight_numbers = true
})
