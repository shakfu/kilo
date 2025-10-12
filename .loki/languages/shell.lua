-- Shell script language definition (Bash, Zsh, etc.)
return loki.register_language({
    name = "Shell",
    extensions = {
        ".sh", ".bash", ".zsh", ".ksh", ".csh", ".tcsh",
        ".profile", ".bashrc", ".bash_profile", ".bash_login",
        ".zshrc", ".zshenv", ".zlogin", ".zprofile"
    },
    keywords = {
        -- Shell Keywords
        "if", "then", "else", "elif", "fi", "case", "esac", "for", "while",
        "until", "do", "done", "select", "function", "in", "time", "coproc",
    },
    types = {
        -- Common shell builtins and commands
        "alias", "bg", "bind", "break", "builtin", "caller", "cd",
        "command", "compgen", "complete", "continue", "declare",
        "dirs", "disown", "echo", "enable", "eval", "exec", "exit",
        "export", "false", "fc", "fg", "getopts", "hash", "help",
        "history", "jobs", "kill", "let", "local", "logout", "mapfile",
        "popd", "printf", "pushd", "pwd", "read", "readarray",
        "readonly", "return", "set", "shift", "shopt", "source",
        "suspend", "test", "times", "trap", "true", "type", "typeset",
        "ulimit", "umask", "unalias", "unset", "wait",

        -- System utilities (commonly used)
        "awk", "cat", "chmod", "chown", "cp", "curl", "cut", "date",
        "df", "diff", "dig", "du", "find", "grep", "head", "ln", "ls",
        "mkdir", "mv", "ping", "ps", "rm", "rsync", "scp", "sed",
        "ssh", "sudo", "tail", "tar", "top", "touch", "tr", "uniq",
        "wc", "wget", "which", "xargs",

        -- Special shell variables (common ones)
        "$BASH", "$HOME", "$PATH", "$PWD", "$USER", "$SHELL",
        "$HOSTNAME", "$UID", "$RANDOM", "$LINENO",
    },
    line_comment = "#",
    block_comment_start = "",
    block_comment_end = "",
    separators = ",.()+-/*=~%<>[]{}:;|&",
    highlight_strings = true,
    highlight_numbers = true
})
