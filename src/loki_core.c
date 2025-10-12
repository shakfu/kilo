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
#include "loki_selection.h"
#include "loki_search.h"
#include "loki_modal.h"
#include "loki_terminal.h"

/* libcurl for async HTTP */
#include <curl/curl.h>

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

void editor_set_status_msg(editor_ctx_t *ctx, const char *fmt, ...) {
    if (!ctx) return;
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(ctx->statusmsg,sizeof(ctx->statusmsg),fmt,ap);
    va_end(ap);
    ctx->statusmsg_time = time(NULL);
}

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
    /* Async HTTP state - already zeroed by memset above */
    ctx->num_pending_http = 0;
    /* Window resize flag - already zeroed by memset above */
    ctx->winsize_changed = 0;
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

/* =========================== Syntax highlights DB =========================
 *
 * Built-in language definitions are in loki_languages.c.
 * Dynamic languages can be registered via loki.register_language() in Lua.
 */

#include "loki_languages.h"

/* Static pointer to editor context for atexit cleanup.
 * Set by init_editor() before registering atexit handler. */
static editor_ctx_t *editor_for_atexit = NULL;

/* Called at exit to avoid remaining in raw mode. */
void editor_atexit(void) {
    if (editor_for_atexit) {
        terminal_disable_raw_mode(editor_for_atexit, STDIN_FILENO);
        editor_cleanup_resources(editor_for_atexit);
    }
    cleanup_dynamic_languages();
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

/* Format RGB color escape sequence for syntax highlighting.
 * Uses true color (24-bit) escape codes: ESC[38;2;R;G;Bm
 * Returns the length of the formatted string. */
int editor_format_color(editor_ctx_t *ctx, int hl, char *buf, size_t bufsize) {
    if (hl < 0 || hl >= 9) hl = 0;  /* Default to HL_NORMAL */
    t_hlcolor *color = &ctx->colors[hl];
    return snprintf(buf, bufsize, "\x1b[38;2;%d;%d;%dm",
                    color->r, color->g, color->b);
}

/* Select the syntax highlight scheme depending on the filename. */
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
    int dynamic_count = get_dynamic_language_count();
    for (int j = 0; j < dynamic_count; j++) {
        struct t_editor_syntax *s = get_dynamic_language(j);
        if (!s) continue;
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
            editor_set_status_msg(ctx, "Cannot open binary file");
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
        editor_set_status_msg(ctx, "Can't save! Out of memory");
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
    editor_set_status_msg(ctx, "%d bytes written on disk", len);
    return 0;

writeerr:
    free(buf);
    if (fd != -1) close(fd);
    editor_set_status_msg(ctx, "Can't save! I/O error: %s",strerror(errno));
    return 1;
}

/* ============================= Terminal update ============================ */

/* Screen buffer functions are now in loki_terminal.c */

/* This function writes the whole screen using VT100 escape characters
 * starting from the logical state of the editor in the global state 'E'. */
void editor_refresh_screen(editor_ctx_t *ctx) {
    int y;
    t_erow *r;
    char buf[32];
    struct abuf ab = ABUF_INIT;

    terminal_buffer_append(&ab,"\x1b[?25l",6); /* Hide cursor. */
    terminal_buffer_append(&ab,"\x1b[H",3); /* Go home. */
    for (y = 0; y < ctx->screenrows; y++) {
        int filerow = ctx->rowoff+y;

        if (filerow >= ctx->numrows) {
            if (ctx->numrows == 0 && y == ctx->screenrows/3) {
                char welcome[80];
                int welcomelen = snprintf(welcome,sizeof(welcome),
                    "Kilo editor -- version %s\x1b[0K\r\n", LOKI_VERSION);
                int padding = (ctx->screencols-welcomelen)/2;
                if (padding) {
                    terminal_buffer_append(&ab,"~",1);
                    padding--;
                }
                while(padding--) terminal_buffer_append(&ab," ",1);
                terminal_buffer_append(&ab,welcome,welcomelen);
            } else {
                terminal_buffer_append(&ab,"~\x1b[0K\r\n",7);
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
                    terminal_buffer_append(&ab,"\x1b[7m",4); /* Reverse video */
                }

                if (hl[j] == HL_NONPRINT) {
                    char sym;
                    if (!selected) terminal_buffer_append(&ab,"\x1b[7m",4);
                    if (c[j] <= 26)
                        sym = '@'+c[j];
                    else
                        sym = '?';
                    terminal_buffer_append(&ab,&sym,1);
                    terminal_buffer_append(&ab,"\x1b[0m",4);
                    if (current_color != -1) {
                        char buf[32];
                        int clen = editor_format_color(ctx, current_color, buf, sizeof(buf));
                        terminal_buffer_append(&ab,buf,clen);
                    }
                } else if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        terminal_buffer_append(&ab,"\x1b[39m",5);
                        current_color = -1;
                    }
                    terminal_buffer_append(&ab,c+j,1);
                    if (selected) {
                        terminal_buffer_append(&ab,"\x1b[0m",4); /* Reset */
                    }
                } else {
                    int color = hl[j];
                    if (color != current_color) {
                        char buf[32];
                        int clen = editor_format_color(ctx, color, buf, sizeof(buf));
                        current_color = color;
                        terminal_buffer_append(&ab,buf,clen);
                    }
                    terminal_buffer_append(&ab,c+j,1);
                    if (selected) {
                        terminal_buffer_append(&ab,"\x1b[0m",4); /* Reset */
                        if (current_color != -1) {
                            char buf[32];
                            int clen = editor_format_color(ctx, current_color, buf, sizeof(buf));
                            terminal_buffer_append(&ab,buf,clen);
                        }
                    }
                }
            }
        }
        terminal_buffer_append(&ab,"\x1b[39m",5);
        terminal_buffer_append(&ab,"\x1b[0K",4);
        terminal_buffer_append(&ab,"\r\n",2);
    }

    /* Create a two rows status. First row: */
    terminal_buffer_append(&ab,"\x1b[0K",4);
    terminal_buffer_append(&ab,"\x1b[7m",4);
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
    terminal_buffer_append(&ab,status,len);
    while(len < ctx->screencols) {
        if (ctx->screencols - len == rlen) {
            terminal_buffer_append(&ab,rstatus,rlen);
            break;
        } else {
            terminal_buffer_append(&ab," ",1);
            len++;
        }
    }
    terminal_buffer_append(&ab,"\x1b[0m\r\n",6);

    /* Second row depends on ctx->statusmsg and the status message update time. */
    terminal_buffer_append(&ab,"\x1b[0K",4);
    int msglen = strlen(ctx->statusmsg);
    if (msglen && time(NULL)-ctx->statusmsg_time < 5)
        terminal_buffer_append(&ab,ctx->statusmsg,msglen <= ctx->screencols ? msglen : ctx->screencols);

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
    terminal_buffer_append(&ab,buf,strlen(buf));
    terminal_buffer_append(&ab,"\x1b[?25h",6); /* Show cursor. */
    write(STDOUT_FILENO,ab.b,ab.len);
    terminal_buffer_free(&ab);
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

/* Window size functions are now in loki_terminal.c */

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

/* ========================= Modal Key Processing ============================ */

/* Process a single keypress - delegates to modal editing module */
void editor_process_keypress(editor_ctx_t *ctx, int fd) {
    /* All modal editing logic is now in loki_modal.c */
    modal_process_keypress(ctx, fd);
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
    terminal_update_window_size(ctx);
    signal(SIGWINCH, terminal_sig_winch_handler);

    /* Set static pointer for atexit cleanup */
    editor_for_atexit = ctx;
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
