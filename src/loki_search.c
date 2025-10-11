/* loki_search.c - Text search functionality
 *
 * This module implements incremental text search within the editor.
 * Search is interactive: as the user types, matches are found and highlighted
 * in real-time. Users can navigate between matches with arrow keys.
 *
 * Features:
 * - Incremental search: updates as you type
 * - Forward/backward navigation: arrow keys move between matches
 * - Visual highlighting: matches shown with HL_MATCH color
 * - Wrapping: search wraps around at beginning/end of file
 * - Restore cursor: ESC returns to original position
 *
 * Keybindings:
 * - ESC: Cancel search, restore original cursor position
 * - ENTER: Accept search, keep cursor at current match
 * - Arrow Up/Left: Search backward (previous match)
 * - Arrow Down/Right: Search forward (next match)
 * - Backspace/Delete: Remove character from query
 * - Printable chars: Add to search query
 */

#include "loki_search.h"
#include "loki_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Incremental text search with arrow keys navigation.
 * Interactive search that updates as you type and allows navigating
 * between matches. ESC cancels and restores cursor position.
 * ENTER accepts and keeps cursor at current match. */
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
        editor_set_status_msg(ctx,
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
            editor_set_status_msg(ctx, "");
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
