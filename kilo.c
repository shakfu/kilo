/* Kilo -- A very simple editor in less than 1-kilo lines of code (as counted
 *         by "cloc"). Does not depend on libcurses, directly emits VT100
 *         escapes on the terminal.
 *
 * -----------------------------------------------------------------------
 *
 * Copyright (C) 2016 Salvatore Sanfilippo <antirez at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define KILO_VERSION "0.4.1"

#ifdef __linux__
#define _POSIX_C_SOURCE 200809L
#endif

#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <signal.h>

/* Lua scripting support (from Homebrew) */
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/* libcurl for async HTTP */
#include <curl/curl.h>

/* Syntax highlight types */
#define HL_NORMAL 0
#define HL_NONPRINT 1
#define HL_COMMENT 2   /* Single line comment. */
#define HL_MLCOMMENT 3 /* Multi-line comment. */
#define HL_KEYWORD1 4
#define HL_KEYWORD2 5
#define HL_STRING 6
#define HL_NUMBER 7
#define HL_MATCH 8      /* Search match. */

#define HL_HIGHLIGHT_STRINGS (1<<0)
#define HL_HIGHLIGHT_NUMBERS (1<<1)

/* Syntax highlighting types */
#define HL_TYPE_C 0
#define HL_TYPE_MARKDOWN 1

/* Code block language types for markdown */
#define CB_LANG_NONE 0
#define CB_LANG_C 1
#define CB_LANG_PYTHON 2
#define CB_LANG_LUA 3
#define CB_LANG_CYTHON 4

struct t_editor_syntax {
    char **filematch;
    char **keywords;
    char singleline_comment_start[2];
    char multiline_comment_start[3];
    char multiline_comment_end[3];
    char *separators;
    int flags;
    int type;  /* HL_TYPE_* */
};

/* This structure represents a single line of the file we are editing. */
typedef struct t_erow {
    int idx;            /* Row index in the file, zero-based. */
    int size;           /* Size of the row, excluding the null term. */
    int rsize;          /* Size of the rendered row. */
    char *chars;        /* Row content. */
    char *render;       /* Row content "rendered" for screen (for TABs). */
    unsigned char *hl;  /* Syntax highlight type for each character in render.*/
    int hl_oc;          /* Row had open comment at end in last syntax highlight
                           check. */
    int cb_lang;        /* Code block language (for markdown): CB_LANG_* */
} t_erow;

typedef struct t_hlcolor {
    int r,g,b;
} t_hlcolor;

struct t_editor_config {
    int cx,cy;  /* Cursor x and y position in characters */
    int rowoff;     /* Offset of row displayed. */
    int coloff;     /* Offset of column displayed. */
    int screenrows; /* Number of rows that we can show */
    int screencols; /* Number of cols that we can show */
    int numrows;    /* Number of rows */
    int rawmode;    /* Is terminal raw mode enabled? */
    t_erow *row;      /* Rows */
    int dirty;      /* File modified but not saved. */
    char *filename; /* Currently open filename */
    char statusmsg[80];
    time_t statusmsg_time;
    struct t_editor_syntax *syntax;    /* Current syntax highlight, or NULL. */
    lua_State *L;   /* Lua interpreter state */
};

/* Global editor state. Note: This makes the editor non-reentrant and
 * non-thread-safe. Only one editor instance can exist per process. */
static struct t_editor_config E;

/* Flag to indicate window size change (set by signal handler) */
static volatile sig_atomic_t winsize_changed = 0;

/* ======================= Async HTTP Infrastructure ======================== */

/* Structure to hold response data from curl */
struct curl_response {
    char *data;
    size_t size;
};

/* Async HTTP request tracking */
typedef struct async_http_request {
    CURLM *multi_handle;
    CURL *easy_handle;
    struct curl_response response;
    char *lua_callback;      /* Name of Lua function to call with response */
    int completed;
    int failed;
    char error_buffer[CURL_ERROR_SIZE];
} async_http_request;

#define MAX_ASYNC_REQUESTS 10
static async_http_request *pending_requests[MAX_ASYNC_REQUESTS] = {0};
static int num_pending = 0;

/* libcurl global initialization flag */
static int curl_initialized = 0;

enum KEY_ACTION{
        KEY_NULL = 0,       /* NULL */
        CTRL_C = 3,         /* Ctrl-c */
        CTRL_D = 4,         /* Ctrl-d */
        CTRL_F = 6,         /* Ctrl-f */
        CTRL_H = 8,         /* Ctrl-h */
        TAB = 9,            /* Tab */
        CTRL_L = 12,        /* Ctrl+l */
        ENTER = 13,         /* Enter */
        CTRL_Q = 17,        /* Ctrl-q */
        CTRL_S = 19,        /* Ctrl-s */
        CTRL_U = 21,        /* Ctrl-u */
        ESC = 27,           /* Escape */
        BACKSPACE =  127,   /* Backspace */
        /* The following are just soft codes, not really reported by the
         * terminal directly. */
        ARROW_LEFT = 1000,
        ARROW_RIGHT,
        ARROW_UP,
        ARROW_DOWN,
        DEL_KEY,
        HOME_KEY,
        END_KEY,
        PAGE_UP,
        PAGE_DOWN
};

void editor_set_status_msg(const char *fmt, ...);
static void exec_lua_command(int fd);
static void cleanup_curl(void);
static void check_async_requests(void);

/* =========================== Syntax highlights DB =========================
 *
 * In order to add a new syntax, define two arrays with a list of file name
 * matches and keywords. The file name matches are used in order to match
 * a given syntax with a given file name: if a match pattern starts with a
 * dot, it is matched as the last past of the filename, for example ".c".
 * Otherwise the pattern is just searched inside the filenme, like "Makefile").
 *
 * The list of keywords to highlight is just a list of words, however if they
 * a trailing '|' character is added at the end, they are highlighted in
 * a different color, so that you can have two different sets of keywords.
 *
 * Finally add a stanza in the HLDB global variable with two two arrays
 * of strings, and a set of flags in order to enable highlighting of
 * comments and numbers.
 *
 * The characters for single and multi line comments must be exactly two
 * and must be provided as well (see the C language example).
 *
 * There is no support to highlight patterns currently. */

/* C / C++ */
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

/* Python keywords for code blocks */
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

/* Lua keywords for code blocks */
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

/* Cython keywords for code blocks (extends Python) */
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

/* Markdown extensions */
char *MD_HL_extensions[] = {".md",".markdown",NULL};

/* Here we define an array of syntax highlights by extensions, keywords,
 * comments delimiters and flags. */
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

/* ======================= Low level terminal handling ====================== */

static struct termios orig_termios; /* In order to restore at exit.*/

void disable_raw_mode(int fd) {
    /* Don't even check the return value as it's too late. */
    if (E.rawmode) {
        tcsetattr(fd,TCSAFLUSH,&orig_termios);
        E.rawmode = 0;
    }
}

/* Called at exit to avoid remaining in raw mode. */
void editor_atexit(void) {
    disable_raw_mode(STDIN_FILENO);
    if (E.L) {
        lua_close(E.L);
        E.L = NULL;
    }
    cleanup_curl();
}

/* Raw mode: 1960 magic shit. */
int enable_raw_mode(int fd) {
    struct termios raw;

    if (E.rawmode) return 0; /* Already enabled. */
    if (!isatty(STDIN_FILENO)) goto fatal;
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer. */
    raw.c_cc[VMIN] = 0; /* Return each byte, or zero for timeout. */
    raw.c_cc[VTIME] = 1; /* 100 ms timeout (unit is tens of second). */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    E.rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

/* Read a key from the terminal put in raw mode, trying to handle
 * escape sequences. */
int editor_read_key(int fd) {
    int nread;
    char c, seq[3];
    int retries = 0;
    /* Wait for input with timeout. If we get too many consecutive
     * zero-byte reads, stdin may be closed. */
    while ((nread = read(fd,&c,1)) == 0) {
        if (++retries > 1000) {
            /* After ~100 seconds of no input, assume stdin is closed */
            fprintf(stderr, "\nNo input received, exiting.\n");
            exit(0);
        }
    }
    if (nread == -1) exit(1);

    while(1) {
        switch(c) {
        case ESC:    /* escape sequence */
            /* If this is just an ESC, we'll timeout here. */
            if (read(fd,seq,1) == 0) return ESC;
            if (read(fd,seq+1,1) == 0) return ESC;

            /* ESC [ sequences. */
            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    /* Extended escape, read additional byte. */
                    if (read(fd,seq+2,1) == 0) return ESC;
                    if (seq[2] == '~') {
                        switch(seq[1]) {
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        }
                    }
                } else {
                    switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                    }
                }
            }

            /* ESC O sequences. */
            else if (seq[0] == 'O') {
                switch(seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
            break;
        default:
            return c;
        }
    }
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor is stored at *rows and *cols and 0 is returned. */
int get_cursor_position(int ifd, int ofd, int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    /* Report cursor location */
    if (write(ofd, "\x1b[6n", 4) != 4) return -1;

    /* Read the response: ESC [ rows ; cols R */
    while (i < sizeof(buf)-1) {
        if (read(ifd,buf+i,1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    /* Parse it. */
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf+2,"%d;%d",rows,cols) != 2) return -1;
    return 0;
}

/* Try to get the number of columns in the current terminal. If the ioctl()
 * call fails the function will try to query the terminal itself.
 * Returns 0 on success, -1 on error. */
int get_window_size(int ifd, int ofd, int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* ioctl() failed. Try to query the terminal itself. */
        int orig_row, orig_col, retval;

        /* Get the initial position so we can restore it later. */
        retval = get_cursor_position(ifd,ofd,&orig_row,&orig_col);
        if (retval == -1) goto failed;

        /* Go to right/bottom margin and get position. */
        if (write(ofd,"\x1b[999C\x1b[999B",12) != 12) goto failed;
        retval = get_cursor_position(ifd,ofd,rows,cols);
        if (retval == -1) goto failed;

        /* Restore position. */
        char seq[32];
        snprintf(seq,32,"\x1b[%d;%dH",orig_row,orig_col);
        if (write(ofd,seq,strlen(seq)) == -1) {
            /* Can't recover... */
        }
        return 0;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }

failed:
    return -1;
}

/* ====================== Syntax highlight color scheme  ==================== */

int is_separator(int c, char *separators) {
    return c == '\0' || isspace(c) || strchr(separators, c) != NULL;
}

/* Return true if the specified row last char is part of a multi line comment
 * that starts at this row or at one before, and does not end at the end
 * of the row but spawns to the next row. */
int editor_row_has_open_comment(t_erow *row) {
    if (row->hl && row->rsize && row->hl[row->rsize-1] == HL_MLCOMMENT &&
        (row->rsize < 2 || (row->render[row->rsize-2] != '*' ||
                            row->render[row->rsize-1] != '/'))) return 1;
    return 0;
}

/* Forward declaration for markdown highlighter */
void editor_update_syntax_markdown(t_erow *row);

/* Set every byte of row->hl (that corresponds to every character in the line)
 * to the right syntax highlight type (HL_* defines). */
void editor_update_syntax(t_erow *row) {
    unsigned char *new_hl = realloc(row->hl,row->rsize);
    if (new_hl == NULL) return; /* Out of memory, keep old highlighting */
    row->hl = new_hl;
    memset(row->hl,HL_NORMAL,row->rsize);

    if (E.syntax == NULL) return; /* No syntax, everything is HL_NORMAL. */

    /* Dispatch to markdown highlighter if needed */
    if (E.syntax->type == HL_TYPE_MARKDOWN) {
        editor_update_syntax_markdown(row);
        return;
    }

    int i, prev_sep, in_string, in_comment;
    char *p;
    char **keywords = E.syntax->keywords;
    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;
    char *separators = E.syntax->separators;

    /* Point to the first non-space char. */
    p = row->render;
    i = 0; /* Current char offset */
    while(*p && isspace(*p)) {
        p++;
        i++;
    }
    prev_sep = 1; /* Tell the parser if 'i' points to start of word. */
    in_string = 0; /* Are we inside "" or '' ? */
    in_comment = 0; /* Are we inside multi-line comment? */

    /* If the previous line has an open comment, this line starts
     * with an open comment state. */
    if (row->idx > 0 && editor_row_has_open_comment(&E.row[row->idx-1]))
        in_comment = 1;

    while(*p) {
        /* Handle // comments. */
        if (prev_sep && i < row->rsize - 1 && *p == scs[0] && *(p+1) == scs[1]) {
            /* From here to end is a comment */
            memset(row->hl+i,HL_COMMENT,row->rsize-i);
            return;
        }

        /* Handle multi line comments. */
        if (in_comment) {
            row->hl[i] = HL_MLCOMMENT;
            if (i < row->rsize - 1 && *p == mce[0] && *(p+1) == mce[1]) {
                row->hl[i+1] = HL_MLCOMMENT;
                p += 2; i += 2;
                in_comment = 0;
                prev_sep = 1;
                continue;
            } else {
                prev_sep = 0;
                p++; i++;
                continue;
            }
        } else if (i < row->rsize - 1 && *p == mcs[0] && *(p+1) == mcs[1]) {
            row->hl[i] = HL_MLCOMMENT;
            row->hl[i+1] = HL_MLCOMMENT;
            p += 2; i += 2;
            in_comment = 1;
            prev_sep = 0;
            continue;
        }

        /* Handle "" and '' */
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (i < row->rsize - 1 && *p == '\\') {
                row->hl[i+1] = HL_STRING;
                p += 2; i += 2;
                prev_sep = 0;
                continue;
            }
            if (*p == in_string) in_string = 0;
            p++; i++;
            continue;
        } else {
            if (*p == '"' || *p == '\'') {
                in_string = *p;
                row->hl[i] = HL_STRING;
                p++; i++;
                prev_sep = 0;
                continue;
            }
        }

        /* Handle non printable chars. */
        if (!isprint(*p)) {
            row->hl[i] = HL_NONPRINT;
            p++; i++;
            prev_sep = 0;
            continue;
        }

        /* Handle numbers */
        if ((isdigit(*p) && (prev_sep || row->hl[i-1] == HL_NUMBER)) ||
            (*p == '.' && i > 0 && row->hl[i-1] == HL_NUMBER &&
             i < row->rsize - 1 && isdigit(*(p+1)))) {
            row->hl[i] = HL_NUMBER;
            p++; i++;
            prev_sep = 0;
            continue;
        }

        /* Handle keywords and lib calls */
        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen-1] == '|';
                if (kw2) klen--;

                if (i + klen <= row->rsize &&
                    !memcmp(p,keywords[j],klen) &&
                    is_separator(*(p+klen), separators))
                {
                    /* Keyword */
                    memset(row->hl+i,kw2 ? HL_KEYWORD2 : HL_KEYWORD1,klen);
                    p += klen;
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue; /* We had a keyword match */
            }
        }

        /* Not special chars */
        prev_sep = is_separator(*p, separators);
        p++; i++;
    }

    /* Propagate syntax change to the next row if the open comment
     * state changed. This may recursively affect all the following rows
     * in the file. */
    int oc = editor_row_has_open_comment(row);
    if (row->hl_oc != oc && row->idx+1 < E.numrows)
        editor_update_syntax(&E.row[row->idx+1]);
    row->hl_oc = oc;
}

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
void editor_update_syntax_markdown(t_erow *row) {
    unsigned char *new_hl = realloc(row->hl, row->rsize);
    if (new_hl == NULL) return;
    row->hl = new_hl;
    memset(row->hl, HL_NORMAL, row->rsize);

    char *p = row->render;
    int i = 0;
    int prev_cb_lang = (row->idx > 0) ? E.row[row->idx - 1].cb_lang : CB_LANG_NONE;

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

/* Maps syntax highlight token types to terminal colors. */
int editor_syntax_to_color(int hl) {
    switch(hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT:  return 90;  /* gray (bright black) */
    case HL_KEYWORD1:   return 95;  /* bright magenta (pink) */
    case HL_KEYWORD2:   return 36;  /* bright cyan (classes/types) */
    case HL_STRING:     return 33;  /* bright yellow */
    case HL_NUMBER:     return 35;  /* magenta (purple-ish) */
    case HL_MATCH:      return 34;  /* blue (keep as-is) */
    default:            return 37;  /* white */
    }
}

/* Select the syntax highlight scheme depending on the filename,
 * setting it in the global state E.syntax. */
void editor_select_syntax_highlight(char *filename) {
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct t_editor_syntax *s = HLDB+j;
        unsigned int i = 0;
        while(s->filematch[i]) {
            char *p;
            int patlen = strlen(s->filematch[i]);
            if ((p = strstr(filename,s->filematch[i])) != NULL) {
                if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
                    E.syntax = s;
                    return;
                }
            }
            i++;
        }
    }
}

/* ======================= Editor rows implementation ======================= */

/* Update the rendered version and the syntax highlight of a row. */
void editor_update_row(t_erow *row) {
    unsigned int tabs = 0;
    int j, idx;

   /* Create a version of the row we can directly print on the screen,
     * respecting tabs, substituting non printable characters with '?'. */
    free(row->render);
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == TAB) tabs++;

    unsigned long long allocsize =
        (unsigned long long) row->size + tabs*8 + 1;
    if (allocsize > UINT32_MAX) {
        printf("Some line of the edited file is too long for kilo\n");
        exit(1);
    }

    row->render = malloc(row->size + tabs*8 + 1);
    if (row->render == NULL) {
        perror("Out of memory");
        exit(1);
    }
    idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == TAB) {
            row->render[idx++] = ' ';
            while((idx+1) % 8 != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->rsize = idx;
    row->render[idx] = '\0';

    /* Update the syntax highlighting attributes of the row. */
    editor_update_syntax(row);
}

/* Insert a row at the specified position, shifting the other rows on the bottom
 * if required. */
void editor_insert_row(int at, char *s, size_t len) {
    if (at > E.numrows) return;
    /* Check for integer overflow in allocation size calculation */
    if ((size_t)E.numrows >= SIZE_MAX / sizeof(t_erow)) {
        fprintf(stderr, "Too many rows, cannot allocate more memory\n");
        exit(1);
    }
    t_erow *new_row = realloc(E.row,sizeof(t_erow)*(E.numrows+1));
    if (new_row == NULL) {
        perror("Out of memory");
        exit(1);
    }
    E.row = new_row;
    if (at != E.numrows) {
        memmove(E.row+at+1,E.row+at,sizeof(E.row[0])*(E.numrows-at));
        for (int j = at+1; j <= E.numrows; j++) E.row[j].idx++;
    }
    E.row[at].size = len;
    E.row[at].chars = malloc(len+1);
    if (E.row[at].chars == NULL) {
        perror("Out of memory");
        exit(1);
    }
    memcpy(E.row[at].chars,s,len+1);
    E.row[at].hl = NULL;
    E.row[at].hl_oc = 0;
    E.row[at].cb_lang = CB_LANG_NONE;
    E.row[at].render = NULL;
    E.row[at].rsize = 0;
    E.row[at].idx = at;
    editor_update_row(E.row+at);
    E.numrows++;
    E.dirty++;
}

/* Free row's heap allocated stuff. */
void editor_free_row(t_erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

/* Remove the row at the specified position, shifting the remaining on the
 * top. */
void editor_del_row(int at) {
    t_erow *row;

    if (at >= E.numrows) return;
    row = E.row+at;
    editor_free_row(row);
    memmove(E.row+at,E.row+at+1,sizeof(E.row[0])*(E.numrows-at-1));
    for (int j = at; j < E.numrows-1; j++) E.row[j].idx++;
    E.numrows--;
    E.dirty++;
}

/* Turn the editor rows into a single heap-allocated string.
 * Returns the pointer to the heap-allocated string and populate the
 * integer pointed by 'buflen' with the size of the string, excluding
 * the final nulterm. */
char *editor_rows_to_string(int *buflen) {
    char *buf = NULL, *p;
    int totlen = 0;
    int j;

    /* Compute count of bytes */
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size+1; /* +1 is for "\n" at end of every row */
    *buflen = totlen;
    totlen++; /* Also make space for nulterm */

    p = buf = malloc(totlen);
    if (buf == NULL) return NULL;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p,E.row[j].chars,E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    *p = '\0';
    return buf;
}

/* Insert a character at the specified position in a row, moving the remaining
 * chars on the right if needed. */
void editor_row_insert_char(t_erow *row, int at, int c) {
    char *new_chars;
    if (at > row->size) {
        /* Pad the string with spaces if the insert location is outside the
         * current length by more than a single character. */
        int padlen = at-row->size;
        /* In the next line +2 means: new char and null term. */
        new_chars = realloc(row->chars,row->size+padlen+2);
        if (new_chars == NULL) {
            perror("Out of memory");
            exit(1);
        }
        row->chars = new_chars;
        memset(row->chars+row->size,' ',padlen);
        row->chars[row->size+padlen+1] = '\0';
        row->size += padlen+1;
    } else {
        /* If we are in the middle of the string just make space for 1 new
         * char plus the (already existing) null term. */
        new_chars = realloc(row->chars,row->size+2);
        if (new_chars == NULL) {
            perror("Out of memory");
            exit(1);
        }
        row->chars = new_chars;
        memmove(row->chars+at+1,row->chars+at,row->size-at+1);
        row->size++;
    }
    row->chars[at] = c;
    editor_update_row(row);
    E.dirty++;
}

/* Append the string 's' at the end of a row */
void editor_row_append_string(t_erow *row, char *s, size_t len) {
    char *new_chars = realloc(row->chars,row->size+len+1);
    if (new_chars == NULL) {
        perror("Out of memory");
        exit(1);
    }
    row->chars = new_chars;
    memcpy(row->chars+row->size,s,len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(row);
    E.dirty++;
}

/* Delete the character at offset 'at' from the specified row. */
void editor_row_del_char(t_erow *row, int at) {
    if (row->size <= at) return;
    /* Include null terminator in move (+1 for the null byte) */
    memmove(row->chars+at,row->chars+at+1,row->size-at+1);
    row->size--;
    editor_update_row(row);
    E.dirty++;
}

/* Insert the specified char at the current prompt position. */
void editor_insert_char(int c) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    t_erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    /* If the row where the cursor is currently located does not exist in our
     * logical representaion of the file, add enough empty rows as needed. */
    if (!row) {
        while(E.numrows <= filerow)
            editor_insert_row(E.numrows,"",0);
    }
    row = &E.row[filerow];
    editor_row_insert_char(row,filecol,c);
    if (E.cx == E.screencols-1)
        E.coloff++;
    else
        E.cx++;
    E.dirty++;
}

/* Inserting a newline is slightly complex as we have to handle inserting a
 * newline in the middle of a line, splitting the line as needed. */
void editor_insert_newline(void) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    t_erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    if (!row) {
        if (filerow == E.numrows) {
            editor_insert_row(filerow,"",0);
            goto fixcursor;
        }
        return;
    }
    /* If the cursor is over the current line size, we want to conceptually
     * think it's just over the last character. */
    if (filecol >= row->size) filecol = row->size;
    if (filecol == 0) {
        editor_insert_row(filerow,"",0);
    } else {
        /* We are in the middle of a line. Split it between two rows. */
        editor_insert_row(filerow+1,row->chars+filecol,row->size-filecol);
        row = &E.row[filerow];
        row->chars[filecol] = '\0';
        row->size = filecol;
        editor_update_row(row);
    }
fixcursor:
    if (E.cy == E.screenrows-1) {
        E.rowoff++;
    } else {
        E.cy++;
    }
    E.cx = 0;
    E.coloff = 0;
}

/* Delete the char at the current prompt position. */
void editor_del_char(void) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    t_erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    if (!row || (filecol == 0 && filerow == 0)) return;
    if (filecol == 0) {
        /* Handle the case of column 0, we need to move the current line
         * on the right of the previous one. */
        filecol = E.row[filerow-1].size;
        editor_row_append_string(&E.row[filerow-1],row->chars,row->size);
        editor_del_row(filerow);
        row = NULL;
        if (E.cy == 0)
            E.rowoff--;
        else
            E.cy--;
        E.cx = filecol;
        if (E.cx >= E.screencols) {
            int shift = (E.cx-E.screencols)+1;
            E.cx -= shift;
            E.coloff += shift;
        }
    } else {
        editor_row_del_char(row,filecol-1);
        if (E.cx == 0 && E.coloff)
            E.coloff--;
        else
            E.cx--;
    }
    if (row) editor_update_row(row);
    E.dirty++;
}

/* Load the specified program in the editor memory and returns 0 on success
 * or 1 on error. */
int editor_open(char *filename) {
    FILE *fp;

    E.dirty = 0;
    free(E.filename);
    size_t fnlen = strlen(filename)+1;
    E.filename = malloc(fnlen);
    if (E.filename == NULL) {
        perror("Out of memory");
        exit(1);
    }
    memcpy(E.filename,filename,fnlen);

    fp = fopen(filename,"r");
    if (!fp) {
        if (errno != ENOENT) {
            perror("Opening file");
            exit(1);
        }
        return 1;
    }

    /* Check if file appears to be binary by looking for null bytes in first 1KB */
    char probe[1024];
    size_t probe_len = fread(probe, 1, sizeof(probe), fp);
    for (size_t i = 0; i < probe_len; i++) {
        if (probe[i] == '\0') {
            fclose(fp);
            editor_set_status_msg("Cannot open binary file");
            return 1;
        }
    }
    rewind(fp);  /* Go back to start of file to read normally */

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while((linelen = getline(&line,&linecap,fp)) != -1) {
        while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
            line[--linelen] = '\0';
        editor_insert_row(E.numrows,line,linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
    return 0;
}

/* Save the current file on disk. Return 0 on success, 1 on error. */
int editor_save(void) {
    int len;
    char *buf = editor_rows_to_string(&len);
    if (buf == NULL) {
        editor_set_status_msg("Can't save! Out of memory");
        return 1;
    }
    int fd = open(E.filename,O_RDWR|O_CREAT,0644);
    if (fd == -1) goto writeerr;

    /* Use truncate + a single write(2) call in order to make saving
     * a bit safer, under the limits of what we can do in a small editor. */
    if (ftruncate(fd,len) == -1) goto writeerr;
    if (write(fd,buf,len) != len) goto writeerr;

    close(fd);
    free(buf);
    E.dirty = 0;
    editor_set_status_msg("%d bytes written on disk", len);
    return 0;

writeerr:
    free(buf);
    if (fd != -1) close(fd);
    editor_set_status_msg("Can't save! I/O error: %s",strerror(errno));
    return 1;
}

/* ============================= Terminal update ============================ */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL,0}

void ab_append(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b,ab->len+len);

    if (new == NULL) {
        /* Out of memory - attempt to restore terminal and exit cleanly */
        write(STDOUT_FILENO, "\x1b[2J", 4);  /* Clear screen */
        write(STDOUT_FILENO, "\x1b[H", 3);   /* Go home */
        perror("Out of memory during screen refresh");
        exit(1);
    }
    memcpy(new+ab->len,s,len);
    ab->b = new;
    ab->len += len;
}

void ab_free(struct abuf *ab) {
    free(ab->b);
}

/* This function writes the whole screen using VT100 escape characters
 * starting from the logical state of the editor in the global state 'E'. */
void editor_refresh_screen(void) {
    int y;
    t_erow *r;
    char buf[32];
    struct abuf ab = ABUF_INIT;

    ab_append(&ab,"\x1b[?25l",6); /* Hide cursor. */
    ab_append(&ab,"\x1b[H",3); /* Go home. */
    for (y = 0; y < E.screenrows; y++) {
        int filerow = E.rowoff+y;

        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows/3) {
                char welcome[80];
                int welcomelen = snprintf(welcome,sizeof(welcome),
                    "Kilo editor -- version %s\x1b[0K\r\n", KILO_VERSION);
                int padding = (E.screencols-welcomelen)/2;
                if (padding) {
                    ab_append(&ab,"~",1);
                    padding--;
                }
                while(padding--) ab_append(&ab," ",1);
                ab_append(&ab,welcome,welcomelen);
            } else {
                ab_append(&ab,"~\x1b[0K\r\n",7);
            }
            continue;
        }

        r = &E.row[filerow];

        int len = r->rsize - E.coloff;
        int current_color = -1;
        if (len > 0) {
            if (len > E.screencols) len = E.screencols;
            char *c = r->render+E.coloff;
            unsigned char *hl = r->hl+E.coloff;
            int j;
            for (j = 0; j < len; j++) {
                if (hl[j] == HL_NONPRINT) {
                    char sym;
                    ab_append(&ab,"\x1b[7m",4);
                    if (c[j] <= 26)
                        sym = '@'+c[j];
                    else
                        sym = '?';
                    ab_append(&ab,&sym,1);
                    ab_append(&ab,"\x1b[0m",4);
                } else if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        ab_append(&ab,"\x1b[39m",5);
                        current_color = -1;
                    }
                    ab_append(&ab,c+j,1);
                } else {
                    int color = editor_syntax_to_color(hl[j]);
                    if (color != current_color) {
                        char buf[16];
                        int clen = snprintf(buf,sizeof(buf),"\x1b[%dm",color);
                        current_color = color;
                        ab_append(&ab,buf,clen);
                    }
                    ab_append(&ab,c+j,1);
                }
            }
        }
        ab_append(&ab,"\x1b[39m",5);
        ab_append(&ab,"\x1b[0K",4);
        ab_append(&ab,"\r\n",2);
    }

    /* Create a two rows status. First row: */
    ab_append(&ab,"\x1b[0K",4);
    ab_append(&ab,"\x1b[7m",4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
        E.filename, E.numrows, E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus),
        "%d/%d",E.rowoff+E.cy+1,E.numrows);
    if (len > E.screencols) len = E.screencols;
    ab_append(&ab,status,len);
    while(len < E.screencols) {
        if (E.screencols - len == rlen) {
            ab_append(&ab,rstatus,rlen);
            break;
        } else {
            ab_append(&ab," ",1);
            len++;
        }
    }
    ab_append(&ab,"\x1b[0m\r\n",6);

    /* Second row depends on E.statusmsg and the status message update time. */
    ab_append(&ab,"\x1b[0K",4);
    int msglen = strlen(E.statusmsg);
    if (msglen && time(NULL)-E.statusmsg_time < 5)
        ab_append(&ab,E.statusmsg,msglen <= E.screencols ? msglen : E.screencols);

    /* Put cursor at its current position. Note that the horizontal position
     * at which the cursor is displayed may be different compared to 'E.cx'
     * because of TABs. */
    int j;
    int cx = 1;
    int filerow = E.rowoff+E.cy;
    t_erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    if (row) {
        for (j = E.coloff; j < (E.cx+E.coloff); j++) {
            if (j < row->size && row->chars[j] == TAB) cx += 7-((cx)%8);
            cx++;
        }
    }
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy+1,cx);
    ab_append(&ab,buf,strlen(buf));
    ab_append(&ab,"\x1b[?25h",6); /* Show cursor. */
    write(STDOUT_FILENO,ab.b,ab.len);
    ab_free(&ab);
}

/* Set an editor status message for the second line of the status, at the
 * end of the screen. */
void editor_set_status_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/* =============================== Find mode ================================ */

#define KILO_QUERY_LEN 256

void editor_find(int fd) {
    char query[KILO_QUERY_LEN+1] = {0};
    int qlen = 0;
    int last_match = -1; /* Last line where a match was found. -1 for none. */
    int find_next = 0; /* if 1 search next, if -1 search prev. */
    int saved_hl_line = -1;  /* No saved HL */
    char *saved_hl = NULL;

#define FIND_RESTORE_HL do { \
    if (saved_hl) { \
        memcpy(E.row[saved_hl_line].hl,saved_hl, E.row[saved_hl_line].rsize); \
        free(saved_hl); \
        saved_hl = NULL; \
    } \
} while (0)

    /* Save the cursor position in order to restore it later. */
    int saved_cx = E.cx, saved_cy = E.cy;
    int saved_coloff = E.coloff, saved_rowoff = E.rowoff;

    while(1) {
        editor_set_status_msg(
            "Search: %s (Use ESC/Arrows/Enter)", query);
        editor_refresh_screen();

        int c = editor_read_key(fd);
        if (c == DEL_KEY || c == CTRL_H || c == BACKSPACE) {
            if (qlen != 0) query[--qlen] = '\0';
            last_match = -1;
        } else if (c == ESC || c == ENTER) {
            if (c == ESC) {
                E.cx = saved_cx; E.cy = saved_cy;
                E.coloff = saved_coloff; E.rowoff = saved_rowoff;
            }
            FIND_RESTORE_HL;
            editor_set_status_msg("");
            return;
        } else if (c == ARROW_RIGHT || c == ARROW_DOWN) {
            find_next = 1;
        } else if (c == ARROW_LEFT || c == ARROW_UP) {
            find_next = -1;
        } else if (isprint(c)) {
            if (qlen < KILO_QUERY_LEN) {
                query[qlen++] = c;
                query[qlen] = '\0';
                last_match = -1;
            }
        }

        /* Search occurrence. */
        if (last_match == -1) find_next = 1;
        if (find_next) {
            char *match = NULL;
            int match_offset = 0;
            int i, current = last_match;

            for (i = 0; i < E.numrows; i++) {
                current += find_next;
                if (current == -1) current = E.numrows-1;
                else if (current == E.numrows) current = 0;
                match = strstr(E.row[current].render,query);
                if (match) {
                    match_offset = match-E.row[current].render;
                    break;
                }
            }
            find_next = 0;

            /* Highlight */
            FIND_RESTORE_HL;

            if (match) {
                t_erow *row = &E.row[current];
                last_match = current;
                if (row->hl) {
                    saved_hl_line = current;
                    saved_hl = malloc(row->rsize);
                    if (saved_hl) {
                        memcpy(saved_hl,row->hl,row->rsize);
                    }
                    memset(row->hl+match_offset,HL_MATCH,qlen);
                }
                E.cy = 0;
                E.cx = match_offset;
                E.rowoff = current;
                E.coloff = 0;
                /* Scroll horizontally as needed. */
                if (E.cx > E.screencols) {
                    int diff = E.cx - E.screencols;
                    E.cx -= diff;
                    E.coloff += diff;
                }
            }
        }
    }
}

/* ========================= Editor events handling  ======================== */

/* Handle cursor position change because arrow keys were pressed. */
void editor_move_cursor(int key) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    int rowlen;
    t_erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    switch(key) {
    case ARROW_LEFT:
        if (E.cx == 0) {
            if (E.coloff) {
                E.coloff--;
            } else {
                if (filerow > 0) {
                    E.cy--;
                    E.cx = E.row[filerow-1].size;
                    if (E.cx > E.screencols-1) {
                        E.coloff = E.cx-E.screencols+1;
                        E.cx = E.screencols-1;
                    }
                }
            }
        } else {
            E.cx -= 1;
        }
        break;
    case ARROW_RIGHT:
        if (row && filecol < row->size) {
            if (E.cx == E.screencols-1) {
                E.coloff++;
            } else {
                E.cx += 1;
            }
        } else if (row && filecol == row->size) {
            E.cx = 0;
            E.coloff = 0;
            if (E.cy == E.screenrows-1) {
                E.rowoff++;
            } else {
                E.cy += 1;
            }
        }
        break;
    case ARROW_UP:
        if (E.cy == 0) {
            if (E.rowoff) E.rowoff--;
        } else {
            E.cy -= 1;
        }
        break;
    case ARROW_DOWN:
        if (filerow < E.numrows) {
            if (E.cy == E.screenrows-1) {
                E.rowoff++;
            } else {
                E.cy += 1;
            }
        }
        break;
    }
    /* Fix cx if the current line has not enough chars. */
    filerow = E.rowoff+E.cy;
    filecol = E.coloff+E.cx;
    row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    rowlen = row ? row->size : 0;
    if (filecol > rowlen) {
        E.cx -= filecol-rowlen;
        if (E.cx < 0) {
            E.coloff += E.cx;
            E.cx = 0;
        }
    }
}

/* Process events arriving from the standard input, which is, the user
 * is typing stuff on the terminal. */
#define KILO_QUIT_TIMES 3
void editor_process_keypress(int fd) {
    /* When the file is modified, requires Ctrl-q to be pressed N times
     * before actually quitting. */
    static int quit_times = KILO_QUIT_TIMES;

    int c = editor_read_key(fd);
    switch(c) {
    case ENTER:         /* Enter */
        editor_insert_newline();
        break;
    case CTRL_C:        /* Ctrl-c */
        /* We ignore ctrl-c, it can't be so simple to lose the changes
         * to the edited file. */
        break;
    case CTRL_Q:        /* Ctrl-q */
        /* Quit if the file was already saved. */
        if (E.dirty && quit_times) {
            editor_set_status_msg("WARNING!!! File has unsaved changes. "
                "Press Ctrl-Q %d more times to quit.", quit_times);
            quit_times--;
            return;
        }
        exit(0);
        break;
    case CTRL_S:        /* Ctrl-s */
        editor_save();
        break;
    case CTRL_F:
        editor_find(fd);
        break;
    case BACKSPACE:     /* Backspace */
    case CTRL_H:        /* Ctrl-h */
    case DEL_KEY:
        editor_del_char();
        break;
    case PAGE_UP:
    case PAGE_DOWN:
        if (c == PAGE_UP && E.cy != 0)
            E.cy = 0;
        else if (c == PAGE_DOWN && E.cy != E.screenrows-1)
            E.cy = E.screenrows-1;
        {
        int times = E.screenrows;
        while(times--)
            editor_move_cursor(c == PAGE_UP ? ARROW_UP:
                                            ARROW_DOWN);
        }
        break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editor_move_cursor(c);
        break;
    case CTRL_L: /* ctrl+l, execute Lua command */
        if (E.L) {
            exec_lua_command(fd);
        } else {
            editor_set_status_msg("Lua not available");
        }
        break;
    case ESC:
        /* Nothing to do for ESC in this mode. */
        break;
    default:
        editor_insert_char(c);
        break;
    }

    quit_times = KILO_QUIT_TIMES; /* Reset it to the original value. */
}

int editor_file_was_modified(void) {
    return E.dirty;
}

void update_window_size(void) {
    if (get_window_size(STDIN_FILENO,STDOUT_FILENO,
                      &E.screenrows,&E.screencols) == -1) {
        /* If we can't get terminal size (e.g., non-interactive mode), use defaults */
        E.screenrows = 24;
        E.screencols = 80;
    }
    E.screenrows -= 2; /* Get room for status bar. */
}

void handle_sig_win_ch(int unused __attribute__((unused))) {
    /* Signal handler must be async-signal-safe.
     * Just set a flag and handle resize in main loop. */
    winsize_changed = 1;
}

void handle_windows_resize(void) {
    if (winsize_changed) {
        winsize_changed = 0;
        update_window_size();
        if (E.cy > E.screenrows) E.cy = E.screenrows - 1;
        if (E.cx > E.screencols) E.cx = E.screencols - 1;
    }
}

/* ======================= Async HTTP Implementation ======================= */

/* Callback for curl to write received data */
static size_t kilo_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct curl_response *resp = (struct curl_response *)userp;

    char *ptr = realloc(resp->data, resp->size + realsize + 1);
    if (!ptr) {
        /* Out of memory */
        return 0;
    }

    resp->data = ptr;
    memcpy(&(resp->data[resp->size]), contents, realsize);
    resp->size += realsize;
    resp->data[resp->size] = 0;

    return realsize;
}

/* Initialize curl globally */
static void init_curl(void) {
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = 1;
    }
}

/* Cleanup curl globally */
static void cleanup_curl(void) {
    if (curl_initialized) {
        curl_global_cleanup();
        curl_initialized = 0;
    }
}

/* Start an async HTTP request */
static int start_async_http_request(const char *url, const char *method,
                                     const char *body, const char **headers,
                                     int num_headers, const char *lua_callback) {
    if (num_pending >= MAX_ASYNC_REQUESTS) {
        editor_set_status_msg("Too many pending requests");
        return -1;
    }

    init_curl();

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_ASYNC_REQUESTS; i++) {
        if (!pending_requests[i]) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;

    /* Allocate request structure */
    async_http_request *req = malloc(sizeof(async_http_request));
    if (!req) return -1;

    memset(req, 0, sizeof(async_http_request));
    req->response.data = malloc(1);
    req->response.data[0] = '\0';
    req->response.size = 0;
    req->lua_callback = strdup(lua_callback);

    /* Initialize curl */
    req->easy_handle = curl_easy_init();
    if (!req->easy_handle) {
        free(req->response.data);
        free(req->lua_callback);
        free(req);
        return -1;
    }

    req->multi_handle = curl_multi_init();
    if (!req->multi_handle) {
        curl_easy_cleanup(req->easy_handle);
        free(req->response.data);
        free(req->lua_callback);
        free(req);
        return -1;
    }

    /* Set curl options */
    curl_easy_setopt(req->easy_handle, CURLOPT_URL, url);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEFUNCTION, kilo_curl_write_callback);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEDATA, &req->response);
    curl_easy_setopt(req->easy_handle, CURLOPT_ERRORBUFFER, req->error_buffer);
    curl_easy_setopt(req->easy_handle, CURLOPT_TIMEOUT, 60L);  /* Increase to 60 seconds */
    curl_easy_setopt(req->easy_handle, CURLOPT_CONNECTTIMEOUT, 10L);  /* Connection timeout */
    curl_easy_setopt(req->easy_handle, CURLOPT_FOLLOWLOCATION, 1L);

    /* SSL/TLS settings - use system CA bundle */
    curl_easy_setopt(req->easy_handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(req->easy_handle, CURLOPT_SSL_VERIFYHOST, 2L);

    /* Try common CA bundle locations */
    curl_easy_setopt(req->easy_handle, CURLOPT_CAINFO, "/etc/ssl/cert.pem");  /* macOS */

    /* Enable verbose output only if KILO_DEBUG is set */
    if (getenv("KILO_DEBUG")) {
        curl_easy_setopt(req->easy_handle, CURLOPT_VERBOSE, 1L);
    }

    /* Set method */
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDS, body);
        }
    }

    /* Set headers if provided */
    if (headers && num_headers > 0) {
        struct curl_slist *header_list = NULL;
        for (int i = 0; i < num_headers; i++) {
            header_list = curl_slist_append(header_list, headers[i]);
        }
        curl_easy_setopt(req->easy_handle, CURLOPT_HTTPHEADER, header_list);
    }

    /* Add to multi handle */
    curl_multi_add_handle(req->multi_handle, req->easy_handle);

    /* Store in pending requests */
    pending_requests[slot] = req;
    num_pending++;

    return slot;
}

/* Check and process async HTTP requests */
static void check_async_requests(void) {
    for (int i = 0; i < MAX_ASYNC_REQUESTS; i++) {
        async_http_request *req = pending_requests[i];
        if (!req) continue;

        /* Perform non-blocking work */
        int still_running = 0;
        curl_multi_perform(req->multi_handle, &still_running);

        if (still_running == 0) {
            /* Request completed - check for messages */
            int msgs_left = 0;
            CURLMsg *msg = NULL;

            while ((msg = curl_multi_info_read(req->multi_handle, &msgs_left))) {
                if (msg->msg == CURLMSG_DONE) {
                    /* Check if the transfer succeeded */
                    if (msg->data.result != CURLE_OK) {
                        req->failed = 1;
                        /* error_buffer should contain the error message */
                        if (req->error_buffer[0] == '\0') {
                            /* If error_buffer is empty, use curl_easy_strerror */
                            snprintf(req->error_buffer, CURL_ERROR_SIZE, "%s",
                                    curl_easy_strerror(msg->data.result));
                        }
                    }
                }
            }

            /* Get response code */
            long response_code = 0;
            curl_easy_getinfo(req->easy_handle, CURLINFO_RESPONSE_CODE, &response_code);

            /* Debug output for non-interactive mode */
            if (!E.rawmode) {
                fprintf(stderr, "HTTP request completed: status=%ld, response_size=%zu\n",
                        response_code, req->response.size);

                /* Check for curl errors */
                if (req->failed) {
                    fprintf(stderr, "CURL error: %s\n", req->error_buffer);
                }

                /* Show response preview if available */
                if (req->response.data && req->response.size > 0) {
                    size_t preview_len = req->response.size > 200 ? 200 : req->response.size;
                    fprintf(stderr, "Response preview: %.*s%s\n",
                            (int)preview_len, req->response.data,
                            req->response.size > 200 ? "..." : "");
                } else {
                    fprintf(stderr, "No response data received\n");
                }
            }

            /* Check for HTTP errors */
            if (response_code >= 400) {
                char errmsg[256];
                snprintf(errmsg, sizeof(errmsg), "HTTP error %ld", response_code);
                editor_set_status_msg("%s", errmsg);
                if (!E.rawmode) {
                    fprintf(stderr, "%s\n", errmsg);
                }
            }

            /* Call Lua callback with response */
            if (E.L && req->lua_callback) {
                lua_getglobal(E.L, req->lua_callback);
                if (lua_isfunction(E.L, -1)) {
                    if (req->response.data && req->response.size > 0) {
                        lua_pushstring(E.L, req->response.data);
                    } else {
                        lua_pushnil(E.L);
                    }

                    if (lua_pcall(E.L, 1, 0, 0) != LUA_OK) {
                        const char *err = lua_tostring(E.L, -1);
                        editor_set_status_msg("Lua callback error: %s", err);
                        /* Also print to stderr for non-interactive mode */
                        if (!E.rawmode) {
                            fprintf(stderr, "Lua callback error: %s\n", err);
                        }
                        lua_pop(E.L, 1);
                    }
                } else {
                    lua_pop(E.L, 1);
                }
            }

            /* Cleanup */
            curl_multi_remove_handle(req->multi_handle, req->easy_handle);
            curl_easy_cleanup(req->easy_handle);
            curl_multi_cleanup(req->multi_handle);
            free(req->response.data);
            free(req->lua_callback);
            free(req);

            pending_requests[i] = NULL;
            num_pending--;
        }
    }
}

/* ======================= Lua API bindings ================================ */

/* Lua API: kilo.status(message) - Set status message */
static int lua_kilo_status(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    editor_set_status_msg("%s", msg);
    return 0;
}

/* Lua API: kilo.get_line(row) - Get line content at row (0-indexed) */
static int lua_kilo_get_line(lua_State *L) {
    int row = luaL_checkinteger(L, 1);
    if (row < 0 || row >= E.numrows) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, E.row[row].chars);
    return 1;
}

/* Lua API: kilo.get_lines() - Get total number of lines */
static int lua_kilo_get_lines(lua_State *L) {
    lua_pushinteger(L, E.numrows);
    return 1;
}

/* Lua API: kilo.get_cursor() - Get cursor position (returns row, col) */
static int lua_kilo_get_cursor(lua_State *L) {
    lua_pushinteger(L, E.cy);
    lua_pushinteger(L, E.cx);
    return 2;
}

/* Lua API: kilo.insert_text(text) - Insert text at cursor */
static int lua_kilo_insert_text(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    for (const char *p = text; *p; p++) {
        editor_insert_char(*p);
    }
    return 0;
}

/* Lua API: kilo.get_filename() - Get current filename */
static int lua_kilo_get_filename(lua_State *L) {
    if (E.filename) {
        lua_pushstring(L, E.filename);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* Lua API: kilo.async_http(url, method, body, headers, callback) - Async HTTP request */
static int lua_kilo_async_http(lua_State *L) {
    const char *url = luaL_checkstring(L, 1);
    const char *method = luaL_optstring(L, 2, "GET");
    const char *body = luaL_optstring(L, 3, NULL);

    /* Parse headers table (optional) */
    const char **headers = NULL;
    int num_headers = 0;

    if (lua_istable(L, 4)) {
        /* Count headers */
        lua_pushnil(L);
        while (lua_next(L, 4) != 0) {
            num_headers++;
            lua_pop(L, 1);
        }

        /* Allocate and populate headers array */
        if (num_headers > 0) {
            headers = malloc(sizeof(char*) * num_headers);
            int i = 0;
            lua_pushnil(L);
            while (lua_next(L, 4) != 0) {
                headers[i++] = strdup(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
    }

    const char *callback = luaL_checkstring(L, 5);

    /* Start async request */
    int req_id = start_async_http_request(url, method, body, headers, num_headers, callback);

    /* Free headers */
    if (headers) {
        for (int i = 0; i < num_headers; i++) {
            free((void*)headers[i]);
        }
        free(headers);
    }

    if (req_id >= 0) {
        editor_set_status_msg("HTTP request sent (async)...");
        lua_pushinteger(L, req_id);
        return 1;
    } else {
        lua_pushnil(L);
        return 1;
    }
}

/* Initialize Lua API */
static void init_lua_api(lua_State *L) {
    /* Create kilo table */
    lua_newtable(L);

    /* Register functions */
    lua_pushcfunction(L, lua_kilo_status);
    lua_setfield(L, -2, "status");

    lua_pushcfunction(L, lua_kilo_get_line);
    lua_setfield(L, -2, "get_line");

    lua_pushcfunction(L, lua_kilo_get_lines);
    lua_setfield(L, -2, "get_lines");

    lua_pushcfunction(L, lua_kilo_get_cursor);
    lua_setfield(L, -2, "get_cursor");

    lua_pushcfunction(L, lua_kilo_insert_text);
    lua_setfield(L, -2, "insert_text");

    lua_pushcfunction(L, lua_kilo_get_filename);
    lua_setfield(L, -2, "get_filename");

    lua_pushcfunction(L, lua_kilo_async_http);
    lua_setfield(L, -2, "async_http");

    /* Set as global 'kilo' */
    lua_setglobal(L, "kilo");
}

/* Load init.lua: try .kilo/init.lua (local) first, then ~/.kilo/init.lua (global) */
static void load_lua_init(lua_State *L) {
    char init_path[1024];

    /* Try local .kilo/init.lua first (project-specific) */
    snprintf(init_path, sizeof(init_path), ".kilo/init.lua");
    if (access(init_path, R_OK) == 0) {
        if (luaL_dofile(L, init_path) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            editor_set_status_msg("Lua init error (.kilo): %s", err);
            lua_pop(L, 1);
        }
        return; /* Local config loaded, don't load global */
    }

    /* Fall back to global ~/.kilo/init.lua */
    char *home = getenv("HOME");
    if (!home) return;

    snprintf(init_path, sizeof(init_path), "%s/.kilo/init.lua", home);
    if (access(init_path, R_OK) == 0) {
        if (luaL_dofile(L, init_path) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            editor_set_status_msg("Lua init error (~/.kilo): %s", err);
            lua_pop(L, 1);
        }
    }
}

/* Execute Lua command from user input */
static void exec_lua_command(int fd) {
    char cmd[KILO_QUERY_LEN+1] = {0};
    int cmdlen = 0;

    while(1) {
        editor_set_status_msg("Lua: %s", cmd);
        editor_refresh_screen();

        int c = editor_read_key(fd);
        if (c == DEL_KEY || c == CTRL_H || c == BACKSPACE) {
            if (cmdlen != 0) cmd[--cmdlen] = '\0';
        } else if (c == ESC) {
            editor_set_status_msg("");
            return;
        } else if (c == ENTER) {
            if (cmdlen > 0) {
                /* Execute Lua command */
                if (luaL_dostring(E.L, cmd) != LUA_OK) {
                    const char *err = lua_tostring(E.L, -1);
                    editor_set_status_msg("Lua error: %s", err);
                    lua_pop(E.L, 1);
                } else {
                    editor_set_status_msg("Lua: OK");
                }
            }
            return;
        } else if (isprint(c)) {
            if (cmdlen < KILO_QUERY_LEN) {
                cmd[cmdlen++] = c;
                cmd[cmdlen] = '\0';
            }
        }
    }
}

void init_editor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.syntax = NULL;
    update_window_size();
    signal(SIGWINCH, handle_sig_win_ch);

    /* Initialize Lua */
    E.L = luaL_newstate();
    if (E.L) {
        luaL_openlibs(E.L);
        init_lua_api(E.L);
        load_lua_init(E.L);
    }
}

/* Run AI command in non-interactive mode */
static int run_ai_command(char *filename, const char *command) {
    init_editor();

    /* Load the file */
    editor_select_syntax_highlight(filename);
    if (editor_open(filename) != 0 && errno != ENOENT) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return 1;
    }

    /* Check if Lua was initialized */
    if (!E.L) {
        fprintf(stderr, "Error: Lua not initialized\n");
        return 1;
    }

    /* Record initial dirty state and row count */
    int initial_dirty = E.dirty;
    int initial_rows = E.numrows;

    /* Record initial number of pending requests */
    int initial_pending = num_pending;

    /* Call the Lua command */
    lua_getglobal(E.L, command);
    if (!lua_isfunction(E.L, -1)) {
        fprintf(stderr, "Error: Lua function '%s' not found\n", command);
        fprintf(stderr, "Make sure .kilo/init.lua or ~/.kilo/init.lua defines this function\n");
        return 1;
    }

    if (lua_pcall(E.L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "Error running %s: %s\n", command, lua_tostring(E.L, -1));
        return 1;
    }

    /* Check if a request was initiated */
    if (num_pending <= initial_pending) {
        fprintf(stderr, "Error: No async request was initiated\n");
        fprintf(stderr, "Check that OPENAI_API_KEY is set and the function makes an HTTP request\n");
        return 1;
    }

    /* Wait for all async requests to complete (max 60 seconds) */
    fprintf(stderr, "Waiting for AI response...\n");
    int timeout = 60000; /* 60 seconds in iterations (assuming ~1ms per iteration) */
    while (num_pending > 0 && timeout-- > 0) {
        check_async_requests();
        usleep(1000); /* 1ms */
    }

    if (num_pending > 0) {
        fprintf(stderr, "Error: AI command timed out\n");
        return 1;
    }

    /* Check if anything was actually inserted */
    if (E.dirty == initial_dirty && E.numrows == initial_rows) {
        fprintf(stderr, "Warning: No content was inserted. Possible issues:\n");
        fprintf(stderr, "  - API request failed (check API key)\n");
        fprintf(stderr, "  - Response parsing failed (check model name)\n");
        fprintf(stderr, "  - Lua callback error (check .kilo/init.lua)\n");
        fprintf(stderr, "Status: %s\n", E.statusmsg);
        return 1;
    }

    fprintf(stderr, "Content inserted: %d rows, dirty=%d\n", E.numrows, E.dirty);

    /* For --complete, save the file */
    if (strcmp(command, "ai_complete") == 0) {
        if (editor_save() != 0) {
            fprintf(stderr, "Error: Failed to save file\n");
            return 1;
        }
        fprintf(stderr, "Completion saved to %s\n", filename);
    } else if (strcmp(command, "ai_explain") == 0) {
        /* For --explain, print the buffer content (explanation was inserted) */
        for (int i = 0; i < E.numrows; i++) {
            printf("%s\n", E.row[i].chars);
        }
    }

    return 0;
}

static void print_usage(void) {
    printf("Usage: kilo [options] <filename>\n");
    printf("\nOptions:\n");
    printf("  --help              Show this help message\n");
    printf("  --complete <file>   Run AI completion on file and save result\n");
    printf("  --explain <file>    Run AI explanation on file and print to stdout\n");
    printf("\nInteractive mode (default):\n");
    printf("  kilo <filename>     Open file in interactive editor\n");
    printf("\nKeybindings in interactive mode:\n");
    printf("  Ctrl-S    Save file\n");
    printf("  Ctrl-Q    Quit\n");
    printf("  Ctrl-F    Find\n");
    printf("  Ctrl-L    Execute Lua command\n");
    printf("\nAI commands require OPENAI_API_KEY environment variable\n");
    printf("and .kilo/init.lua or ~/.kilo/init.lua configuration.\n");
}

int main(int argc, char **argv) {
    /* Register cleanup handler early to ensure terminal is always restored */
    atexit(editor_atexit);

    /* Parse command-line arguments */
    if (argc < 2) {
        print_usage();
        exit(1);
    }

    /* Check for --help flag */
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        exit(0);
    }

    /* Check for --complete flag */
    if (strcmp(argv[1], "--complete") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Error: --complete requires a filename\n");
            print_usage();
            exit(1);
        }
        int result = run_ai_command(argv[2], "ai_complete");
        exit(result);
    }

    /* Check for --explain flag */
    if (strcmp(argv[1], "--explain") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Error: --explain requires a filename\n");
            print_usage();
            exit(1);
        }
        int result = run_ai_command(argv[2], "ai_explain");
        exit(result);
    }

    /* Default: interactive mode */
    if (argc != 2) {
        fprintf(stderr, "Error: Too many arguments\n");
        print_usage();
        exit(1);
    }

    init_editor();
    editor_select_syntax_highlight(argv[1]);
    editor_open(argv[1]);
    enable_raw_mode(STDIN_FILENO);
    editor_set_status_msg(
        "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-L = lua");
    while(1) {
        handle_windows_resize();
        check_async_requests();  /* Process any pending async HTTP requests */
        editor_refresh_screen();
        editor_process_keypress(STDIN_FILENO);
    }
    return 0;
}
