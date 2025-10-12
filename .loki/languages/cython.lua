-- Cython language definition
return loki.register_language({
    name = "Cython",
    extensions = {".pyx", ".pxd", ".pxi"},
    keywords = {
        -- Python keywords
        "False", "None", "True", "and", "as", "assert", "async", "await", "break",
        "class", "continue", "def", "del", "elif", "else", "except", "finally",
        "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
        "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",

        -- Cython-specific keywords
        "cdef", "cpdef", "cimport", "ctypedef", "struct", "union", "enum",
        "public", "readonly", "extern", "nogil", "gil", "inline", "api",
        "DEF", "IF", "ELIF", "ELSE",
    },
    types = {
        -- C types
        "int", "long", "float", "double", "char", "short", "void",
        "signed", "unsigned", "const", "volatile", "size_t",
        -- Python types
        "str", "bool", "list", "dict", "tuple", "set", "frozenset",
        "bytes", "bytearray", "object", "type",
    },
    line_comment = "#",
    block_comment_start = "",
    block_comment_end = "",
    separators = ",.()+-/*=~%[]{}:",
    highlight_strings = true,
    highlight_numbers = true
})
