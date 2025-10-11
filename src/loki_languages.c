/* loki_languages.c - Built-in language syntax definitions
 *
 * This file contains the syntax highlighting definitions for built-in languages.
 * Each language definition includes:
 * - File extensions
 * - Keywords (control flow and types)
 * - Comment delimiters (single-line and multi-line)
 * - Separator characters
 * - Highlighting flags
 *
 * To add a new language:
 * 1. Define extension array: char *YourLang_HL_extensions[]
 * 2. Define keywords array: char *YourLang_HL_keywords[]
 *    - Use "|" suffix for type keywords
 * 3. Add entry to HLDB array with comment delimiters and separators
 *
 * Note: The characters for single and multi line comments must be exactly two
 * and must be provided (see the C language example).
 */

#include "loki_internal.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ======================= C / C++ ========================================== */

char *C_HL_extensions[] = {".c",".h",".cpp",".hpp",".cc",NULL};
char *C_HL_keywords[] = {
	/* C Keywords */
	"auto","break","case","continue","default","do","else","enum",
	"extern","for","goto","if","register","return","sizeof","static",
	"struct","switch","typedef","union","volatile","while","NULL",

	/* C++ Keywords */
	"alignas","alignof","and","and_eq","asm","bitand","bitor","class",
	"compl","constexpr","const_cast","deltype","delete","dynamic_cast",
	"explicit","export","false","friend","inline","mutable","namespace",
	"new","noexcept","not","not_eq","nullptr","operator","or","or_eq",
	"private","protected","public","reinterpret_cast","static_assert",
	"static_cast","template","this","thread_local","throw","true","try",
	"typeid","typename","virtual","xor","xor_eq",

	/* C types */
        "int|","long|","double|","float|","char|","unsigned|","signed|",
        "void|","short|","auto|","const|","bool|",NULL
};

/* ======================= Python =========================================== */

char *Python_HL_extensions[] = {".py",".pyw",NULL};
char *Python_HL_keywords[] = {
	/* Python keywords */
	"False","None","True","and","as","assert","async","await","break",
	"class","continue","def","del","elif","else","except","finally",
	"for","from","global","if","import","in","is","lambda","nonlocal",
	"not","or","pass","raise","return","try","while","with","yield",

	/* Python built-in types */
	"int|","float|","str|","bool|","list|","dict|","tuple|","set|",
	"frozenset|","bytes|","bytearray|","object|","type|",NULL
};

/* ======================= Lua ============================================== */

char *Lua_HL_extensions[] = {".lua",NULL};
char *Lua_HL_keywords[] = {
	/* Lua keywords */
	"and","break","do","else","elseif","end","false","for","function",
	"goto","if","in","local","nil","not","or","repeat","return","then",
	"true","until","while",

	/* Lua built-in functions */
	"assert|","collectgarbage|","dofile|","error|","getmetatable|",
	"ipairs|","load|","loadfile|","next|","pairs|","pcall|","print|",
	"rawequal|","rawget|","rawlen|","rawset|","require|","select|",
	"setmetatable|","tonumber|","tostring|","type|","xpcall|",NULL
};

/* ======================= Cython =========================================== */

char *Cython_HL_extensions[] = {".pyx",".pxd",".pxi",NULL};
char *Cython_HL_keywords[] = {
	/* Python keywords */
	"False","None","True","and","as","assert","async","await","break",
	"class","continue","def","del","elif","else","except","finally",
	"for","from","global","if","import","in","is","lambda","nonlocal",
	"not","or","pass","raise","return","try","while","with","yield",

	/* Cython-specific keywords */
	"cdef","cpdef","cimport","ctypedef","struct","union","enum",
	"public","readonly","extern","nogil","gil","inline","api",
	"DEF","IF","ELIF","ELSE",

	/* Python/Cython built-in types */
	"int|","long|","float|","double|","char|","short|","void|",
	"signed|","unsigned|","const|","volatile|","size_t|",
	"str|","bool|","list|","dict|","tuple|","set|","frozenset|",
	"bytes|","bytearray|","object|","type|",NULL
};

/* ======================= Markdown ========================================= */

char *MD_HL_extensions[] = {".md",".markdown",NULL};

/* ======================= Language Database ================================ */

/* Array of syntax highlights by extensions, keywords, comments delimiters and flags. */
struct t_editor_syntax HLDB[] = {
    {
        /* C / C++ */
        C_HL_extensions,
        C_HL_keywords,
        "//","/*","*/",
        ",.()+-/*=~%[];",  /* Separators */
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS,
        HL_TYPE_C
    },
    {
        /* Python */
        Python_HL_extensions,
        Python_HL_keywords,
        "#","","",  /* Python uses # for comments, no block comments */
        ",.()+-/*=~%[]{}:",  /* Separators */
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS,
        HL_TYPE_C
    },
    {
        /* Lua */
        Lua_HL_extensions,
        Lua_HL_keywords,
        "--","--[[","]]",  /* Lua comments */
        ",.()+-/*=~%[]{}:",  /* Separators */
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS,
        HL_TYPE_C
    },
    {
        /* Cython */
        Cython_HL_extensions,
        Cython_HL_keywords,
        "#","","",  /* Same as Python */
        ",.()+-/*=~%[]{}:",  /* Separators */
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS,
        HL_TYPE_C
    },
    {
        /* Markdown */
        MD_HL_extensions,
        NULL,  /* No keywords */
        "","","",  /* No comments */
        "",  /* No separators */
        0,  /* No flags */
        HL_TYPE_MARKDOWN
    }
};

#define HLDB_ENTRIES (sizeof(HLDB)/sizeof(HLDB[0]))

/* Get the number of built-in language entries */
unsigned int loki_get_builtin_language_count(void) {
    return HLDB_ENTRIES;
}

/* ======================= Syntax Highlighting Functions ==================== */

/* Helper function to highlight code block content with specified language rules.
 * This is a simplified version of editor_update_syntax for use within markdown. */
void highlight_code_line(t_erow *row, char **keywords, char *scs, char *separators) {
    if (row->rsize == 0) return;

    int i = 0, prev_sep = 1, in_string = 0;
    char *p = row->render;

    while (i < row->rsize) {
        /* Handle // comments (if scs is provided) */
        if (scs && scs[0] && prev_sep && i < row->rsize - 1 &&
            p[i] == scs[0] && p[i+1] == scs[1]) {
            memset(row->hl + i, HL_COMMENT, row->rsize - i);
            return;
        }

        /* Handle strings */
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (i < row->rsize - 1 && p[i] == '\\') {
                row->hl[i+1] = HL_STRING;
                i += 2;
                prev_sep = 0;
                continue;
            }
            if (p[i] == in_string) in_string = 0;
            i++;
            continue;
        } else {
            if (p[i] == '"' || p[i] == '\'') {
                in_string = p[i];
                row->hl[i] = HL_STRING;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        /* Handle numbers */
        if ((isdigit(p[i]) && (prev_sep || row->hl[i-1] == HL_NUMBER)) ||
            (p[i] == '.' && i > 0 && row->hl[i-1] == HL_NUMBER &&
             i < row->rsize - 1 && isdigit(p[i+1]))) {
            row->hl[i] = HL_NUMBER;
            i++;
            prev_sep = 0;
            continue;
        }

        /* Handle keywords */
        if (prev_sep && keywords) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen-1] == '|';
                if (kw2) klen--;

                if (i + klen <= row->rsize &&
                    !memcmp(p + i, keywords[j], klen) &&
                    is_separator(p[i + klen], separators))
                {
                    memset(row->hl + i, kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(p[i], separators);
        i++;
    }
}

/* Markdown syntax highlighting. */
void editor_update_syntax_markdown(editor_ctx_t *ctx, t_erow *row) {
    unsigned char *new_hl = realloc(row->hl, row->rsize);
    if (new_hl == NULL) return;
    row->hl = new_hl;
    memset(row->hl, HL_NORMAL, row->rsize);

    char *p = row->render;
    int i = 0;
    int prev_cb_lang = (row->idx > 0 && ctx && ctx->row) ? ctx->row[row->idx - 1].cb_lang : CB_LANG_NONE;

    /* Code blocks: lines starting with ``` */
    if (row->rsize >= 3 && p[0] == '`' && p[1] == '`' && p[2] == '`') {
        /* Opening or closing code fence */
        memset(row->hl, HL_STRING, row->rsize);

        if (prev_cb_lang != CB_LANG_NONE) {
            /* Closing fence */
            row->cb_lang = CB_LANG_NONE;
        } else {
            /* Opening fence - detect language */
            row->cb_lang = CB_LANG_NONE;
            if (row->rsize > 3) {
                char *lang = p + 3;
                /* Skip whitespace */
                while (*lang && isspace(*lang)) lang++;

                if (strncmp(lang, "cython", 6) == 0 ||
                    strncmp(lang, "pyx", 3) == 0 ||
                    strncmp(lang, "pxd", 3) == 0) {
                    row->cb_lang = CB_LANG_CYTHON;
                } else if (strncmp(lang, "c", 1) == 0 &&
                    (lang[1] == '\0' || isspace(lang[1]) || lang[1] == 'p')) {
                    if (lang[1] == 'p' && lang[2] == 'p') {
                        row->cb_lang = CB_LANG_C; /* C++ */
                    } else if (lang[1] == '\0' || isspace(lang[1])) {
                        row->cb_lang = CB_LANG_C; /* C */
                    }
                } else if (strncmp(lang, "python", 6) == 0 || strncmp(lang, "py", 2) == 0) {
                    row->cb_lang = CB_LANG_PYTHON;
                } else if (strncmp(lang, "lua", 3) == 0) {
                    row->cb_lang = CB_LANG_LUA;
                }
            }
        }
        return;
    }

    /* Inside code block - apply language-specific highlighting */
    if (prev_cb_lang != CB_LANG_NONE) {
        row->cb_lang = prev_cb_lang;

        char **keywords = NULL;
        char *scs = NULL;
        char *separators = ",.()+-/*=~%[];";

        switch (prev_cb_lang) {
            case CB_LANG_C:
                keywords = C_HL_keywords;
                scs = "//";
                break;
            case CB_LANG_PYTHON:
                keywords = Python_HL_keywords;
                scs = "#";
                break;
            case CB_LANG_LUA:
                keywords = Lua_HL_keywords;
                scs = "--";
                break;
            case CB_LANG_CYTHON:
                keywords = Cython_HL_keywords;
                scs = "#";
                break;
        }

        highlight_code_line(row, keywords, scs, separators);
        return;
    }

    /* Not in code block - reset */
    row->cb_lang = CB_LANG_NONE;

    /* Headers: # ## ### etc. at start of line */
    if (row->rsize > 0 && p[0] == '#') {
        int header_len = 0;
        while (header_len < row->rsize && p[header_len] == '#')
            header_len++;
        if (header_len < row->rsize && (p[header_len] == ' ' || p[header_len] == '\t')) {
            /* Valid header - highlight entire line */
            memset(row->hl, HL_KEYWORD1, row->rsize);
            return;
        }
    }

    /* Lists: lines starting with *, -, or + followed by space */
    if (row->rsize >= 2 && (p[0] == '*' || p[0] == '-' || p[0] == '+') &&
        (p[1] == ' ' || p[1] == '\t')) {
        row->hl[0] = HL_KEYWORD2;
    }

    /* Inline patterns: bold, italic, code, links */
    i = 0;
    while (i < row->rsize) {
        /* Inline code: `text` */
        if (p[i] == '`') {
            row->hl[i] = HL_STRING;
            i++;
            while (i < row->rsize && p[i] != '`') {
                row->hl[i] = HL_STRING;
                i++;
            }
            if (i < row->rsize) {
                row->hl[i] = HL_STRING; /* Closing ` */
                i++;
            }
            continue;
        }

        /* Bold: **text** */
        if (i < row->rsize - 1 && p[i] == '*' && p[i+1] == '*') {
            int start = i;
            i += 2;
            while (i < row->rsize - 1) {
                if (p[i] == '*' && p[i+1] == '*') {
                    /* Found closing ** */
                    memset(row->hl + start, HL_KEYWORD2, i - start + 2);
                    i += 2;
                    break;
                }
                i++;
            }
            continue;
        }

        /* Italic: *text* or _text_ */
        if (p[i] == '*' || p[i] == '_') {
            char marker = p[i];
            int start = i;
            i++;
            while (i < row->rsize) {
                if (p[i] == marker) {
                    /* Found closing marker */
                    memset(row->hl + start, HL_COMMENT, i - start + 1);
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }

        /* Links: [text](url) */
        if (p[i] == '[') {
            int start = i;
            i++;
            /* Find closing ] */
            while (i < row->rsize && p[i] != ']') i++;
            if (i < row->rsize && i + 1 < row->rsize && p[i+1] == '(') {
                /* Found ]( - continue to find ) */
                i += 2;
                while (i < row->rsize && p[i] != ')') i++;
                if (i < row->rsize) {
                    /* Complete link found */
                    memset(row->hl + start, HL_NUMBER, i - start + 1);
                    i++;
                    continue;
                }
            }
            i = start + 1; /* Not a link, continue from next char */
            continue;
        }

        i++;
    }
}
