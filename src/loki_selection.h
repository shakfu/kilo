/* loki_selection.h - Text selection and clipboard functionality
 *
 * This header declares the public API for visual selection and clipboard
 * operations. The implementation uses OSC 52 terminal escape sequences for
 * clipboard access that works over SSH without X11 dependencies.
 */

#ifndef LOKI_SELECTION_H
#define LOKI_SELECTION_H

#include "loki_internal.h"
#include <stddef.h>

/* Check if a position is within the current selection
 * Returns 1 if selected, 0 otherwise */
int is_selected(editor_ctx_t *ctx, int row, int col);

/* Base64 encode a string (for OSC 52 clipboard protocol)
 * Caller must free the returned string
 * Returns NULL on allocation failure */
char *base64_encode(const char *input, size_t len);

/* Copy selected text to clipboard using OSC 52 escape sequence
 * Clears selection after successful copy */
void copy_selection_to_clipboard(editor_ctx_t *ctx);

#endif /* LOKI_SELECTION_H */
