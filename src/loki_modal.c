/* loki_modal.c - Modal editing (vim-like modes)
 *
 * This module implements vim-like modal editing with three modes:
 * - NORMAL mode: Navigation and commands (default)
 * - INSERT mode: Text insertion
 * - VISUAL mode: Text selection
 *
 * Modal editing separates navigation from text insertion, allowing
 * efficient keyboard-only editing without modifier keys.
 *
 * Keybindings:
 * NORMAL mode:
 *   h/j/k/l - Move cursor left/down/up/right
 *   i - Enter INSERT mode
 *   a - Enter INSERT mode after cursor
 *   o/O - Insert line below/above and enter INSERT mode
 *   v - Enter VISUAL mode (selection)
 *   x - Delete character
 *   {/} - Paragraph motion (move to prev/next empty line)
 *
 * INSERT mode:
 *   ESC - Return to NORMAL mode
 *   Normal typing inserts characters
 *   Arrow keys move cursor
 *
 * VISUAL mode:
 *   h/j/k/l - Extend selection
 *   y - Yank (copy) selection
 *   ESC - Return to NORMAL mode
 */

#include "loki_modal.h"
#include "loki_internal.h"
#include "loki_selection.h"
#include "loki_search.h"
#include "loki_terminal.h"
#include <stdlib.h>

/* Number of times CTRL-Q must be pressed before actually quitting */
#define KILO_QUIT_TIMES 3

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
                editor_set_status_msg(ctx, "Lua REPL active (Ctrl-L or ESC to close)");
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
            editor_set_status_msg(ctx, "Unknown command");
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
            editor_set_status_msg(ctx, "Word wrap %s", ctx->word_wrap ? "enabled" : "disabled");
            break;
        case CTRL_L:
            /* Toggle REPL */
            ctx->repl.active = !ctx->repl.active;
            editor_update_repl_layout(ctx);
            if (ctx->repl.active) {
                editor_set_status_msg(ctx, "Lua REPL active (Ctrl-L or ESC to close)");
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
            editor_set_status_msg(ctx, "Yanked selection");
            break;

        /* Delete selection (without undo for now) */
        case 'd':
        case 'x':
            copy_selection_to_clipboard(ctx); /* Save to clipboard first */
            /* TODO: delete selection - need to implement this */
            editor_set_status_msg(ctx, "Delete not implemented yet");
            ctx->mode = MODE_NORMAL;
            ctx->sel_active = 0;
            break;

        /* Global commands */
        case CTRL_C:
            copy_selection_to_clipboard(ctx);
            break;

        default:
            /* Unknown command - beep */
            editor_set_status_msg(ctx, "Unknown visual command");
            break;
    }
    (void)fd; /* Unused */
}

/* Process a single keypress with modal editing support.
 * This is the main entry point for all keyboard input when modal editing is enabled.
 * Dispatches to appropriate mode handler (normal/insert/visual). */
void modal_process_keypress(editor_ctx_t *ctx, int fd) {
    /* When the file is modified, requires Ctrl-q to be pressed N times
     * before actually quitting. */
    static int quit_times = KILO_QUIT_TIMES;

    int c = terminal_read_key(fd);

    /* REPL keypress handling */
    if (ctx->repl.active) {
        lua_repl_handle_keypress(ctx, c);
        return;
    }

    /* Handle quit globally (works in all modes) */
    if (c == CTRL_Q) {
        if (ctx->dirty && quit_times) {
            editor_set_status_msg(ctx, "WARNING!!! File has unsaved changes. "
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
