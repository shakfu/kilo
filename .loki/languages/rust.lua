-- Rust language definition
return loki.register_language({
    name = "Rust",
    extensions = {".rs"},
    keywords = {
        "as", "async", "await", "break", "const", "continue", "crate", "dyn",
        "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in",
        "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
        "self", "static", "struct", "super", "trait", "true", "type", "unsafe",
        "use", "where", "while"
    },
    types = {
        "bool", "char", "f32", "f64", "i8", "i16", "i32", "i64", "i128",
        "isize", "str", "u8", "u16", "u32", "u64", "u128", "usize",
        "String", "Vec", "Option", "Result", "Some", "None", "Ok", "Err",
        "Self"
    },
    line_comment = "//",
    block_comment_start = "/*",
    block_comment_end = "*/",
    separators = ",.()+-/*=~%<>[]{}:;?!|&^",
    highlight_strings = true,
    highlight_numbers = true
})
