#include "loki/version.h"

/* 
loki is based on kilo -- A very simple editor in less than 1000 
lines of code (as counted by "cloc"). Does not depend on libcurses,
directly emits VT100 escapes on the terminal.

Copyright (c) 2016, Salvatore Sanfilippo <antirez at gmail dot com>

see LICENSE.
*/


#ifdef __linux__
#define _POSIX_C_SOURCE 200809L
#endif

#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <strings.h>

#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <signal.h>

#include "loki/editor.h"
#include "loki/lua.h"

/* Lua scripting support (from Homebrew) */
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#if LUA_VERSION_NUM < 502 && !defined(lua_rawlen)
#define lua_rawlen(L, idx) lua_objlen(L, (idx))
#endif

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
    char singleline_comment_start[4];   /* Increased to support longer comment syntax */
    char multiline_comment_start[6];    /* Increased for Lua's --[[ */
    char multiline_comment_end[6];      /* Increased for potential longer delimiters */
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

#define KILO_QUERY_LEN 256

#define STATUS_ROWS 2

#define LUA_REPL_HISTORY_MAX 64
#define LUA_REPL_LOG_MAX 128
#define LUA_REPL_OUTPUT_ROWS 2
#define LUA_REPL_TOTAL_ROWS (LUA_REPL_OUTPUT_ROWS + 1)
#define LUA_REPL_PROMPT ">> "

typedef struct t_lua_repl {
    char input[KILO_QUERY_LEN+1];
    int input_len;
    int active;
    int history_len;
    int history_index;
    char *history[LUA_REPL_HISTORY_MAX];
    int log_len;
    char *log[LUA_REPL_LOG_MAX];
} t_lua_repl;

struct t_editor_config {
    int cx,cy;  /* Cursor x and y position in characters */
    int rowoff;     /* Offset of row displayed. */
    int coloff;     /* Offset of column displayed. */
    int screenrows; /* Number of rows that we can show */
    int screencols; /* Number of cols that we can show */
    int screenrows_total; /* Rows available after status bars (before REPL) */
    int numrows;    /* Number of rows */
    int rawmode;    /* Is terminal raw mode enabled? */
    t_erow *row;      /* Rows */
    int dirty;      /* File modified but not saved. */
    char *filename; /* Currently open filename */
    char statusmsg[80];
    time_t statusmsg_time;
    struct t_editor_syntax *syntax;    /* Current syntax highlight, or NULL. */
    lua_State *L;   /* Lua interpreter state */
    int word_wrap;  /* Word wrap enabled flag */
    int sel_active; /* Selection active flag */
    int sel_start_x, sel_start_y; /* Selection start position */
    int sel_end_x, sel_end_y;     /* Selection end position */
    t_lua_repl repl; /* Embedded Lua REPL state */
    t_hlcolor colors[9]; /* Syntax highlight colors: indexed by HL_* constants */
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
    struct curl_slist *header_list;  /* HTTP headers for curl */
    int completed;
    int failed;
    char error_buffer[CURL_ERROR_SIZE];
} async_http_request;

#define MAX_ASYNC_REQUESTS 10
#define MAX_HTTP_RESPONSE_SIZE (10 * 1024 * 1024)  /* 10MB limit */
static async_http_request *pending_requests[MAX_ASYNC_REQUESTS] = {0};
static int num_pending = 0;

/* libcurl global initialization flag */
static int curl_initialized = 0;

struct abuf;

static void lua_repl_init(t_lua_repl *repl);
static void lua_repl_free(t_lua_repl *repl);
static void lua_repl_handle_keypress(int key);
static void lua_repl_render(struct abuf *ab);
static void lua_repl_reset_log(t_lua_repl *repl);
static int lua_repl_handle_builtin(const char *cmd, size_t len);
static void editor_update_repl_layout(void);
static void lua_repl_emit_registered_help(void);
static int lua_loki_repl_register(lua_State *L);

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
        CTRL_W = 23,        /* Ctrl-w */
        ESC = 27,           /* Escape */
        BACKSPACE =  127,   /* Backspace */
        /* The following are just soft codes, not really reported by the
         * terminal directly. */
        ARROW_LEFT = 1000,
        ARROW_RIGHT,
        ARROW_UP,
        ARROW_DOWN,
        SHIFT_ARROW_LEFT,
        SHIFT_ARROW_RIGHT,
        SHIFT_ARROW_UP,
        SHIFT_ARROW_DOWN,
        DEL_KEY,
        HOME_KEY,
        END_KEY,
        PAGE_UP,
        PAGE_DOWN
};

void editor_set_status_msg(const char *fmt, ...);
static void exec_lua_command(int fd);
static void cleanup_dynamic_languages(void);

/* Check if position is within selection */
int is_selected(int row, int col) {
    if (!E.sel_active) return 0;

    int start_y = E.sel_start_y;
    int start_x = E.sel_start_x;
    int end_y = E.sel_end_y;
    int end_x = E.sel_end_x;

    /* Ensure start comes before end */
    if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
        int tmp;
        tmp = start_y; start_y = end_y; end_y = tmp;
        tmp = start_x; start_x = end_x; end_x = tmp;
    }

    /* Check if row is in range */
    if (row < start_y || row > end_y) return 0;

    /* Single line selection */
    if (start_y == end_y) {
        return col >= start_x && col < end_x;
    }

    /* Multi-line selection */
    if (row == start_y) {
        return col >= start_x;
    } else if (row == end_y) {
        return col < end_x;
    } else {
        return 1; /* Entire line selected */
    }
}

/* Base64 encoding table */
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64 encode a string for OSC 52 clipboard */
char *base64_encode(const char *input, size_t len) {
    size_t output_len = 4 * ((len + 2) / 3);
    char *output = malloc(output_len + 1);
    if (!output) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < len;) {
        uint32_t octet_a = i < len ? (unsigned char)input[i++] : 0;
        uint32_t octet_b = i < len ? (unsigned char)input[i++] : 0;
        uint32_t octet_c = i < len ? (unsigned char)input[i++] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        output[j++] = base64_table[(triple >> 18) & 0x3F];
        output[j++] = base64_table[(triple >> 12) & 0x3F];
        output[j++] = base64_table[(triple >> 6) & 0x3F];
        output[j++] = base64_table[triple & 0x3F];
    }

    /* Add padding */
    for (i = 0; i < (3 - len % 3) % 3; i++)
        output[output_len - 1 - i] = '=';

    output[output_len] = '\0';
    return output;
}

/* Copy selected text to clipboard using OSC 52 */
void copy_selection_to_clipboard(void) {
    if (!E.sel_active) {
        editor_set_status_msg("No selection");
        return;
    }

    /* Ensure start comes before end */
    int start_y = E.sel_start_y;
    int start_x = E.sel_start_x;
    int end_y = E.sel_end_y;
    int end_x = E.sel_end_x;

    if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
        int tmp;
        tmp = start_y; start_y = end_y; end_y = tmp;
        tmp = start_x; start_x = end_x; end_x = tmp;
    }

    /* Build selected text */
    char *text = NULL;
    size_t text_len = 0;
    size_t text_capacity = 1024;
    text = malloc(text_capacity);
    if (!text) return;

    for (int y = start_y; y <= end_y && y < E.numrows; y++) {
        int x_start = (y == start_y) ? start_x : 0;
        int x_end = (y == end_y) ? end_x : E.row[y].size;
        if (x_end > E.row[y].size) x_end = E.row[y].size;

        int len = x_end - x_start;
        if (len > 0) {
            while (text_len + len + 2 > text_capacity) {
                text_capacity *= 2;
                char *new_text = realloc(text, text_capacity);
                if (!new_text) { free(text); return; }
                text = new_text;
            }
            memcpy(text + text_len, E.row[y].chars + x_start, len);
            text_len += len;
        }
        if (y < end_y) {
            text[text_len++] = '\n';
        }
    }
    text[text_len] = '\0';

    /* Base64 encode */
    char *encoded = base64_encode(text, text_len);
    free(text);
    if (!encoded) return;

    /* Send OSC 52 sequence: ESC]52;c;<base64>BEL */
    printf("\033]52;c;%s\007", encoded);
    fflush(stdout);
    free(encoded);

    editor_set_status_msg("Copied %d bytes to clipboard", (int)text_len);
    E.sel_active = 0;  /* Clear selection after copy */
}
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

/* Python extensions */
char *Python_HL_extensions[] = {".py",".pyw",NULL};

/* Lua extensions */
char *Lua_HL_extensions[] = {".lua",NULL};

/* Cython extensions */
char *Cython_HL_extensions[] = {".pyx",".pxd",".pxi",NULL};

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

/* Dynamic language registry for user-defined languages */
static struct t_editor_syntax **HLDB_dynamic = NULL;
static int HLDB_dynamic_count = 0;

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
    lua_repl_free(&E.repl);
    if (E.L) {
        lua_close(E.L);
        E.L = NULL;
    }
    cleanup_curl();
    cleanup_dynamic_languages();
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
    char c, seq[6];
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
                    } else if (seq[2] == ';') {
                        /* ESC[1;2X for Shift+Arrow */
                        if (read(fd,seq+3,1) == 0) return ESC;
                        if (read(fd,seq+4,1) == 0) return ESC;
                        if (seq[1] == '1' && seq[3] == '2') {
                            switch(seq[4]) {
                            case 'A': return SHIFT_ARROW_UP;
                            case 'B': return SHIFT_ARROW_DOWN;
                            case 'C': return SHIFT_ARROW_RIGHT;
                            case 'D': return SHIFT_ARROW_LEFT;
                            }
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

/* Map human-readable style names to HL_* constants */
static int hl_name_to_code(const char *name) {
    if (name == NULL) return -1;
    if (strcasecmp(name, "normal") == 0) return HL_NORMAL;
    if (strcasecmp(name, "nonprint") == 0) return HL_NONPRINT;
    if (strcasecmp(name, "comment") == 0) return HL_COMMENT;
    if (strcasecmp(name, "mlcomment") == 0) return HL_MLCOMMENT;
    if (strcasecmp(name, "keyword1") == 0) return HL_KEYWORD1;
    if (strcasecmp(name, "keyword2") == 0) return HL_KEYWORD2;
    if (strcasecmp(name, "string") == 0) return HL_STRING;
    if (strcasecmp(name, "number") == 0) return HL_NUMBER;
    if (strcasecmp(name, "match") == 0) return HL_MATCH;
    return -1;
}

/* Attempt to delegate highlighting of a row to Lua. Returns 1 if handled. */
static int lua_apply_span_table(t_erow *row, int table_index) {
    if (!lua_istable(E.L, table_index)) return 0;

    int applied = 0;
    size_t entries = lua_rawlen(E.L, table_index);

    for (size_t i = 1; i <= entries; i++) {
        lua_rawgeti(E.L, table_index, (lua_Integer)i);
        if (lua_type(E.L, -1) == LUA_TTABLE) {
            int start = 0;
            int stop = 0;
            int length = 0;
            int style = -1;

            lua_getfield(E.L, -1, "start");
            if (lua_isnumber(E.L, -1)) start = (int)lua_tointeger(E.L, -1);
            lua_pop(E.L, 1);

            lua_getfield(E.L, -1, "stop");
            if (lua_isnumber(E.L, -1)) stop = (int)lua_tointeger(E.L, -1);
            lua_pop(E.L, 1);

            lua_getfield(E.L, -1, "end");
            if (lua_isnumber(E.L, -1)) stop = (int)lua_tointeger(E.L, -1);
            lua_pop(E.L, 1);

            lua_getfield(E.L, -1, "length");
            if (lua_isnumber(E.L, -1)) length = (int)lua_tointeger(E.L, -1);
            lua_pop(E.L, 1);

            lua_getfield(E.L, -1, "style");
            if (lua_isstring(E.L, -1)) {
                style = hl_name_to_code(lua_tostring(E.L, -1));
            } else if (lua_isnumber(E.L, -1)) {
                style = (int)lua_tointeger(E.L, -1);
            }
            lua_pop(E.L, 1);

            if (style < 0) {
                lua_getfield(E.L, -1, "type");
                if (lua_isstring(E.L, -1)) {
                    style = hl_name_to_code(lua_tostring(E.L, -1));
                } else if (lua_isnumber(E.L, -1)) {
                    style = (int)lua_tointeger(E.L, -1);
                }
                lua_pop(E.L, 1);
            }

            if (start <= 0) start = 1;
            if (length > 0 && stop <= 0) stop = start + length - 1;
            if (stop <= 0) stop = start;

            if (style >= 0 && row->rsize > 0) {
                if (start > stop) {
                    int tmp = start;
                    start = stop;
                    stop = tmp;
                }
                if (start < 1) start = 1;
                if (stop > row->rsize) stop = row->rsize;
                for (int pos = start - 1; pos < stop && pos < row->rsize; pos++) {
                    row->hl[pos] = style;
                }
                applied = 1;
            } else if (style >= 0 && row->rsize == 0) {
                applied = 1;
            }
        }
        lua_pop(E.L, 1);
    }

    return applied;
}

static void lua_apply_highlight_row(t_erow *row, int default_ran) {
    if (!E.L || row == NULL || row->render == NULL) return;
    int top = lua_gettop(E.L);

    lua_getglobal(E.L, "loki");
    if (!lua_istable(E.L, -1)) {
        lua_settop(E.L, top);
        return;
    }

    lua_getfield(E.L, -1, "highlight_row");
    if (!lua_isfunction(E.L, -1)) {
        lua_settop(E.L, top);
        return;
    }

    lua_pushinteger(E.L, row->idx);
    lua_pushlstring(E.L, row->chars ? row->chars : "", (size_t)row->size);
    lua_pushlstring(E.L, row->render ? row->render : "", (size_t)row->rsize);
    if (E.syntax) {
        lua_pushinteger(E.L, E.syntax->type);
    } else {
        lua_pushnil(E.L);
    }
    lua_pushboolean(E.L, default_ran);

    if (lua_pcall(E.L, 5, 1, 0) != LUA_OK) {
        const char *err = lua_tostring(E.L, -1);
        editor_set_status_msg("Lua highlight error: %s", err ? err : "unknown");
        lua_settop(E.L, top);
        return;
    }

    if (!lua_istable(E.L, -1)) {
        lua_settop(E.L, top);
        return;
    }

    int table_index = lua_gettop(E.L);
    int replace = 0;

    lua_getfield(E.L, table_index, "replace");
    if (lua_isboolean(E.L, -1)) replace = lua_toboolean(E.L, -1);
    lua_pop(E.L, 1);

    int spans_index = table_index;
    int has_spans_field = 0;

    lua_getfield(E.L, table_index, "spans");
    if (lua_istable(E.L, -1)) {
        spans_index = lua_gettop(E.L);
        has_spans_field = 1;
    } else {
        lua_pop(E.L, 1);
    }

    if (replace) {
        memset(row->hl, HL_NORMAL, row->rsize);
    }

    lua_apply_span_table(row, spans_index);

    if (has_spans_field) {
        lua_pop(E.L, 1);
    }

    lua_settop(E.L, top);
}

/* Set every byte of row->hl (that corresponds to every character in the line)
 * to the right syntax highlight type (HL_* defines). */
void editor_update_syntax(t_erow *row) {
    unsigned char *new_hl = realloc(row->hl,row->rsize);
    if (new_hl == NULL) return; /* Out of memory, keep old highlighting */
    row->hl = new_hl;
    memset(row->hl,HL_NORMAL,row->rsize);

    int default_ran = 0;

    if (E.syntax != NULL) {
        if (E.syntax->type == HL_TYPE_MARKDOWN) {
            editor_update_syntax_markdown(row);
            default_ran = 1;
        } else {
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
                    break;
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

            default_ran = 1;
        }
    }

    lua_apply_highlight_row(row, default_ran);

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

/* Format RGB color escape sequence for syntax highlighting.
 * Uses true color (24-bit) escape codes: ESC[38;2;R;G;Bm
 * Returns the length of the formatted string. */
int editor_format_color(int hl, char *buf, size_t bufsize) {
    if (hl < 0 || hl >= 9) hl = 0;  /* Default to HL_NORMAL */
    t_hlcolor *color = &E.colors[hl];
    return snprintf(buf, bufsize, "\x1b[38;2;%d;%d;%dm",
                    color->r, color->g, color->b);
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

    /* Also check dynamic language registry */
    for (int j = 0; j < HLDB_dynamic_count; j++) {
        struct t_editor_syntax *s = HLDB_dynamic[j];
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

/* Free a single dynamically allocated language definition */
static void free_dynamic_language(struct t_editor_syntax *lang) {
    if (!lang) return;

    /* Free filematch array */
    if (lang->filematch) {
        for (int i = 0; lang->filematch[i]; i++) {
            free(lang->filematch[i]);
        }
        free(lang->filematch);
    }

    /* Free keywords array */
    if (lang->keywords) {
        for (int i = 0; lang->keywords[i]; i++) {
            free(lang->keywords[i]);
        }
        free(lang->keywords);
    }

    /* Free separators string */
    if (lang->separators) {
        free(lang->separators);
    }

    free(lang);
}

/* Free all dynamically allocated languages (called at exit) */
static void cleanup_dynamic_languages(void) {
    for (int i = 0; i < HLDB_dynamic_count; i++) {
        free_dynamic_language(HLDB_dynamic[i]);
    }
    free(HLDB_dynamic);
    HLDB_dynamic = NULL;
    HLDB_dynamic_count = 0;
}

/* Add a new language definition dynamically
 * Returns 0 on success, -1 on error */
static int add_dynamic_language(struct t_editor_syntax *lang) {
    if (!lang) return -1;

    /* Grow the dynamic array */
    struct t_editor_syntax **new_array = realloc(HLDB_dynamic,
        sizeof(struct t_editor_syntax*) * (HLDB_dynamic_count + 1));
    if (!new_array) {
        return -1;  /* Allocation failed */
    }

    HLDB_dynamic = new_array;
    HLDB_dynamic[HLDB_dynamic_count] = lang;
    HLDB_dynamic_count++;

    return 0;
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
        printf("Some line of the edited file is too long for loki\n");
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
                    "Kilo editor -- version %s\x1b[0K\r\n", LOKI_VERSION);
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

        /* Word wrap: clamp to screen width and find word boundary */
        if (E.word_wrap && len > E.screencols && r->cb_lang == CB_LANG_NONE) {
            len = E.screencols;
            /* Find last space/separator to break at word boundary */
            int last_space = -1;
            for (int k = 0; k < len; k++) {
                if (isspace(r->render[E.coloff + k])) {
                    last_space = k;
                }
            }
            if (last_space > 0 && last_space > len / 2) {
                len = last_space + 1; /* Include the space */
            }
        }

        if (len > 0) {
            if (len > E.screencols) len = E.screencols;
            char *c = r->render+E.coloff;
            unsigned char *hl = r->hl+E.coloff;
            int j;
            for (j = 0; j < len; j++) {
                int selected = is_selected(filerow, E.coloff + j);

                /* Apply selection background */
                if (selected) {
                    ab_append(&ab,"\x1b[7m",4); /* Reverse video */
                }

                if (hl[j] == HL_NONPRINT) {
                    char sym;
                    if (!selected) ab_append(&ab,"\x1b[7m",4);
                    if (c[j] <= 26)
                        sym = '@'+c[j];
                    else
                        sym = '?';
                    ab_append(&ab,&sym,1);
                    ab_append(&ab,"\x1b[0m",4);
                    if (current_color != -1) {
                        char buf[32];
                        int clen = editor_format_color(current_color, buf, sizeof(buf));
                        ab_append(&ab,buf,clen);
                    }
                } else if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        ab_append(&ab,"\x1b[39m",5);
                        current_color = -1;
                    }
                    ab_append(&ab,c+j,1);
                    if (selected) {
                        ab_append(&ab,"\x1b[0m",4); /* Reset */
                    }
                } else {
                    int color = hl[j];
                    if (color != current_color) {
                        char buf[32];
                        int clen = editor_format_color(color, buf, sizeof(buf));
                        current_color = color;
                        ab_append(&ab,buf,clen);
                    }
                    ab_append(&ab,c+j,1);
                    if (selected) {
                        ab_append(&ab,"\x1b[0m",4); /* Reset */
                        if (current_color != -1) {
                            char buf[32];
                            int clen = editor_format_color(current_color, buf, sizeof(buf));
                            ab_append(&ab,buf,clen);
                        }
                    }
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

    if (E.repl.active) lua_repl_render(&ab);

    /* Put cursor at its current position. Note that the horizontal position
     * at which the cursor is displayed may be different compared to 'E.cx'
     * because of TABs. */
    int cursor_row = 1;
    int cursor_col = 1;

    if (E.repl.active) {
        int prompt_len = (int)strlen(LUA_REPL_PROMPT);
        int visible = E.repl.input_len;
        if (prompt_len + visible >= E.screencols) {
            visible = E.screencols > prompt_len ? E.screencols - prompt_len : 0;
        }
        cursor_row = E.screenrows + STATUS_ROWS + LUA_REPL_OUTPUT_ROWS + 1;
        cursor_col = prompt_len + visible + 1;
        if (cursor_col < 1) cursor_col = 1;
        if (cursor_col > E.screencols) cursor_col = E.screencols;
    } else {
        int cx = 1;
        int filerow = E.rowoff+E.cy;
        t_erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
        if (row) {
            for (int j = E.coloff; j < (E.cx+E.coloff); j++) {
                if (j < row->size && row->chars[j] == TAB)
                    cx += 7-((cx)%8);
                cx++;
            }
        }
        cursor_row = E.cy + 1;
        cursor_col = cx;
        if (cursor_col > E.screencols) cursor_col = E.screencols;
    }

    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",cursor_row,cursor_col);
    ab_append(&ab,buf,strlen(buf));
    ab_append(&ab,"\x1b[?25h",6); /* Show cursor. */
    write(STDOUT_FILENO,ab.b,ab.len);
    ab_free(&ab);
}

static void lua_repl_render(struct abuf *ab) {
    if (!E.repl.active) return;

    ab_append(ab,"\r\n",2);

    int start = E.repl.log_len - LUA_REPL_OUTPUT_ROWS;
    if (start < 0) start = 0;
    int rendered = 0;

    for (int i = start; i < E.repl.log_len; i++) {
        const char *line = E.repl.log[i] ? E.repl.log[i] : "";
        int take = (int)strlen(line);
        if (take > E.screencols) take = E.screencols;
        ab_append(ab,"\x1b[0K",4);
        if (take > 0) ab_append(ab,line,take);
        ab_append(ab,"\r\n",2);
        rendered++;
    }

    while (rendered < LUA_REPL_OUTPUT_ROWS) {
        ab_append(ab,"\x1b[0K\r\n",6);
        rendered++;
    }

    ab_append(ab,"\x1b[0K",4);
    ab_append(ab,LUA_REPL_PROMPT,strlen(LUA_REPL_PROMPT));

    int prompt_len = (int)strlen(LUA_REPL_PROMPT);
    int available = E.screencols - prompt_len;
    if (available < 0) available = 0;
    if (available > 0 && E.repl.input_len > 0) {
        int shown = E.repl.input_len;
        if (shown > available) shown = available;
        ab_append(ab,E.repl.input,shown);
    }
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

    if (E.repl.active) {
        lua_repl_handle_keypress(c);
        return;
    }

    switch(c) {
    case ENTER:         /* Enter */
        editor_insert_newline();
        break;
    case CTRL_C:        /* Ctrl-c */
        /* Copy selection to clipboard */
        copy_selection_to_clipboard();
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
    case CTRL_W:        /* Ctrl-w */
        E.word_wrap = !E.word_wrap;
        editor_set_status_msg("Word wrap %s", E.word_wrap ? "enabled" : "disabled");
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

    case SHIFT_ARROW_UP:
    case SHIFT_ARROW_DOWN:
    case SHIFT_ARROW_LEFT:
    case SHIFT_ARROW_RIGHT:
        /* Start selection if not active */
        if (!E.sel_active) {
            E.sel_active = 1;
            E.sel_start_x = E.cx;
            E.sel_start_y = E.cy;
        }
        /* Move cursor */
        if (c == SHIFT_ARROW_UP) editor_move_cursor(ARROW_UP);
        else if (c == SHIFT_ARROW_DOWN) editor_move_cursor(ARROW_DOWN);
        else if (c == SHIFT_ARROW_LEFT) editor_move_cursor(ARROW_LEFT);
        else if (c == SHIFT_ARROW_RIGHT) editor_move_cursor(ARROW_RIGHT);
        /* Update selection end */
        E.sel_end_x = E.cx;
        E.sel_end_y = E.cy;
        break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        /* Clear selection on normal arrow movement */
        E.sel_active = 0;
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
    int rows, cols;
    if (get_window_size(STDIN_FILENO,STDOUT_FILENO,
                      &rows,&cols) == -1) {
        /* If we can't get terminal size (e.g., non-interactive mode), use defaults */
        rows = 24;
        cols = 80;
    }
    E.screencols = cols;
    rows -= STATUS_ROWS;
    if (rows < 1) rows = 1;
    E.screenrows_total = rows;
    editor_update_repl_layout();
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

    /* Check size limit to prevent memory exhaustion */
    if (resp->size + realsize > MAX_HTTP_RESPONSE_SIZE) {
        /* Abort transfer - response too large */
        return 0;
    }

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

/* Detect CA bundle path for SSL/TLS verification */
static const char *detect_ca_bundle_path(void) {
    /* Common CA bundle locations across different platforms */
    static const char *ca_paths[] = {
        "/etc/ssl/cert.pem",                     /* macOS */
        "/etc/ssl/certs/ca-certificates.crt",   /* Debian/Ubuntu/Gentoo */
        "/etc/pki/tls/certs/ca-bundle.crt",     /* Fedora/RHEL */
        "/etc/ssl/ca-bundle.pem",                /* OpenSUSE */
        "/etc/ssl/certs/ca-bundle.crt",         /* Old Red Hat */
        "/usr/local/share/certs/ca-root-nss.crt", /* FreeBSD */
        NULL
    };

    for (int i = 0; ca_paths[i] != NULL; i++) {
        if (access(ca_paths[i], R_OK) == 0) {
            return ca_paths[i];
        }
    }

    /* If no bundle found, let curl use its default */
    return NULL;
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
    if (!req->response.data) {
        free(req);
        return -1;
    }
    req->response.data[0] = '\0';
    req->response.size = 0;
    req->lua_callback = strdup(lua_callback);
    if (!req->lua_callback) {
        free(req->response.data);
        free(req);
        return -1;
    }

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

    /* Detect and set CA bundle path if available */
    const char *ca_bundle = detect_ca_bundle_path();
    if (ca_bundle) {
        curl_easy_setopt(req->easy_handle, CURLOPT_CAINFO, ca_bundle);
    }
    /* Otherwise, let curl use its built-in defaults */

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
    req->header_list = NULL;
    if (headers && num_headers > 0) {
        for (int i = 0; i < num_headers; i++) {
            req->header_list = curl_slist_append(req->header_list, headers[i]);
        }
        curl_easy_setopt(req->easy_handle, CURLOPT_HTTPHEADER, req->header_list);
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
            if (req->header_list) {
                curl_slist_free_all(req->header_list);
            }
            free(req->response.data);
            free(req->lua_callback);
            free(req);

            pending_requests[i] = NULL;
            num_pending--;
        }
    }
}

/* ======================= Lua API bindings ================================ */

/* Lua API: loki.status(message) - Set status message */
static int lua_loki_status(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    editor_set_status_msg("%s", msg);
    return 0;
}

/* Lua API: loki.get_line(row) - Get line content at row (0-indexed) */
static int lua_loki_get_line(lua_State *L) {
    int row = luaL_checkinteger(L, 1);
    if (row < 0 || row >= E.numrows) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, E.row[row].chars);
    return 1;
}

/* Lua API: loki.get_lines() - Get total number of lines */
static int lua_loki_get_lines(lua_State *L) {
    lua_pushinteger(L, E.numrows);
    return 1;
}

/* Lua API: loki.get_cursor() - Get cursor position (returns row, col) */
static int lua_loki_get_cursor(lua_State *L) {
    lua_pushinteger(L, E.cy);
    lua_pushinteger(L, E.cx);
    return 2;
}

/* Lua API: loki.insert_text(text) - Insert text at cursor */
static int lua_loki_insert_text(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    for (const char *p = text; *p; p++) {
        editor_insert_char(*p);
    }
    return 0;
}

/* Lua API: loki.stream_text(text) - Append text and scroll to bottom */
static int lua_loki_stream_text(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);

    /* Move to end of file */
    if (E.numrows > 0) {
        E.cy = E.numrows - 1;
        E.cx = E.row[E.cy].size;
    }

    /* Insert the text */
    for (const char *p = text; *p; p++) {
        editor_insert_char(*p);
    }

    /* Scroll to bottom */
    if (E.numrows > E.screenrows) {
        E.rowoff = E.numrows - E.screenrows;
    }
    E.cy = E.numrows - 1;

    /* Refresh screen immediately */
    editor_refresh_screen();

    return 0;
}

/* Lua API: loki.get_filename() - Get current filename */
static int lua_loki_get_filename(lua_State *L) {
    if (E.filename) {
        lua_pushstring(L, E.filename);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* Helper: Map color name to HL_* constant */
static int color_name_to_hl(const char *name) {
    if (strcasecmp(name, "normal") == 0) return HL_NORMAL;
    if (strcasecmp(name, "nonprint") == 0) return HL_NONPRINT;
    if (strcasecmp(name, "comment") == 0) return HL_COMMENT;
    if (strcasecmp(name, "mlcomment") == 0) return HL_MLCOMMENT;
    if (strcasecmp(name, "keyword1") == 0) return HL_KEYWORD1;
    if (strcasecmp(name, "keyword2") == 0) return HL_KEYWORD2;
    if (strcasecmp(name, "string") == 0) return HL_STRING;
    if (strcasecmp(name, "number") == 0) return HL_NUMBER;
    if (strcasecmp(name, "match") == 0) return HL_MATCH;
    return -1;
}

/* Lua API: loki.set_color(name, {r=R, g=G, b=B}) - Set syntax highlight color */
static int lua_loki_set_color(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    int hl = color_name_to_hl(name);
    if (hl < 0) {
        return luaL_error(L, "Unknown color name: %s", name);
    }

    /* Get RGB values from table */
    lua_getfield(L, 2, "r");
    lua_getfield(L, 2, "g");
    lua_getfield(L, 2, "b");

    if (!lua_isnumber(L, -3) || !lua_isnumber(L, -2) || !lua_isnumber(L, -1)) {
        return luaL_error(L, "Color table must have r, g, b numeric fields");
    }

    int r = lua_tointeger(L, -3);
    int g = lua_tointeger(L, -2);
    int b = lua_tointeger(L, -1);

    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        return luaL_error(L, "RGB values must be 0-255");
    }

    E.colors[hl].r = r;
    E.colors[hl].g = g;
    E.colors[hl].b = b;

    lua_pop(L, 3);
    return 0;
}

/* Lua API: loki.set_theme(table) - Set multiple colors at once */
static int lua_loki_set_theme(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    /* Iterate over theme table */
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        /* Key at -2, value at -1 */
        if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TTABLE) {
            const char *name = lua_tostring(L, -2);
            int hl = color_name_to_hl(name);

            if (hl >= 0) {
                /* Get RGB from value table */
                lua_getfield(L, -1, "r");
                lua_getfield(L, -1, "g");
                lua_getfield(L, -1, "b");

                if (lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
                    int r = lua_tointeger(L, -3);
                    int g = lua_tointeger(L, -2);
                    int b = lua_tointeger(L, -1);

                    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                        E.colors[hl].r = r;
                        E.colors[hl].g = g;
                        E.colors[hl].b = b;
                    }
                }
                lua_pop(L, 3);
            }
        }
        lua_pop(L, 1); /* Remove value, keep key for next iteration */
    }

    return 0;
}

/* Lua API: loki.async_http(url, method, body, headers, callback) - Async HTTP request */
static int lua_loki_async_http(lua_State *L) {
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
            if (!headers) {
                return luaL_error(L, "Out of memory allocating HTTP headers array");
            }
            int i = 0;
            lua_pushnil(L);
            while (lua_next(L, 4) != 0) {
                const char *header_str = strdup(lua_tostring(L, -1));
                if (!header_str) {
                    /* Free previously allocated headers */
                    for (int j = 0; j < i; j++) {
                        free((void*)headers[j]);
                    }
                    free(headers);
                    lua_pop(L, 1);  /* Pop the value */
                    return luaL_error(L, "Out of memory allocating HTTP header string");
                }
                headers[i++] = header_str;
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

static int lua_loki_repl_register(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *description = luaL_checkstring(L, 2);
    const char *example = luaL_optstring(L, 3, NULL);

    lua_getglobal(L, "loki");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }

    lua_getfield(L, -1, "__repl_help");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return 0;
    }

    lua_newtable(L);
    lua_pushstring(L, name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, description);
    lua_setfield(L, -2, "description");
    if (example) {
        lua_pushstring(L, example);
        lua_setfield(L, -2, "example");
    }

    int idx = (int)lua_rawlen(L, -2) + 1;
    lua_rawseti(L, -2, idx);

    lua_pop(L, 2);
    return 0;
}

/* Lua API: loki.register_language(config) - Register a new language for syntax highlighting
 * config table must contain:
 *   - name (string): Language name
 *   - extensions (table): File extensions (e.g., {".py", ".pyw"})
 * Optional fields:
 *   - keywords (table): Language keywords
 *   - types (table): Type keywords
 *   - line_comment (string): Single-line comment delimiter
 *   - block_comment_start (string): Multi-line comment start
 *   - block_comment_end (string): Multi-line comment end
 *   - separators (string): Separator characters
 *   - highlight_strings (boolean): Enable string highlighting (default: true)
 *   - highlight_numbers (boolean): Enable number highlighting (default: true)
 */
static int lua_loki_register_language(lua_State *L) {
    /* Validate argument is a table */
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "argument must be a table");
        return 2;
    }

    /* Allocate new language structure */
    struct t_editor_syntax *lang = calloc(1, sizeof(struct t_editor_syntax));
    if (!lang) {
        lua_pushnil(L);
        lua_pushstring(L, "memory allocation failed");
        return 2;
    }

    /* Extract name (required) */
    lua_getfield(L, 1, "name");
    if (!lua_isstring(L, -1)) {
        free(lang);
        lua_pushnil(L);
        lua_pushstring(L, "'name' field is required and must be a string");
        return 2;
    }
    /* Note: name is not stored in the struct, just used for error messages */
    lua_pop(L, 1);

    /* Extract extensions (required) */
    lua_getfield(L, 1, "extensions");
    if (!lua_istable(L, -1)) {
        free(lang);
        lua_pushnil(L);
        lua_pushstring(L, "'extensions' field is required and must be a table");
        return 2;
    }

    /* Count extensions */
    int ext_count = (int)lua_rawlen(L, -1);
    if (ext_count == 0) {
        lua_pop(L, 1);
        free(lang);
        lua_pushnil(L);
        lua_pushstring(L, "'extensions' table cannot be empty");
        return 2;
    }

    /* Allocate extension array (NULL-terminated) */
    lang->filematch = calloc(ext_count + 1, sizeof(char*));
    if (!lang->filematch) {
        lua_pop(L, 1);
        free(lang);
        lua_pushnil(L);
        lua_pushstring(L, "memory allocation failed for extensions");
        return 2;
    }

    /* Copy extensions */
    for (int i = 0; i < ext_count; i++) {
        lua_rawgeti(L, -1, i + 1);
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 2);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "extension must be a string");
            return 2;
        }
        const char *ext = lua_tostring(L, -1);
        if (ext[0] != '.') {
            lua_pop(L, 2);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "extension must start with '.'");
            return 2;
        }
        lang->filematch[i] = strdup(ext);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  /* pop extensions table */

    /* Extract keywords (optional) */
    lua_getfield(L, 1, "keywords");
    lua_getfield(L, 1, "types");
    int kw_count = lua_istable(L, -2) ? (int)lua_rawlen(L, -2) : 0;
    int type_count = lua_istable(L, -1) ? (int)lua_rawlen(L, -1) : 0;
    int total_kw = kw_count + type_count;

    if (total_kw > 0) {
        lang->keywords = calloc(total_kw + 1, sizeof(char*));
        if (!lang->keywords) {
            lua_pop(L, 2);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "memory allocation failed for keywords");
            return 2;
        }

        /* Copy regular keywords */
        int idx = 0;
        if (kw_count > 0) {
            for (int i = 0; i < kw_count; i++) {
                lua_rawgeti(L, -2, i + 1);
                if (lua_isstring(L, -1)) {
                    const char *kw = lua_tostring(L, -1);
                    lang->keywords[idx++] = strdup(kw);
                }
                lua_pop(L, 1);
            }
        }

        /* Copy type keywords (append "|" to distinguish them) */
        if (type_count > 0) {
            for (int i = 0; i < type_count; i++) {
                lua_rawgeti(L, -1, i + 1);
                if (lua_isstring(L, -1)) {
                    const char *type = lua_tostring(L, -1);
                    size_t len = strlen(type);
                    char *type_with_pipe = malloc(len + 2);
                    if (type_with_pipe) {
                        strcpy(type_with_pipe, type);
                        type_with_pipe[len] = '|';
                        type_with_pipe[len + 1] = '\0';
                        lang->keywords[idx++] = type_with_pipe;
                    }
                }
                lua_pop(L, 1);
            }
        }
    }
    lua_pop(L, 2);  /* pop types and keywords tables */

    /* Extract comment delimiters (optional) */
    lua_getfield(L, 1, "line_comment");
    if (lua_isstring(L, -1)) {
        const char *lc = lua_tostring(L, -1);
        size_t len = strlen(lc);
        if (len >= sizeof(lang->singleline_comment_start)) {
            lua_pop(L, 1);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "line_comment too long (max 3 chars)");
            return 2;
        }
        strncpy(lang->singleline_comment_start, lc, sizeof(lang->singleline_comment_start) - 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "block_comment_start");
    if (lua_isstring(L, -1)) {
        const char *bcs = lua_tostring(L, -1);
        size_t len = strlen(bcs);
        if (len >= sizeof(lang->multiline_comment_start)) {
            lua_pop(L, 1);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "block_comment_start too long (max 5 chars)");
            return 2;
        }
        strncpy(lang->multiline_comment_start, bcs, sizeof(lang->multiline_comment_start) - 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "block_comment_end");
    if (lua_isstring(L, -1)) {
        const char *bce = lua_tostring(L, -1);
        size_t len = strlen(bce);
        if (len >= sizeof(lang->multiline_comment_end)) {
            lua_pop(L, 1);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "block_comment_end too long (max 5 chars)");
            return 2;
        }
        strncpy(lang->multiline_comment_end, bce, sizeof(lang->multiline_comment_end) - 1);
    }
    lua_pop(L, 1);

    /* Extract separators (optional) */
    lua_getfield(L, 1, "separators");
    if (lua_isstring(L, -1)) {
        const char *sep = lua_tostring(L, -1);
        lang->separators = strdup(sep);
    } else {
        lang->separators = strdup(",.()+-/*=~%<>[];");  /* Default separators */
    }
    lua_pop(L, 1);

    /* Extract flags (optional) */
    lua_getfield(L, 1, "highlight_strings");
    int hl_strings = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : 1;  /* Default: true */
    lua_pop(L, 1);

    lua_getfield(L, 1, "highlight_numbers");
    int hl_numbers = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : 1;  /* Default: true */
    lua_pop(L, 1);

    lang->flags = 0;
    if (hl_strings) lang->flags |= HL_HIGHLIGHT_STRINGS;
    if (hl_numbers) lang->flags |= HL_HIGHLIGHT_NUMBERS;

    lang->type = HL_TYPE_C;  /* Use C-style highlighting */

    /* Add to dynamic registry */
    if (add_dynamic_language(lang) != 0) {
        free_dynamic_language(lang);
        lua_pushnil(L);
        lua_pushstring(L, "failed to register language");
        return 2;
    }

    lua_pushboolean(L, 1);  /* Success */
    return 1;
}

static int lua_loki_status_stdout(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    if (msg && *msg) {
        fprintf(stdout, "[loki] %s\n", msg);
        fflush(stdout);
    }
    return 0;
}

static void loki_lua_bind_minimal(lua_State *L) {
    lua_getglobal(L, "loki");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }

    lua_pushcfunction(L, lua_loki_status_stdout);
    lua_setfield(L, -2, "status");

    lua_pushcfunction(L, lua_loki_register_language);
    lua_setfield(L, -2, "register_language");

    lua_newtable(L); /* storage for registered help */
    lua_setfield(L, -2, "__repl_help");

    lua_newtable(L); /* repl helpers */
    lua_pushcfunction(L, lua_loki_repl_register);
    lua_setfield(L, -2, "register");
    lua_setfield(L, -2, "repl");

    lua_setglobal(L, "loki");
}

/* Initialize Lua API */
void loki_lua_bind_editor(lua_State *L) {
    /* Create loki table */
    lua_newtable(L);

    /* Register functions */
    lua_pushcfunction(L, lua_loki_status);
    lua_setfield(L, -2, "status");

    lua_pushcfunction(L, lua_loki_get_line);
    lua_setfield(L, -2, "get_line");

    lua_pushcfunction(L, lua_loki_get_lines);
    lua_setfield(L, -2, "get_lines");

    lua_pushcfunction(L, lua_loki_get_cursor);
    lua_setfield(L, -2, "get_cursor");

    lua_pushcfunction(L, lua_loki_insert_text);
    lua_setfield(L, -2, "insert_text");

    lua_pushcfunction(L, lua_loki_stream_text);
    lua_setfield(L, -2, "stream_text");

    lua_pushcfunction(L, lua_loki_get_filename);
    lua_setfield(L, -2, "get_filename");

    lua_pushcfunction(L, lua_loki_set_color);
    lua_setfield(L, -2, "set_color");

    lua_pushcfunction(L, lua_loki_set_theme);
    lua_setfield(L, -2, "set_theme");

    lua_pushcfunction(L, lua_loki_async_http);
    lua_setfield(L, -2, "async_http");

    lua_pushcfunction(L, lua_loki_register_language);
    lua_setfield(L, -2, "register_language");

    lua_newtable(L); /* storage for registered help */
    lua_setfield(L, -2, "__repl_help");

    lua_newtable(L); /* repl helpers */
    lua_pushcfunction(L, lua_loki_repl_register);
    lua_setfield(L, -2, "register");
    lua_setfield(L, -2, "repl");

    /* Highlight constants */
    lua_newtable(L);
    lua_pushinteger(L, HL_NORMAL);
    lua_setfield(L, -2, "normal");
    lua_pushinteger(L, HL_NONPRINT);
    lua_setfield(L, -2, "nonprint");
    lua_pushinteger(L, HL_COMMENT);
    lua_setfield(L, -2, "comment");
    lua_pushinteger(L, HL_MLCOMMENT);
    lua_setfield(L, -2, "mlcomment");
    lua_pushinteger(L, HL_KEYWORD1);
    lua_setfield(L, -2, "keyword1");
    lua_pushinteger(L, HL_KEYWORD2);
    lua_setfield(L, -2, "keyword2");
    lua_pushinteger(L, HL_STRING);
    lua_setfield(L, -2, "string");
    lua_pushinteger(L, HL_NUMBER);
    lua_setfield(L, -2, "number");
    lua_pushinteger(L, HL_MATCH);
    lua_setfield(L, -2, "match");
    lua_setfield(L, -2, "hl");

    /* Set as global 'loki' */
    lua_setglobal(L, "loki");
}

static void loki_lua_report(const struct loki_lua_opts *opts, const char *fmt, ...) {
    if (!fmt) return;

    char message[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    if (opts && opts->reporter) {
        opts->reporter(message, opts->reporter_userdata);
    } else {
        fprintf(stderr, "%s\n", message);
    }
}

/* Load init.lua: try override, .loki/init.lua (local), then ~/.loki/init.lua */
int loki_lua_load_config(lua_State *L, const struct loki_lua_opts *opts) {
    if (!L) return -1;

    const char *override = (opts && opts->config_override && opts->config_override[0] != '\0')
                               ? opts->config_override
                               : NULL;
    const char *project_root = NULL;
    char init_path[1024];
    int loaded = 0;

    if (opts && opts->project_root && opts->project_root[0] != '\0') {
        project_root = opts->project_root;
    }

    if (override && override[0] != '\0') {
        if (luaL_dofile(L, override) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            loki_lua_report(opts, "Lua init error (%s): %s", override, err ? err : "unknown");
            lua_pop(L, 1);
            return -1;
        }
        return 0;
    }

    /* Try local .loki/init.lua first (project-specific) */
    if (project_root) {
        snprintf(init_path, sizeof(init_path), "%s/.loki/init.lua", project_root);
    } else {
        snprintf(init_path, sizeof(init_path), ".loki/init.lua");
    }

    if (access(init_path, R_OK) == 0) {
        if (luaL_dofile(L, init_path) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            loki_lua_report(opts, "Lua init error (%s): %s", init_path, err ? err : "unknown");
            lua_pop(L, 1);
            return -1;
        }
        loaded = 1;
    }

    if (!loaded) {
        const char *home = getenv("HOME");
        if (home && home[0] != '\0') {
            snprintf(init_path, sizeof(init_path), "%s/.loki/init.lua", home);
            if (access(init_path, R_OK) == 0) {
                if (luaL_dofile(L, init_path) != LUA_OK) {
                    const char *err = lua_tostring(L, -1);
                    loki_lua_report(opts, "Lua init error (%s): %s", init_path, err ? err : "unknown");
                    lua_pop(L, 1);
                    return -1;
                }
                loaded = 1;
            }
        }
    }

    return loaded ? 0 : 1;
}

static void loki_lua_extend_path(lua_State *L, const struct loki_lua_opts *opts) {
    if (!L) return;

    char addition[4096];
    addition[0] = '\0';
    size_t used = 0;
    size_t remaining = sizeof(addition);

    const char *project_root = (opts && opts->project_root && opts->project_root[0] != '\0')
                                   ? opts->project_root
                                   : ".";
    int wrote = snprintf(addition + used, remaining, "%s/.loki/?.lua;%s/.loki/?/init.lua;",
                         project_root, project_root);
    if (wrote > 0 && (size_t)wrote < remaining) {
        used += (size_t)wrote;
        remaining -= (size_t)wrote;
    } else {
        remaining = remaining > 0 ? 1 : 0;
        used = sizeof(addition) - remaining;
    }

    const char *home = getenv("HOME");
    if (home && home[0] != '\0' && remaining > 1) {
        wrote = snprintf(addition + used, remaining, "%s/.loki/?.lua;%s/.loki/?/init.lua;",
                         home, home);
        if (wrote > 0 && (size_t)wrote < remaining) {
            used += (size_t)wrote;
            remaining -= (size_t)wrote;
        } else {
            remaining = remaining > 0 ? 1 : 0;
            used = sizeof(addition) - remaining;
        }
    }

    const char *extra = NULL;
    if (opts && opts->extra_lua_path && opts->extra_lua_path[0] != '\0') {
        extra = opts->extra_lua_path;
    } else {
        const char *env_extra = getenv("LOKI_LUA_PATH");
        if (env_extra && env_extra[0] != '\0') {
            extra = env_extra;
        }
    }
    if (extra && remaining > 1) {
        wrote = snprintf(addition + used, remaining, "%s;", extra);
        if (wrote > 0 && (size_t)wrote < remaining) {
            used += (size_t)wrote;
            remaining -= (size_t)wrote;
        }
    }

    if (used == 0) {
        return;
    }

    if (addition[used - 1] == ';') {
        addition[used - 1] = '\0';
    }

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "path");
    const char *current = lua_tostring(L, -1);
    if (!current) current = "";

    lua_pushfstring(L, "%s;%s", current, addition);
    lua_setfield(L, -3, "path");

    lua_pop(L, 2); /* pop path and package */
}

void loki_lua_bind_http(lua_State *L) {
    if (!L) return;

    lua_getglobal(L, "loki");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }

    lua_pushcfunction(L, lua_loki_async_http);
    lua_setfield(L, -2, "async_http");

    lua_setglobal(L, "loki");
}

void loki_lua_install_namespaces(lua_State *L) {
    if (!L) return;

    lua_getglobal(L, "loki");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    if (lua_getfield(L, -1, "async_http") != LUA_TFUNCTION) {
        lua_pop(L, 2);
        return;
    }
    lua_pop(L, 1); /* remove async_http */

    static const char *const shim =
        "local loki = ...\n"
        "local function ensure(tbl, key)\n"
        "  local value = rawget(tbl, key)\n"
        "  if type(value) ~= 'table' then\n"
        "    value = {}\n"
        "    rawset(tbl, key, value)\n"
        "  end\n"
        "  return value\n"
        "end\n"
        "local editor = ensure(loki, 'editor')\n"
        "editor.buffer = ensure(editor, 'buffer')\n"
        "editor.status = ensure(editor, 'status')\n"
        "function editor.status.set(message)\n"
        "  return loki.status(message)\n"
        "end\n"
        "function editor.buffer.get_line(idx)\n"
        "  return loki.get_line(idx)\n"
        "end\n"
        "function editor.buffer.line_count()\n"
        "  return loki.get_lines()\n"
        "end\n"
        "function editor.buffer.insert(text)\n"
        "  return loki.insert_text(text)\n"
        "end\n"
        "local ai = rawget(_G, 'ai')\n"
        "if type(ai) ~= 'table' then\n"
        "  ai = {}\n"
        "  rawset(_G, 'ai', ai)\n"
        "end\n"
        "local function default_headers()\n"
        "  local api_key = os.getenv('OPENAI_API_KEY')\n"
        "  if api_key and api_key ~= '' then\n"
        "    return {\n"
        "      'Content-Type: application/json',\n"
        "      'Authorization: Bearer ' .. api_key,\n"
        "    }\n"
        "  end\n"
        "  return { 'Content-Type: application/json' }\n"
        "end\n"
        "local function default_body(prompt, opts)\n"
        "  local model = (opts and opts.model) or os.getenv('LOKI_AI_MODEL') or 'gpt-5-nano'\n"
        "  local temperature = (opts and opts.temperature) or 0.2\n"
        "  return string.format('{\"model\":%q,\"temperature\":%.3f,\"messages\":[{\"role\":\"user\",\"content\":%q}]}',\n"
        "    model, temperature, prompt)\n"
        "end\n"
        "function ai.prompt(prompt, opts)\n"
        "  assert(type(prompt) == 'string', 'ai.prompt expects a prompt string')\n"
        "  opts = opts or {}\n"
        "  local url = opts.url or os.getenv('LOKI_AI_URL') or 'https://api.openai.com/v1/chat/completions'\n"
        "  local method = opts.method or 'POST'\n"
        "  local callback = opts.callback or opts.on_response or 'ai_response_handler'\n"
        "  assert(type(callback) == 'string' and callback ~= '', 'ai.prompt requires opts.callback (Lua function name)')\n"
        "  local headers = opts.headers\n"
        "  if headers == nil then headers = default_headers() end\n"
        "  local body = opts.body\n"
        "  if body == nil then body = default_body(prompt, opts) end\n"
        "  return loki.async_http(url, method, body, headers, callback)\n"
        "end\n";

    if (luaL_loadstring(L, shim) == LUA_OK) {
        lua_pushvalue(L, -2); /* push loki table */
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            fprintf(stderr, "Failed to install ai namespace: %s\n", err ? err : "unknown error");
            lua_pop(L, 1);
        }
    } else {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "Failed to compile ai namespace shim: %s\n", err ? err : "unknown error");
        lua_pop(L, 1);
    }

    lua_pop(L, 1); /* loki */
}

const char *loki_lua_runtime(void) {
#if defined(LUAJIT_VERSION)
    return "LuaJIT " LUAJIT_VERSION;
#elif defined(LUA_VERSION_MAJOR) && defined(LUA_VERSION_MINOR)
    return "Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR;
#elif defined(LUA_VERSION)
    return LUA_VERSION;
#else
    return "Lua";
#endif
}

lua_State *loki_lua_bootstrap(const struct loki_lua_opts *opts) {
    struct loki_lua_opts effective = {
        .bind_editor = 1,
        .bind_http = 1,
        .load_config = 1,
        .config_override = NULL,
        .project_root = NULL,
        .extra_lua_path = NULL,
        .reporter = NULL,
        .reporter_userdata = NULL,
    };

    if (opts) {
        effective.bind_editor = opts->bind_editor ? 1 : 0;
        effective.bind_http = opts->bind_http ? 1 : 0;
        effective.load_config = opts->load_config ? 1 : 0;
        effective.config_override = opts->config_override;
        effective.project_root = opts->project_root;
        effective.extra_lua_path = opts->extra_lua_path;
        effective.reporter = opts->reporter;
        effective.reporter_userdata = opts->reporter_userdata;
    }

    lua_State *L = luaL_newstate();
    if (!L) {
        loki_lua_report(&effective, "Failed to allocate Lua state");
        return NULL;
    }

    luaL_openlibs(L);
    loki_lua_extend_path(L, &effective);

    if (effective.bind_editor) {
        loki_lua_bind_editor(L);
    } else {
        loki_lua_bind_minimal(L);
    }

    if (effective.bind_http) {
        loki_lua_bind_http(L);
    }

    if (effective.load_config) {
        if (loki_lua_load_config(L, &effective) < 0) {
            /* Leave state usable even if config fails; errors already reported */
        }
    }

    loki_lua_install_namespaces(L);

    return L;
}

/* =========================== Lua REPL Helpers ============================ */

static void lua_repl_clear_input(t_lua_repl *repl) {
    repl->input_len = 0;
    repl->input[0] = '\0';
}

static void lua_repl_append_log_owned(char *line) {
    if (!line) return;
    if (E.repl.log_len == LUA_REPL_LOG_MAX) {
        free(E.repl.log[0]);
        memmove(E.repl.log, E.repl.log + 1,
                sizeof(char*) * (LUA_REPL_LOG_MAX - 1));
        E.repl.log_len--;
    }
    E.repl.log[E.repl.log_len++] = line;
}

static void lua_repl_append_log(const char *line) {
    if (!line) return;
    char *copy = strdup(line);
    if (!copy) {
        editor_set_status_msg("Lua REPL: out of memory");
        return;
    }
    lua_repl_append_log_owned(copy);
}

static void lua_repl_log_prefixed(const char *prefix, const char *text) {
    if (!prefix) prefix = "";
    if (!text) text = "";

    size_t prefix_len = strlen(prefix);
    const char *line = text;
    do {
        const char *newline = strchr(line, '\n');
        size_t segment_len = newline ? (size_t)(newline - line) : strlen(line);
        size_t total = prefix_len + segment_len;
        char *entry = malloc(total + 1);
        if (!entry) {
            editor_set_status_msg("Lua REPL: out of memory");
            return;
        }
        memcpy(entry, prefix, prefix_len);
        if (segment_len) memcpy(entry + prefix_len, line, segment_len);
        entry[total] = '\0';
        lua_repl_append_log_owned(entry);
        if (!newline) break;
        line = newline + 1;
        if (*line == '\0') {
            /* Preserve empty trailing line */
            char *blank = strdup(prefix);
            if (!blank) {
                editor_set_status_msg("Lua REPL: out of memory");
                return;
            }
            lua_repl_append_log_owned(blank);
            break;
        }
    } while (1);
}

static void lua_repl_reset_log(t_lua_repl *repl) {
    for (int i = 0; i < repl->log_len; i++) {
        free(repl->log[i]);
        repl->log[i] = NULL;
    }
    repl->log_len = 0;
}

static const char *lua_repl_top_to_string(lua_State *L, size_t *len) {
#if LUA_VERSION_NUM >= 502
    return luaL_tolstring(L, -1, len);
#else
    if (luaL_callmeta(L, -1, "__tostring")) {
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 1);
            return NULL;
        }
        return lua_tolstring(L, -1, len);
    }
    return lua_tolstring(L, -1, len);
#endif
}

static void lua_repl_push_history(const char *cmd) {
    if (!cmd || !*cmd) return;
    size_t len = strlen(cmd);
    int all_space = 1;
    for (size_t i = 0; i < len; i++) {
        if (!isspace((unsigned char)cmd[i])) {
            all_space = 0;
            break;
        }
    }
    if (all_space) return;

    if (E.repl.history_len == LUA_REPL_HISTORY_MAX) {
        free(E.repl.history[0]);
        memmove(E.repl.history, E.repl.history + 1,
                sizeof(char*) * (LUA_REPL_HISTORY_MAX - 1));
        E.repl.history_len--;
    }

    if (E.repl.history_len > 0) {
        const char *last = E.repl.history[E.repl.history_len - 1];
        if (last && strcmp(last, cmd) == 0) {
            E.repl.history_index = -1;
            return;
        }
    }

    char *copy = strdup(cmd);
    if (!copy) {
        editor_set_status_msg("Lua REPL: out of memory");
        return;
    }
    E.repl.history[E.repl.history_len++] = copy;
    E.repl.history_index = -1;
}

static void lua_repl_history_apply(t_lua_repl *repl) {
    if (repl->history_index < 0 || repl->history_index >= repl->history_len)
        return;
    const char *src = repl->history[repl->history_index];
    if (!src) {
        lua_repl_clear_input(repl);
        return;
    }
    size_t copy_len = strlen(src);
    if (copy_len > KILO_QUERY_LEN) copy_len = KILO_QUERY_LEN;
    int prompt_len = (int)strlen(LUA_REPL_PROMPT);
    if (E.screencols > prompt_len) {
        int max_cols = E.screencols - prompt_len;
        if ((int)copy_len > max_cols) copy_len = max_cols;
    }
    memcpy(repl->input, src, copy_len);
    repl->input[copy_len] = '\0';
    repl->input_len = (int)copy_len;
}

static int lua_repl_input_has_content(const t_lua_repl *repl) {
    for (int i = 0; i < repl->input_len; i++) {
        if (!isspace((unsigned char)repl->input[i])) return 1;
    }
    return 0;
}

static void lua_repl_emit_registered_help(void) {
    if (!E.L) return;

    lua_getglobal(E.L, "loki");
    if (!lua_istable(E.L, -1)) {
        lua_pop(E.L, 1);
        return;
    }

    lua_getfield(E.L, -1, "__repl_help");
    if (!lua_istable(E.L, -1)) {
        lua_pop(E.L, 2);
        return;
    }

    int len = (int)lua_rawlen(E.L, -1);
    if (len == 0) {
        lua_pop(E.L, 2);
        return;
    }

    lua_repl_log_prefixed("= ", "Project commands:");

    lua_pushnil(E.L);
    while (lua_next(E.L, -2) != 0) {
        const char *name = NULL;
        const char *desc = NULL;
        const char *example = NULL;

        lua_getfield(E.L, -1, "name");
        if (lua_isstring(E.L, -1)) name = lua_tostring(E.L, -1);
        lua_pop(E.L, 1);

        lua_getfield(E.L, -1, "description");
        if (lua_isstring(E.L, -1)) desc = lua_tostring(E.L, -1);
        lua_pop(E.L, 1);

        lua_getfield(E.L, -1, "example");
        if (lua_isstring(E.L, -1)) example = lua_tostring(E.L, -1);
        lua_pop(E.L, 1);

        if (name && desc) {
            char buf[256];
            snprintf(buf, sizeof(buf), "  %s - %s", name, desc);
            lua_repl_append_log(buf);
        }
        if (example) {
            char buf[256];
            snprintf(buf, sizeof(buf), "    e.g. %s", example);
            lua_repl_append_log(buf);
        }

        lua_pop(E.L, 1); /* pop value, keep key for next iteration */
    }

    lua_pop(E.L, 2); /* __repl_help, loki */
}

static void lua_repl_execute_current(void) {
    if (!E.L) {
        lua_repl_append_log("! Lua interpreter not available");
        return;
    }

    if (!lua_repl_input_has_content(&E.repl)) {
        lua_repl_clear_input(&E.repl);
        return;
    }

    lua_repl_log_prefixed(LUA_REPL_PROMPT, E.repl.input);
    lua_repl_push_history(E.repl.input);

    const char *trim = E.repl.input;
    while (*trim && isspace((unsigned char)*trim)) trim++;
    size_t tlen = strlen(trim);
    while (tlen > 0 && isspace((unsigned char)trim[tlen-1])) tlen--;

    if (lua_repl_handle_builtin(trim, tlen)) {
        lua_repl_clear_input(&E.repl);
        return;
    }

    int base = lua_gettop(E.L);
    if (luaL_loadbuffer(E.L, E.repl.input, (size_t)E.repl.input_len,
                        "repl") != LUA_OK) {
        const char *err = lua_tostring(E.L, -1);
        lua_repl_log_prefixed("! ", err ? err : "(unknown error)");
        lua_pop(E.L, 1);
        lua_settop(E.L, base);
        lua_repl_clear_input(&E.repl);
        return;
    }

    int status = lua_pcall(E.L, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        const char *err = lua_tostring(E.L, -1);
        lua_repl_log_prefixed("! ", err ? err : "(unknown error)");
        lua_pop(E.L, 1);
        lua_settop(E.L, base);
        lua_repl_clear_input(&E.repl);
        return;
    }

    int results = lua_gettop(E.L) - base;
    if (results == 0) {
        lua_repl_log_prefixed("= ", "ok");
    } else {
        for (int i = 0; i < results; i++) {
            lua_pushvalue(E.L, base + 1 + i);
            size_t len = 0;
            const char *res = lua_repl_top_to_string(E.L, &len);
            if (res) {
                lua_repl_log_prefixed("= ", res);
            } else {
                lua_repl_log_prefixed("= ", "(non-printable)");
            }
            lua_settop(E.L, base + results);
        }
    }
    lua_settop(E.L, base);
    lua_repl_clear_input(&E.repl);
}

static int lua_repl_iequals(const char *cmd, size_t len, const char *word) {
    size_t wlen = strlen(word);
    if (len != wlen) return 0;
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)cmd[i]) != tolower((unsigned char)word[i]))
            return 0;
    }
    return 1;
}

static int lua_repl_handle_builtin(const char *cmd, size_t len) {
    if (!cmd) return 0;
    while (len && isspace((unsigned char)*cmd)) {
        cmd++;
        len--;
    }
    while (len && isspace((unsigned char)cmd[len-1])) len--;
    if (len == 0) return 0;

    if (cmd[0] == ':') {
        cmd++;
        if (len) len--;
        while (len && isspace((unsigned char)*cmd)) {
            cmd++;
            len--;
        }
        while (len && isspace((unsigned char)cmd[len-1])) len--;
        if (len == 0) return 0;
    }

    if ((len == 1 && cmd[0] == '?') || lua_repl_iequals(cmd, len, "help")) {
        lua_repl_log_prefixed("= ", "Built-in commands:");
        lua_repl_append_log("  help       Show this help message");
        lua_repl_append_log("  history    Print recent commands");
        lua_repl_append_log("  clear      Clear the REPL output log");
        lua_repl_append_log("  clear-history  Drop saved input history");
        lua_repl_append_log("  exit       Close the REPL panel");
        lua_repl_emit_registered_help();
        lua_repl_append_log("  Lua code   Any other input runs inside loki's Lua state");
        return 1;
    }

    if (lua_repl_iequals(cmd, len, "clear")) {
        lua_repl_reset_log(&E.repl);
        lua_repl_log_prefixed("= ", "Log cleared");
        return 1;
    }

    if (lua_repl_iequals(cmd, len, "history")) {
        if (E.repl.history_len == 0) {
            lua_repl_log_prefixed("= ", "History is empty");
            return 1;
        }
        lua_repl_log_prefixed("= ", "History (newest first):");
        int start = E.repl.history_len - 1;
        int shown = 0;
        for (int i = start; i >= 0; i--) {
            const char *entry = E.repl.history[i];
            if (!entry) continue;
            char buf[256];
            snprintf(buf, sizeof(buf), "  %d: %s", E.repl.history_len - i, entry);
            lua_repl_append_log(buf);
            shown++;
            if (shown >= 20) {
                lua_repl_append_log("  ...");
                break;
            }
        }
        return 1;
    }

    if (lua_repl_iequals(cmd, len, "clear-history")) {
        for (int i = 0; i < E.repl.history_len; i++) {
            free(E.repl.history[i]);
            E.repl.history[i] = NULL;
        }
        E.repl.history_len = 0;
        E.repl.history_index = -1;
        lua_repl_log_prefixed("= ", "History cleared");
        return 1;
    }

    if (lua_repl_iequals(cmd, len, "exit") || lua_repl_iequals(cmd, len, "quit")) {
        E.repl.active = 0;
        editor_update_repl_layout();
        editor_set_status_msg("Lua REPL closed");
        return 1;
    }

    return 0;
}

static void lua_repl_handle_keypress(int key) {
    t_lua_repl *repl = &E.repl;
    int prompt_len = (int)strlen(LUA_REPL_PROMPT);

    switch(key) {
    case CTRL_L:
    case ESC:
    case CTRL_C:
        repl->active = 0;
        editor_update_repl_layout();
        editor_set_status_msg("Lua REPL closed");
        return;
    case CTRL_U:
        lua_repl_clear_input(repl);
        repl->history_index = -1;
        return;
    case BACKSPACE:
    case CTRL_H:
    case DEL_KEY:
        if (repl->input_len > 0) {
            repl->input[--repl->input_len] = '\0';
        }
        repl->history_index = -1;
        return;
    case ARROW_UP:
        if (repl->history_len > 0) {
            if (repl->history_index == -1)
                repl->history_index = repl->history_len - 1;
            else if (repl->history_index > 0)
                repl->history_index--;
            lua_repl_history_apply(repl);
        }
        return;
    case ARROW_DOWN:
        if (repl->history_len > 0) {
            if (repl->history_index == -1) {
                return;
            } else if (repl->history_index < repl->history_len - 1) {
                repl->history_index++;
                lua_repl_history_apply(repl);
            } else {
                repl->history_index = -1;
                lua_repl_clear_input(repl);
            }
        }
        return;
    case ENTER:
        lua_repl_execute_current();
        if (!repl->active) {
            editor_update_repl_layout();
        }
        return;
    default:
        if (isprint(key)) {
            if (repl->input_len < KILO_QUERY_LEN) {
                if (E.screencols <= prompt_len) return;
                if (prompt_len + repl->input_len >= E.screencols) return;
                repl->input[repl->input_len++] = key;
                repl->input[repl->input_len] = '\0';
                repl->history_index = -1;
            }
        }
        return;
    }
}

static void lua_repl_free(t_lua_repl *repl) {
    for (int i = 0; i < repl->history_len; i++) {
        free(repl->history[i]);
        repl->history[i] = NULL;
    }
    repl->history_len = 0;
    repl->history_index = -1;

    lua_repl_reset_log(repl);
}

static void lua_repl_init(t_lua_repl *repl) {
    lua_repl_free(repl);
    repl->active = 0;
    repl->history_index = -1;
    lua_repl_clear_input(repl);
}

static void editor_update_repl_layout(void) {
    int reserved = E.repl.active ? LUA_REPL_TOTAL_ROWS : 0;
    int available = E.screenrows_total;
    if (available > reserved) {
        E.screenrows = available - reserved;
    } else {
        E.screenrows = 1;
    }
    if (E.screenrows < 1) E.screenrows = 1;

    if (E.cy >= E.screenrows) {
        E.cy = E.screenrows - 1;
        if (E.cy < 0) E.cy = 0;
    }

    if (E.numrows > E.screenrows && E.rowoff > E.numrows - E.screenrows) {
        E.rowoff = E.numrows - E.screenrows;
    }
    if (E.numrows <= E.screenrows) {
        E.rowoff = 0;
    }
}

/* Toggle the Lua REPL focus */
static void exec_lua_command(int fd) {
    (void)fd;
    if (!E.L) {
        editor_set_status_msg("Lua not available");
        return;
    }
    int was_active = E.repl.active;
    E.repl.active = !E.repl.active;
    editor_update_repl_layout();
    if (E.repl.active) {
        E.repl.history_index = -1;
        editor_set_status_msg(
            "Lua REPL: Enter runs, ESC exits, Up/Down history, type 'help'");
        if (E.repl.log_len == 0) {
            lua_repl_append_log("Type 'help' for built-in commands");
        }
    } else {
        if (was_active) {
            editor_set_status_msg("Lua REPL closed");
        }
    }
}

static void loki_lua_status_reporter(const char *message, void *userdata) {
    (void)userdata;
    if (message && message[0] != '\0') {
        editor_set_status_msg("%s", message);
    }
}

/* Initialize default syntax highlighting colors.
 * Colors are stored as RGB values and rendered using true color escape codes.
 * These defaults match the visual appearance of the original ANSI color scheme. */
void init_default_colors(void) {
    /* HL_NORMAL */
    E.colors[0].r = 200; E.colors[0].g = 200; E.colors[0].b = 200;
    /* HL_NONPRINT */
    E.colors[1].r = 100; E.colors[1].g = 100; E.colors[1].b = 100;
    /* HL_COMMENT */
    E.colors[2].r = 100; E.colors[2].g = 100; E.colors[2].b = 100;
    /* HL_MLCOMMENT */
    E.colors[3].r = 100; E.colors[3].g = 100; E.colors[3].b = 100;
    /* HL_KEYWORD1 */
    E.colors[4].r = 220; E.colors[4].g = 100; E.colors[4].b = 220;
    /* HL_KEYWORD2 */
    E.colors[5].r = 100; E.colors[5].g = 220; E.colors[5].b = 220;
    /* HL_STRING */
    E.colors[6].r = 220; E.colors[6].g = 220; E.colors[6].b = 100;
    /* HL_NUMBER */
    E.colors[7].r = 200; E.colors[7].g = 100; E.colors[7].b = 200;
    /* HL_MATCH */
    E.colors[8].r = 100; E.colors[8].g = 150; E.colors[8].b = 220;
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
    E.word_wrap = 1;  /* Word wrap enabled by default */
    E.sel_active = 0;
    E.sel_start_x = E.sel_start_y = 0;
    E.sel_end_x = E.sel_end_y = 0;
    init_default_colors();
    lua_repl_init(&E.repl);
    update_window_size();
    signal(SIGWINCH, handle_sig_win_ch);

    /* Initialize Lua */
    struct loki_lua_opts lua_opts = {
        .bind_editor = 1,
        .bind_http = 1,
        .load_config = 1,
        .config_override = NULL,
        .project_root = NULL,
        .extra_lua_path = NULL,
        .reporter = loki_lua_status_reporter,
        .reporter_userdata = NULL,
    };
    E.L = loki_lua_bootstrap(&lua_opts);
    if (!E.L) {
        editor_set_status_msg("Failed to initialize Lua runtime (%s)", loki_lua_runtime());
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
        fprintf(stderr, "Make sure .loki/init.lua or ~/.loki/init.lua defines this function\n");
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
        fprintf(stderr, "  - Lua callback error (check .loki/init.lua)\n");
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
    printf("Usage: loki [options] <filename>\n");
    printf("\nOptions:\n");
    printf("  --help              Show this help message\n");
    printf("  --version           Show version information\n");
    printf("  --complete <file>   Run AI completion on file and save result\n");
    printf("  --explain <file>    Run AI explanation on file and print to stdout\n");
    printf("\nInteractive mode (default):\n");
    printf("  loki <filename>     Open file in interactive editor\n");
    printf("\nKeybindings in interactive mode:\n");
    printf("  Ctrl-S    Save file\n");
    printf("  Ctrl-Q    Quit\n");
    printf("  Ctrl-F    Find\n");
    printf("  Ctrl-L    Toggle Lua REPL\n");
    printf("\nAI commands require OPENAI_API_KEY environment variable\n");
    printf("and .loki/init.lua or ~/.loki/init.lua configuration.\n");
}

int loki_editor_main(int argc, char **argv) {
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

    /* Check for --version flag */
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("loki %s\n", LOKI_VERSION);
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

    /* Check for unknown options */
    if (argv[1][0] == '-') {
        fprintf(stderr, "Error: Unknown option: %s\n", argv[1]);
        print_usage();
        exit(1);
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
        "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-W = wrap | Ctrl-L = repl | Ctrl-C = copy");
    while(1) {
        handle_windows_resize();
        check_async_requests();  /* Process any pending async HTTP requests */
        editor_refresh_screen();
        editor_process_keypress(STDIN_FILENO);
    }
    return 0;
}
