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
#include "loki_internal.h"

/* libcurl for async HTTP */
#include <curl/curl.h>

/* Global editor state. Note: This makes the editor non-reentrant and
 * non-thread-safe. Only one editor instance can exist per process.
 * During migration to context passing, functions will gradually be updated
 * to take editor_ctx_t* instead of using this global. */
editor_ctx_t E;

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
/* Async HTTP state is in loki_editor.c */
/* REPL layout management is in loki_editor.c */

/* Modal editing functions (implemented below) */
static void process_normal_mode(editor_ctx_t *ctx, int fd, int c);
static void process_insert_mode(editor_ctx_t *ctx, int fd, int c);
static void process_visual_mode(editor_ctx_t *ctx, int fd, int c);

void editor_set_status_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

static void cleanup_dynamic_languages(void);

/* ======================= Context Management =============================== */

/* Initialize an editor context with default values.
 * This allows creating independent editor contexts for split windows
 * and multiple buffer support. */
void editor_ctx_init(editor_ctx_t *ctx) {
    memset(ctx, 0, sizeof(editor_ctx_t));
    ctx->cx = 0;
    ctx->cy = 0;
    ctx->rowoff = 0;
    ctx->coloff = 0;
    ctx->screenrows = 0;
    ctx->screencols = 0;
    ctx->screenrows_total = 0;
    ctx->numrows = 0;
    ctx->rawmode = 0;
    ctx->row = NULL;
    ctx->dirty = 0;
    ctx->filename = NULL;
    ctx->statusmsg[0] = '\0';
    ctx->statusmsg_time = 0;
    ctx->syntax = NULL;
    ctx->L = NULL;
    memset(&ctx->repl, 0, sizeof(t_lua_repl));
    ctx->mode = MODE_NORMAL;
    ctx->word_wrap = 0;
    ctx->sel_active = 0;
    ctx->sel_start_x = 0;
    ctx->sel_start_y = 0;
    ctx->sel_end_x = 0;
    ctx->sel_end_y = 0;
    memset(ctx->colors, 0, sizeof(ctx->colors));
}

/* Free all dynamically allocated memory in a context.
 * This should be called when a context is no longer needed. */
void editor_ctx_free(editor_ctx_t *ctx) {
    /* Free all row data */
    for (int i = 0; i < ctx->numrows; i++) {
        free(ctx->row[i].chars);
        free(ctx->row[i].render);
        free(ctx->row[i].hl);
    }
    free(ctx->row);

    /* Free filename */
    free(ctx->filename);

    /* Note: We don't free ctx->L (Lua state) as it's shared across contexts
     * and managed separately by the editor instance */

    /* Clear the structure */
    memset(ctx, 0, sizeof(editor_ctx_t));
}

/* Check if position is within selection */
int is_selected(editor_ctx_t *ctx, int row, int col) {
    if (!ctx->sel_active) return 0;

    int start_y = ctx->sel_start_y;
    int start_x = ctx->sel_start_x;
    int end_y = ctx->sel_end_y;
    int end_x = ctx->sel_end_x;

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
void copy_selection_to_clipboard(editor_ctx_t *ctx) {
    if (!ctx->sel_active) {
        editor_set_status_msg("No selection");
        return;
    }

    /* Ensure start comes before end */
    int start_y = ctx->sel_start_y;
    int start_x = ctx->sel_start_x;
    int end_y = ctx->sel_end_y;
    int end_x = ctx->sel_end_x;

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

    for (int y = start_y; y <= end_y && y < ctx->numrows; y++) {
        int x_start = (y == start_y) ? start_x : 0;
        int x_end = (y == end_y) ? end_x : ctx->row[y].size;
        if (x_end > ctx->row[y].size) x_end = ctx->row[y].size;

        int len = x_end - x_start;
        if (len > 0) {
            while (text_len + len + 2 > text_capacity) {
                text_capacity *= 2;
                char *new_text = realloc(text, text_capacity);
                if (!new_text) { free(text); return; }
                text = new_text;
            }
            memcpy(text + text_len, ctx->row[y].chars + x_start, len);
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
    ctx->sel_active = 0;  /* Clear selection after copy */
}
/* Async HTTP and cleanup_curl are in loki_editor.c */

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

void disable_raw_mode(editor_ctx_t *ctx, int fd) {
    /* Don't even check the return value as it's too late. */
    if (ctx && ctx->rawmode) {
        tcsetattr(fd,TCSAFLUSH,&orig_termios);
        ctx->rawmode = 0;
    }
}

/* Called at exit to avoid remaining in raw mode. */
void editor_atexit(void) {
    disable_raw_mode(&E, STDIN_FILENO);
    cleanup_dynamic_languages();
    editor_cleanup_resources(&E); /* Clean up Lua, REPL, and CURL (in loki_editor.c) */
}

/* Raw mode: 1960 magic shit. */
int enable_raw_mode(editor_ctx_t *ctx, int fd) {
    struct termios raw;

    if (!ctx) return -1; /* Need valid context */
    if (ctx->rawmode) return 0; /* Already enabled. */
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
    ctx->rawmode = 1;
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
void editor_update_syntax_markdown(editor_ctx_t *ctx, t_erow *row);

/* Map human-readable style names to HL_* constants */
int hl_name_to_code(const char *name) {
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

/* Set every byte of row->hl (that corresponds to every character in the line)
 * to the right syntax highlight type (HL_* defines). */
void editor_update_syntax(editor_ctx_t *ctx, t_erow *row) {
    unsigned char *new_hl = realloc(row->hl,row->rsize);
    if (new_hl == NULL) return; /* Out of memory, keep old highlighting */
    row->hl = new_hl;
    memset(row->hl,HL_NORMAL,row->rsize);

    int default_ran = 0;

    if (ctx->syntax != NULL) {
        if (ctx->syntax->type == HL_TYPE_MARKDOWN) {
            editor_update_syntax_markdown(ctx, row);
            default_ran = 1;
        } else {
            int i, prev_sep, in_string, in_comment;
            char *p;
            char **keywords = ctx->syntax->keywords;
            char *scs = ctx->syntax->singleline_comment_start;
            char *mcs = ctx->syntax->multiline_comment_start;
            char *mce = ctx->syntax->multiline_comment_end;
            char *separators = ctx->syntax->separators;

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
            if (row->idx > 0 && editor_row_has_open_comment(&ctx->row[row->idx-1]))
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

    /* Lua custom highlighting is in loki_editor.c */
    (void)default_ran; /* Suppress unused variable warning */

    /* Propagate syntax change to the next row if the open comment
     * state changed. This may recursively affect all the following rows
     * in the file. */
    int oc = editor_row_has_open_comment(row);
    if (row->hl_oc != oc && row->idx+1 < ctx->numrows)
        editor_update_syntax(ctx, &ctx->row[row->idx+1]);
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

/* Format RGB color escape sequence for syntax highlighting.
 * Uses true color (24-bit) escape codes: ESC[38;2;R;G;Bm
 * Returns the length of the formatted string. */
int editor_format_color(editor_ctx_t *ctx, int hl, char *buf, size_t bufsize) {
    if (hl < 0 || hl >= 9) hl = 0;  /* Default to HL_NORMAL */
    t_hlcolor *color = &ctx->colors[hl];
    return snprintf(buf, bufsize, "\x1b[38;2;%d;%d;%dm",
                    color->r, color->g, color->b);
}

/* Select the syntax highlight scheme depending on the filename,
 * setting it in the global state E.syntax. */
void editor_select_syntax_highlight(editor_ctx_t *ctx, char *filename) {
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct t_editor_syntax *s = HLDB+j;
        unsigned int i = 0;
        while(s->filematch[i]) {
            char *p;
            int patlen = strlen(s->filematch[i]);
            if ((p = strstr(filename,s->filematch[i])) != NULL) {
                if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
                    ctx->syntax = s;
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
                    ctx->syntax = s;
                    return;
                }
            }
            i++;
        }
    }
}

/* Free a single dynamically allocated language definition */
void free_dynamic_language(struct t_editor_syntax *lang) {
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
int add_dynamic_language(struct t_editor_syntax *lang) {
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
void editor_update_row(editor_ctx_t *ctx, t_erow *row) {
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
    editor_update_syntax(ctx, row);
}

/* Insert a row at the specified position, shifting the other rows on the bottom
 * if required. */
void editor_insert_row(editor_ctx_t *ctx, int at, char *s, size_t len) {
    if (at > ctx->numrows) return;
    /* Check for integer overflow in allocation size calculation */
    if ((size_t)ctx->numrows >= SIZE_MAX / sizeof(t_erow)) {
        fprintf(stderr, "Too many rows, cannot allocate more memory\n");
        exit(1);
    }
    t_erow *new_row = realloc(ctx->row,sizeof(t_erow)*(ctx->numrows+1));
    if (new_row == NULL) {
        perror("Out of memory");
        exit(1);
    }
    ctx->row = new_row;
    if (at != ctx->numrows) {
        memmove(ctx->row+at+1,ctx->row+at,sizeof(ctx->row[0])*(ctx->numrows-at));
        for (int j = at+1; j <= ctx->numrows; j++) ctx->row[j].idx++;
    }
    ctx->row[at].size = len;
    ctx->row[at].chars = malloc(len+1);
    if (ctx->row[at].chars == NULL) {
        perror("Out of memory");
        exit(1);
    }
    memcpy(ctx->row[at].chars,s,len+1);
    ctx->row[at].hl = NULL;
    ctx->row[at].hl_oc = 0;
    ctx->row[at].cb_lang = CB_LANG_NONE;
    ctx->row[at].render = NULL;
    ctx->row[at].rsize = 0;
    ctx->row[at].idx = at;
    editor_update_row(ctx, ctx->row+at);
    ctx->numrows++;
    ctx->dirty++;
}

/* Free row's heap allocated stuff. */
void editor_free_row(t_erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

/* Remove the row at the specified position, shifting the remaining on the
 * top. */
void editor_del_row(editor_ctx_t *ctx, int at) {
    t_erow *row;

    if (at >= ctx->numrows) return;
    row = ctx->row+at;
    editor_free_row(row);
    memmove(ctx->row+at,ctx->row+at+1,sizeof(ctx->row[0])*(ctx->numrows-at-1));
    for (int j = at; j < ctx->numrows-1; j++) ctx->row[j].idx++;
    ctx->numrows--;
    ctx->dirty++;
}

/* Turn the editor rows into a single heap-allocated string.
 * Returns the pointer to the heap-allocated string and populate the
 * integer pointed by 'buflen' with the size of the string, excluding
 * the final nulterm. */
char *editor_rows_to_string(editor_ctx_t *ctx, int *buflen) {
    char *buf = NULL, *p;
    int totlen = 0;
    int j;

    /* Compute count of bytes */
    for (j = 0; j < ctx->numrows; j++)
        totlen += ctx->row[j].size+1; /* +1 is for "\n" at end of every row */
    *buflen = totlen;
    totlen++; /* Also make space for nulterm */

    p = buf = malloc(totlen);
    if (buf == NULL) return NULL;
    for (j = 0; j < ctx->numrows; j++) {
        memcpy(p,ctx->row[j].chars,ctx->row[j].size);
        p += ctx->row[j].size;
        *p = '\n';
        p++;
    }
    *p = '\0';
    return buf;
}

/* Insert a character at the specified position in a row, moving the remaining
 * chars on the right if needed. */
void editor_row_insert_char(editor_ctx_t *ctx, t_erow *row, int at, int c) {
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
    editor_update_row(ctx, row);
    ctx->dirty++;
}

/* Append the string 's' at the end of a row */
void editor_row_append_string(editor_ctx_t *ctx, t_erow *row, char *s, size_t len) {
    char *new_chars = realloc(row->chars,row->size+len+1);
    if (new_chars == NULL) {
        perror("Out of memory");
        exit(1);
    }
    row->chars = new_chars;
    memcpy(row->chars+row->size,s,len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(ctx, row);
    ctx->dirty++;
}

/* Delete the character at offset 'at' from the specified row. */
void editor_row_del_char(editor_ctx_t *ctx, t_erow *row, int at) {
    if (row->size <= at) return;
    /* Include null terminator in move (+1 for the null byte) */
    memmove(row->chars+at,row->chars+at+1,row->size-at+1);
    row->size--;
    editor_update_row(ctx, row);
    ctx->dirty++;
}

/* Insert the specified char at the current prompt position. */
void editor_insert_char(editor_ctx_t *ctx, int c) {
    int filerow = ctx->rowoff+ctx->cy;
    int filecol = ctx->coloff+ctx->cx;
    t_erow *row = (filerow >= ctx->numrows) ? NULL : &ctx->row[filerow];

    /* If the row where the cursor is currently located does not exist in our
     * logical representaion of the file, add enough empty rows as needed. */
    if (!row) {
        while(ctx->numrows <= filerow)
            editor_insert_row(ctx, ctx->numrows,"",0);
    }
    row = &ctx->row[filerow];
    editor_row_insert_char(ctx, row,filecol,c);
    if (ctx->cx == ctx->screencols-1)
        ctx->coloff++;
    else
        ctx->cx++;
    ctx->dirty++;
}

/* Inserting a newline is slightly complex as we have to handle inserting a
 * newline in the middle of a line, splitting the line as needed. */
void editor_insert_newline(editor_ctx_t *ctx) {
    int filerow = ctx->rowoff+ctx->cy;
    int filecol = ctx->coloff+ctx->cx;
    t_erow *row = (filerow >= ctx->numrows) ? NULL : &ctx->row[filerow];

    if (!row) {
        if (filerow == ctx->numrows) {
            editor_insert_row(ctx, filerow,"",0);
            goto fixcursor;
        }
        return;
    }
    /* If the cursor is over the current line size, we want to conceptually
     * think it's just over the last character. */
    if (filecol >= row->size) filecol = row->size;
    if (filecol == 0) {
        editor_insert_row(ctx, filerow,"",0);
    } else {
        /* We are in the middle of a line. Split it between two rows. */
        editor_insert_row(ctx, filerow+1,row->chars+filecol,row->size-filecol);
        row = &ctx->row[filerow];
        row->chars[filecol] = '\0';
        row->size = filecol;
        editor_update_row(ctx, row);
    }
fixcursor:
    if (ctx->cy == ctx->screenrows-1) {
        ctx->rowoff++;
    } else {
        ctx->cy++;
    }
    ctx->cx = 0;
    ctx->coloff = 0;
}

/* Delete the char at the current prompt position. */
void editor_del_char(editor_ctx_t *ctx) {
    int filerow = ctx->rowoff+ctx->cy;
    int filecol = ctx->coloff+ctx->cx;
    t_erow *row = (filerow >= ctx->numrows) ? NULL : &ctx->row[filerow];

    if (!row || (filecol == 0 && filerow == 0)) return;
    if (filecol == 0) {
        /* Handle the case of column 0, we need to move the current line
         * on the right of the previous one. */
        filecol = ctx->row[filerow-1].size;
        editor_row_append_string(ctx, &ctx->row[filerow-1],row->chars,row->size);
        editor_del_row(ctx, filerow);
        row = NULL;
        if (ctx->cy == 0)
            ctx->rowoff--;
        else
            ctx->cy--;
        ctx->cx = filecol;
        if (ctx->cx >= ctx->screencols) {
            int shift = (ctx->cx-ctx->screencols)+1;
            ctx->cx -= shift;
            ctx->coloff += shift;
        }
    } else {
        editor_row_del_char(ctx, row,filecol-1);
        if (ctx->cx == 0 && ctx->coloff)
            ctx->coloff--;
        else
            ctx->cx--;
    }
    if (row) editor_update_row(ctx, row);
    ctx->dirty++;
}

/* Load the specified program in the editor memory and returns 0 on success
 * or 1 on error. */
int editor_open(editor_ctx_t *ctx, char *filename) {
    FILE *fp;

    ctx->dirty = 0;
    free(ctx->filename);
    size_t fnlen = strlen(filename)+1;
    ctx->filename = malloc(fnlen);
    if (ctx->filename == NULL) {
        perror("Out of memory");
        exit(1);
    }
    memcpy(ctx->filename,filename,fnlen);

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
        editor_insert_row(ctx, ctx->numrows,line,linelen);
    }
    free(line);
    fclose(fp);
    ctx->dirty = 0;
    return 0;
}

/* Save the current file on disk. Return 0 on success, 1 on error. */
int editor_save(editor_ctx_t *ctx) {
    int len;
    char *buf = editor_rows_to_string(ctx, &len);
    if (buf == NULL) {
        editor_set_status_msg("Can't save! Out of memory");
        return 1;
    }
    int fd = open(ctx->filename,O_RDWR|O_CREAT,0644);
    if (fd == -1) goto writeerr;

    /* Use truncate + a single write(2) call in order to make saving
     * a bit safer, under the limits of what we can do in a small editor. */
    if (ftruncate(fd,len) == -1) goto writeerr;
    if (write(fd,buf,len) != len) goto writeerr;

    close(fd);
    free(buf);
    ctx->dirty = 0;
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
/* Note: struct abuf is now defined in loki_internal.h */

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
void editor_refresh_screen(editor_ctx_t *ctx) {
    int y;
    t_erow *r;
    char buf[32];
    struct abuf ab = ABUF_INIT;

    ab_append(&ab,"\x1b[?25l",6); /* Hide cursor. */
    ab_append(&ab,"\x1b[H",3); /* Go home. */
    for (y = 0; y < ctx->screenrows; y++) {
        int filerow = ctx->rowoff+y;

        if (filerow >= ctx->numrows) {
            if (ctx->numrows == 0 && y == ctx->screenrows/3) {
                char welcome[80];
                int welcomelen = snprintf(welcome,sizeof(welcome),
                    "Kilo editor -- version %s\x1b[0K\r\n", LOKI_VERSION);
                int padding = (ctx->screencols-welcomelen)/2;
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

        r = &ctx->row[filerow];

        int len = r->rsize - ctx->coloff;
        int current_color = -1;

        /* Word wrap: clamp to screen width and find word boundary */
        if (ctx->word_wrap && len > ctx->screencols && r->cb_lang == CB_LANG_NONE) {
            len = ctx->screencols;
            /* Find last space/separator to break at word boundary */
            int last_space = -1;
            for (int k = 0; k < len; k++) {
                if (isspace(r->render[ctx->coloff + k])) {
                    last_space = k;
                }
            }
            if (last_space > 0 && last_space > len / 2) {
                len = last_space + 1; /* Include the space */
            }
        }

        if (len > 0) {
            if (len > ctx->screencols) len = ctx->screencols;
            char *c = r->render+ctx->coloff;
            unsigned char *hl = r->hl+ctx->coloff;
            int j;
            for (j = 0; j < len; j++) {
                int selected = is_selected(ctx, filerow, ctx->coloff + j);

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
                        int clen = editor_format_color(ctx, current_color, buf, sizeof(buf));
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
                        int clen = editor_format_color(ctx, color, buf, sizeof(buf));
                        current_color = color;
                        ab_append(&ab,buf,clen);
                    }
                    ab_append(&ab,c+j,1);
                    if (selected) {
                        ab_append(&ab,"\x1b[0m",4); /* Reset */
                        if (current_color != -1) {
                            char buf[32];
                            int clen = editor_format_color(ctx, current_color, buf, sizeof(buf));
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

    /* Get mode indicator */
    const char *mode_str = "";
    switch(ctx->mode) {
        case MODE_NORMAL: mode_str = "NORMAL"; break;
        case MODE_INSERT: mode_str = "INSERT"; break;
        case MODE_VISUAL: mode_str = "VISUAL"; break;
        case MODE_COMMAND: mode_str = "COMMAND"; break;
    }

    int len = snprintf(status, sizeof(status), " %s  %.20s - %d lines %s",
        mode_str, ctx->filename, ctx->numrows, ctx->dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus),
        "%d/%d",ctx->rowoff+ctx->cy+1,ctx->numrows);
    if (len > ctx->screencols) len = ctx->screencols;
    ab_append(&ab,status,len);
    while(len < ctx->screencols) {
        if (ctx->screencols - len == rlen) {
            ab_append(&ab,rstatus,rlen);
            break;
        } else {
            ab_append(&ab," ",1);
            len++;
        }
    }
    ab_append(&ab,"\x1b[0m\r\n",6);

    /* Second row depends on ctx->statusmsg and the status message update time. */
    ab_append(&ab,"\x1b[0K",4);
    int msglen = strlen(ctx->statusmsg);
    if (msglen && time(NULL)-ctx->statusmsg_time < 5)
        ab_append(&ab,ctx->statusmsg,msglen <= ctx->screencols ? msglen : ctx->screencols);

    /* Render REPL if active */
    if (ctx->repl.active) lua_repl_render(ctx, &ab);

    /* Put cursor at its current position. Note that the horizontal position
     * at which the cursor is displayed may be different compared to 'ctx->cx'
     * because of TABs. */
    int cursor_row = 1;
    int cursor_col = 1;

    /* Calculate cursor position - different for REPL vs editor mode */
    if (ctx->repl.active) {
        /* REPL mode: cursor is on the REPL prompt line */
        int prompt_len = (int)strlen(LUA_REPL_PROMPT);
        int visible = ctx->repl.input_len;
        if (prompt_len + visible >= ctx->screencols) {
            visible = ctx->screencols > prompt_len ? ctx->screencols - prompt_len : 0;
        }
        cursor_row = ctx->screenrows + STATUS_ROWS + LUA_REPL_OUTPUT_ROWS + 1;
        cursor_col = prompt_len + visible + 1;
        if (cursor_col < 1) cursor_col = 1;
        if (cursor_col > ctx->screencols) cursor_col = ctx->screencols;
    } else {
        /* Editor mode: cursor is in the text area */
        int cx = 1;
        int filerow = ctx->rowoff+ctx->cy;
        t_erow *row = (filerow >= ctx->numrows) ? NULL : &ctx->row[filerow];
        if (row) {
            for (int j = ctx->coloff; j < (ctx->cx+ctx->coloff); j++) {
                if (j < row->size && row->chars[j] == TAB)
                    cx += 7-((cx)%8);
                cx++;
            }
        }
        cursor_row = ctx->cy + 1;
        cursor_col = cx;
        if (cursor_col > ctx->screencols) cursor_col = ctx->screencols;
    }

    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",cursor_row,cursor_col);
    ab_append(&ab,buf,strlen(buf));
    ab_append(&ab,"\x1b[?25h",6); /* Show cursor. */
    write(STDOUT_FILENO,ab.b,ab.len);
    ab_free(&ab);
}

/* REPL layout management, toggle function, and status reporter are in loki_editor.c */

/* Initialize default syntax highlighting colors.
 * Colors are stored as RGB values and rendered using true color escape codes.
 * These defaults match the visual appearance of the original ANSI color scheme. */
void init_default_colors(editor_ctx_t *ctx) {
    /* HL_NORMAL */
    ctx->colors[0].r = 200; ctx->colors[0].g = 200; ctx->colors[0].b = 200;
    /* HL_NONPRINT */
    ctx->colors[1].r = 100; ctx->colors[1].g = 100; ctx->colors[1].b = 100;
    /* HL_COMMENT */
    ctx->colors[2].r = 100; ctx->colors[2].g = 100; ctx->colors[2].b = 100;
    /* HL_MLCOMMENT */
    ctx->colors[3].r = 100; ctx->colors[3].g = 100; ctx->colors[3].b = 100;
    /* HL_KEYWORD1 */
    ctx->colors[4].r = 220; ctx->colors[4].g = 100; ctx->colors[4].b = 220;
    /* HL_KEYWORD2 */
    ctx->colors[5].r = 100; ctx->colors[5].g = 220; ctx->colors[5].b = 220;
    /* HL_STRING */
    ctx->colors[6].r = 220; ctx->colors[6].g = 220; ctx->colors[6].b = 100;
    /* HL_NUMBER */
    ctx->colors[7].r = 200; ctx->colors[7].g = 100; ctx->colors[7].b = 200;
    /* HL_MATCH */
    ctx->colors[8].r = 100; ctx->colors[8].g = 150; ctx->colors[8].b = 220;
}

/* Update window size and adjust screen layout */
void update_window_size(editor_ctx_t *ctx) {
    int rows, cols;
    if (get_window_size(STDIN_FILENO,STDOUT_FILENO,
                      &rows,&cols) == -1) {
        /* If we can't get terminal size (e.g., non-interactive mode), use defaults */
        rows = 24;
        cols = 80;
    }
    ctx->screencols = cols;
    rows -= STATUS_ROWS;
    if (rows < 1) rows = 1;
    ctx->screenrows_total = rows;
    /* REPL layout update (editor_update_repl_layout) is in loki_editor.c */
    ctx->screenrows = ctx->screenrows_total; /* Without REPL, use all available rows */
}

/* Signal handler for window size changes */
void handle_sig_win_ch(int unused __attribute__((unused))) {
    /* Signal handler must be async-signal-safe.
     * Just set a flag and handle resize in main loop. */
    winsize_changed = 1;
}

/* Check and handle window resize */
void handle_windows_resize(editor_ctx_t *ctx) {
    if (winsize_changed) {
        winsize_changed = 0;
        update_window_size(ctx);
        if (ctx->cy > ctx->screenrows) ctx->cy = ctx->screenrows - 1;
        if (ctx->cx > ctx->screencols) ctx->cx = ctx->screencols - 1;
    }
}

/* Number of times CTRL-Q must be pressed before actually quitting */
#define KILO_QUIT_TIMES 3

/* Maximum search query length */
#define KILO_QUERY_LEN 256

/* ========================= Modal Editing ================================= */

/* Helper: Check if a line is empty (blank or whitespace only) */
static int is_empty_line(editor_ctx_t *ctx, int row) {
    if (row < 0 || row >= ctx->numrows) return 1;
    t_erow *line = &ctx->row[row];
    for (int i = 0; i < line->size; i++) {
        if (line->chars[i] != ' ' && line->chars[i] != '\t') {
            return 0;
        }
    }
    return 1;
}

/* Move to next empty line (paragraph motion: }) */
static void move_to_next_empty_line(editor_ctx_t *ctx) {
    int filerow = ctx->rowoff + ctx->cy;

    /* Skip current paragraph (non-empty lines) */
    int row = filerow + 1;
    while (row < ctx->numrows && !is_empty_line(ctx, row)) {
        row++;
    }

    /* Skip empty lines to find start of next paragraph or stay at first empty */
    if (row < ctx->numrows) {
        /* Found an empty line - this is where we stop */
        filerow = row;
    } else {
        /* No empty line found, go to end of file */
        filerow = ctx->numrows - 1;
    }

    /* Update cursor position */
    if (filerow < ctx->rowoff) {
        ctx->rowoff = filerow;
        ctx->cy = 0;
    } else if (filerow >= ctx->rowoff + ctx->screenrows) {
        ctx->rowoff = filerow - ctx->screenrows + 1;
        ctx->cy = ctx->screenrows - 1;
    } else {
        ctx->cy = filerow - ctx->rowoff;
    }

    /* Move to start of line */
    ctx->cx = 0;
    ctx->coloff = 0;
}

/* Move to previous empty line (paragraph motion: {) */
static void move_to_prev_empty_line(editor_ctx_t *ctx) {
    int filerow = ctx->rowoff + ctx->cy;

    /* Skip current paragraph (non-empty lines) going backward */
    int row = filerow - 1;
    while (row >= 0 && !is_empty_line(ctx, row)) {
        row--;
    }

    /* Found an empty line - this is where we stop */
    if (row >= 0) {
        filerow = row;
    } else {
        /* No empty line found, go to start of file */
        filerow = 0;
    }

    /* Update cursor position */
    if (filerow < ctx->rowoff) {
        ctx->rowoff = filerow;
        ctx->cy = 0;
    } else if (filerow >= ctx->rowoff + ctx->screenrows) {
        ctx->rowoff = filerow - ctx->screenrows + 1;
        ctx->cy = ctx->screenrows - 1;
    } else {
        ctx->cy = filerow - ctx->rowoff;
    }

    /* Move to start of line */
    ctx->cx = 0;
    ctx->coloff = 0;
}

/* ========================= Editor events handling  ======================== */

/* Handle cursor position change because arrow keys were pressed. */
void editor_move_cursor(editor_ctx_t *ctx, int key) {
    int filerow = ctx->rowoff+ctx->cy;
    int filecol = ctx->coloff+ctx->cx;
    int rowlen;
    t_erow *row = (filerow >= ctx->numrows) ? NULL : &ctx->row[filerow];

    switch(key) {
    case ARROW_LEFT:
        if (ctx->cx == 0) {
            if (ctx->coloff) {
                ctx->coloff--;
            } else {
                if (filerow > 0) {
                    ctx->cy--;
                    ctx->cx = ctx->row[filerow-1].size;
                    if (ctx->cx > ctx->screencols-1) {
                        ctx->coloff = ctx->cx-ctx->screencols+1;
                        ctx->cx = ctx->screencols-1;
                    }
                }
            }
        } else {
            ctx->cx -= 1;
        }
        break;
    case ARROW_RIGHT:
        if (row && filecol < row->size) {
            if (ctx->cx == ctx->screencols-1) {
                ctx->coloff++;
            } else {
                ctx->cx += 1;
            }
        } else if (row && filecol == row->size) {
            ctx->cx = 0;
            ctx->coloff = 0;
            if (ctx->cy == ctx->screenrows-1) {
                ctx->rowoff++;
            } else {
                ctx->cy += 1;
            }
        }
        break;
    case ARROW_UP:
        if (ctx->cy == 0) {
            if (ctx->rowoff) ctx->rowoff--;
        } else {
            ctx->cy -= 1;
        }
        break;
    case ARROW_DOWN:
        if (filerow < ctx->numrows) {
            if (ctx->cy == ctx->screenrows-1) {
                ctx->rowoff++;
            } else {
                ctx->cy += 1;
            }
        }
        break;
    }
    /* Fix cx if the current line has not enough chars. */
    filerow = ctx->rowoff+ctx->cy;
    filecol = ctx->coloff+ctx->cx;
    row = (filerow >= ctx->numrows) ? NULL : &ctx->row[filerow];
    rowlen = row ? row->size : 0;
    if (filecol > rowlen) {
        ctx->cx -= filecol-rowlen;
        if (ctx->cx < 0) {
            ctx->coloff += ctx->cx;
            ctx->cx = 0;
        }
    }
}

/* Find and highlight text in the buffer */
void editor_find(editor_ctx_t *ctx, int fd) {
    char query[KILO_QUERY_LEN+1] = {0};
    int qlen = 0;
    int last_match = -1; /* Last line where a match was found. -1 for none. */
    int find_next = 0; /* if 1 search next, if -1 search prev. */
    int saved_hl_line = -1;  /* No saved HL */
    char *saved_hl = NULL;

#define FIND_RESTORE_HL do { \
    if (saved_hl) { \
        memcpy(ctx->row[saved_hl_line].hl,saved_hl, ctx->row[saved_hl_line].rsize); \
        free(saved_hl); \
        saved_hl = NULL; \
    } \
} while (0)

    /* Save the cursor position in order to restore it later. */
    int saved_cx = ctx->cx, saved_cy = ctx->cy;
    int saved_coloff = ctx->coloff, saved_rowoff = ctx->rowoff;

    while(1) {
        editor_set_status_msg(
            "Search: %s (Use ESC/Arrows/Enter)", query);
        editor_refresh_screen(ctx);

        int c = editor_read_key(fd);
        if (c == DEL_KEY || c == CTRL_H || c == BACKSPACE) {
            if (qlen != 0) query[--qlen] = '\0';
            last_match = -1;
        } else if (c == ESC || c == ENTER) {
            if (c == ESC) {
                ctx->cx = saved_cx; ctx->cy = saved_cy;
                ctx->coloff = saved_coloff; ctx->rowoff = saved_rowoff;
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

            for (i = 0; i < ctx->numrows; i++) {
                current += find_next;
                if (current == -1) current = ctx->numrows-1;
                else if (current == ctx->numrows) current = 0;
                match = strstr(ctx->row[current].render,query);
                if (match) {
                    match_offset = match-ctx->row[current].render;
                    break;
                }
            }
            find_next = 0;

            /* Highlight */
            FIND_RESTORE_HL;

            if (match) {
                t_erow *row = &ctx->row[current];
                last_match = current;
                if (row->hl) {
                    saved_hl_line = current;
                    saved_hl = malloc(row->rsize);
                    if (saved_hl) {
                        memcpy(saved_hl,row->hl,row->rsize);
                    }
                    memset(row->hl+match_offset,HL_MATCH,qlen);
                }
                ctx->cy = 0;
                ctx->cx = match_offset;
                ctx->rowoff = current;
                ctx->coloff = 0;
                /* Scroll horizontally as needed. */
                if (ctx->cx > ctx->screencols) {
                    int diff = ctx->cx - ctx->screencols;
                    ctx->cx -= diff;
                    ctx->coloff += diff;
                }
            }
        }
    }
}

/* ========================= Modal Key Processing ============================ */

/* Process normal mode keypresses */
static void process_normal_mode(editor_ctx_t *ctx, int fd, int c) {
    switch(c) {
        case 'h': editor_move_cursor(ctx, ARROW_LEFT); break;
        case 'j': editor_move_cursor(ctx, ARROW_DOWN); break;
        case 'k': editor_move_cursor(ctx, ARROW_UP); break;
        case 'l': editor_move_cursor(ctx, ARROW_RIGHT); break;

        /* Paragraph motion */
        case '{':
            move_to_prev_empty_line(ctx);
            break;
        case '}':
            move_to_next_empty_line(ctx);
            break;

        /* Enter insert mode */
        case 'i': ctx->mode = MODE_INSERT; break;
        case 'a':
            editor_move_cursor(ctx, ARROW_RIGHT);
            ctx->mode = MODE_INSERT;
            break;
        case 'o':
            /* Insert line below and enter insert mode */
            if (ctx->numrows > 0) {
                int filerow = ctx->rowoff + ctx->cy;
                if (filerow < ctx->numrows) {
                    ctx->cx = ctx->row[filerow].size; /* Move to end of line */
                }
            }
            editor_insert_newline(ctx);
            ctx->mode = MODE_INSERT;
            break;
        case 'O':
            /* Insert line above and enter insert mode */
            ctx->cx = 0; /* Move to start of line */
            editor_insert_newline(ctx);
            editor_move_cursor(ctx, ARROW_UP);
            ctx->mode = MODE_INSERT;
            break;

        /* Enter visual mode */
        case 'v':
            ctx->mode = MODE_VISUAL;
            ctx->sel_active = 1;
            ctx->sel_start_x = ctx->cx;
            ctx->sel_start_y = ctx->cy;
            ctx->sel_end_x = ctx->cx;
            ctx->sel_end_y = ctx->cy;
            break;

        /* Delete character */
        case 'x':
            editor_del_char(ctx);
            break;

        /* Global commands (work in all modes) */
        case CTRL_S: editor_save(ctx); break;
        case CTRL_F: editor_find(ctx, fd); break;
        case CTRL_L:
            /* Toggle REPL */
            ctx->repl.active = !ctx->repl.active;
            editor_update_repl_layout(ctx);
            if (ctx->repl.active) {
                editor_set_status_msg("Lua REPL active (Ctrl-L or ESC to close)");
            }
            break;
        case CTRL_Q:
            /* Handle quit in main function for consistency */
            break;

        /* Arrow keys */
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editor_move_cursor(ctx, c);
            break;

        default:
            /* Beep or show message for unknown command */
            editor_set_status_msg("Unknown command");
            break;
    }
}

/* Process insert mode keypresses */
static void process_insert_mode(editor_ctx_t *ctx, int fd, int c) {
    switch(c) {
        case ESC:
            ctx->mode = MODE_NORMAL;
            /* Move cursor left if not at start of line */
            if (ctx->cx > 0 || ctx->coloff > 0) {
                editor_move_cursor(ctx, ARROW_LEFT);
            }
            break;

        case ENTER:
            editor_insert_newline(ctx);
            break;

        case BACKSPACE:
        case CTRL_H:
        case DEL_KEY:
            editor_del_char(ctx);
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editor_move_cursor(ctx, c);
            break;

        /* Global commands */
        case CTRL_S: editor_save(ctx); break;
        case CTRL_F: editor_find(ctx, fd); break;
        case CTRL_W:
            ctx->word_wrap = !ctx->word_wrap;
            editor_set_status_msg("Word wrap %s", ctx->word_wrap ? "enabled" : "disabled");
            break;
        case CTRL_L:
            /* Toggle REPL */
            ctx->repl.active = !ctx->repl.active;
            editor_update_repl_layout(ctx);
            if (ctx->repl.active) {
                editor_set_status_msg("Lua REPL active (Ctrl-L or ESC to close)");
            }
            break;
        case CTRL_C:
            copy_selection_to_clipboard(ctx);
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            if (c == PAGE_UP && ctx->cy != 0)
                ctx->cy = 0;
            else if (c == PAGE_DOWN && ctx->cy != ctx->screenrows-1)
                ctx->cy = ctx->screenrows-1;
            {
                int times = ctx->screenrows;
                while(times--)
                    editor_move_cursor(ctx, c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case SHIFT_ARROW_UP:
        case SHIFT_ARROW_DOWN:
        case SHIFT_ARROW_LEFT:
        case SHIFT_ARROW_RIGHT:
            /* Start selection if not active */
            if (!ctx->sel_active) {
                ctx->sel_active = 1;
                ctx->sel_start_x = ctx->cx;
                ctx->sel_start_y = ctx->cy;
            }
            /* Move cursor */
            if (c == SHIFT_ARROW_UP) editor_move_cursor(ctx, ARROW_UP);
            else if (c == SHIFT_ARROW_DOWN) editor_move_cursor(ctx, ARROW_DOWN);
            else if (c == SHIFT_ARROW_LEFT) editor_move_cursor(ctx, ARROW_LEFT);
            else if (c == SHIFT_ARROW_RIGHT) editor_move_cursor(ctx, ARROW_RIGHT);
            /* Update selection end */
            ctx->sel_end_x = ctx->cx;
            ctx->sel_end_y = ctx->cy;
            break;

        default:
            /* Insert the character */
            editor_insert_char(ctx, c);
            break;
    }
}

/* Process visual mode keypresses */
static void process_visual_mode(editor_ctx_t *ctx, int fd, int c) {
    switch(c) {
        case ESC:
            ctx->mode = MODE_NORMAL;
            ctx->sel_active = 0;
            break;

        /* Movement extends selection */
        case 'h':
        case ARROW_LEFT:
            editor_move_cursor(ctx, ARROW_LEFT);
            ctx->sel_end_x = ctx->cx;
            ctx->sel_end_y = ctx->cy;
            break;

        case 'j':
        case ARROW_DOWN:
            editor_move_cursor(ctx, ARROW_DOWN);
            ctx->sel_end_x = ctx->cx;
            ctx->sel_end_y = ctx->cy;
            break;

        case 'k':
        case ARROW_UP:
            editor_move_cursor(ctx, ARROW_UP);
            ctx->sel_end_x = ctx->cx;
            ctx->sel_end_y = ctx->cy;
            break;

        case 'l':
        case ARROW_RIGHT:
            editor_move_cursor(ctx, ARROW_RIGHT);
            ctx->sel_end_x = ctx->cx;
            ctx->sel_end_y = ctx->cy;
            break;

        /* Copy selection */
        case 'y':
            copy_selection_to_clipboard(ctx);
            ctx->mode = MODE_NORMAL;
            ctx->sel_active = 0;
            editor_set_status_msg("Yanked selection");
            break;

        /* Delete selection (without undo for now) */
        case 'd':
        case 'x':
            copy_selection_to_clipboard(ctx); /* Save to clipboard first */
            /* TODO: delete selection - need to implement this */
            editor_set_status_msg("Delete not implemented yet");
            ctx->mode = MODE_NORMAL;
            ctx->sel_active = 0;
            break;

        /* Global commands */
        case CTRL_C:
            copy_selection_to_clipboard(ctx);
            break;

        default:
            /* Unknown command - beep */
            editor_set_status_msg("Unknown visual command");
            break;
    }
    (void)fd; /* Unused */
}

/* Process a single keypress */
void editor_process_keypress(editor_ctx_t *ctx, int fd) {
    /* When the file is modified, requires Ctrl-q to be pressed N times
     * before actually quitting. */
    static int quit_times = KILO_QUIT_TIMES;

    int c = editor_read_key(fd);

    /* REPL keypress handling */
    if (ctx->repl.active) {
        lua_repl_handle_keypress(ctx, c);
        return;
    }

    /* Handle quit globally (works in all modes) */
    if (c == CTRL_Q) {
        if (ctx->dirty && quit_times) {
            editor_set_status_msg("WARNING!!! File has unsaved changes. "
                "Press Ctrl-Q %d more times to quit.", quit_times);
            quit_times--;
            return;
        }
        exit(0);
    }

    /* Dispatch to mode-specific handler */
    switch(ctx->mode) {
        case MODE_NORMAL:
            process_normal_mode(ctx, fd, c);
            break;
        case MODE_INSERT:
            process_insert_mode(ctx, fd, c);
            break;
        case MODE_VISUAL:
            process_visual_mode(ctx, fd, c);
            break;
        case MODE_COMMAND:
            /* TODO: implement command mode */
            break;
    }

    quit_times = KILO_QUIT_TIMES; /* Reset it to the original value. */
}

void init_editor(editor_ctx_t *ctx) {
    ctx->cx = 0;
    ctx->cy = 0;
    ctx->rowoff = 0;
    ctx->coloff = 0;
    ctx->numrows = 0;
    ctx->row = NULL;
    ctx->dirty = 0;
    ctx->filename = NULL;
    ctx->syntax = NULL;
    ctx->mode = MODE_NORMAL;  /* Start in normal mode (vim-like) */
    ctx->word_wrap = 1;  /* Word wrap enabled by default */
    ctx->sel_active = 0;
    ctx->sel_start_x = ctx->sel_start_y = 0;
    ctx->sel_end_x = ctx->sel_end_y = 0;
    init_default_colors(ctx);
    /* Lua REPL init and Lua initialization are in loki_editor.c */
    update_window_size(ctx);
    signal(SIGWINCH, handle_sig_win_ch);
}

/* AI command execution is in loki_editor.c */
/* Run AI command in non-interactive mode */
/*
static int run_ai_command(char *filename, const char *command) {
    init_editor();

    (void)filename;
    (void)command;
    fprintf(stderr, "Error: AI commands require Lua support\n");
    return 1;
}
*/

/* Main editor entry point moved to loki_editor.c */
