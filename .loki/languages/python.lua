-- Python language definition
return loki.register_language({
    name = "Python",
    extensions = {".py", ".pyw"},
    keywords = {
        "and", "as", "assert", "async", "await", "break", "class", "continue",
        "def", "del", "elif", "else", "except", "finally", "for", "from",
        "global", "if", "import", "in", "is", "lambda", "nonlocal", "not",
        "or", "pass", "raise", "return", "try", "while", "with", "yield"
    },
    types = {
        "int", "str", "list", "dict", "tuple", "set", "bool", "float",
        "bytes", "bytearray", "complex", "frozenset", "object", "None",
        "True", "False", "self", "cls"
    },
    line_comment = "#",
    block_comment_start = "",
    block_comment_end = "",
    separators = ",.()+-/*=~%[]{}:;",
    highlight_strings = true,
    highlight_numbers = true
})
