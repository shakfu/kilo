/* loki_markdown.h - Markdown parsing and rendering API
 *
 * This header provides the public API for markdown parsing and rendering
 * using the cmark library (CommonMark specification).
 */

#ifndef LOKI_MARKDOWN_H
#define LOKI_MARKDOWN_H

#include <stddef.h>
#include <cmark.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================= Data Structures ================================== */

/* Markdown document handle (opaque) */
typedef struct loki_markdown_doc {
    cmark_node *root;    /* Root node of parsed AST */
    int options;         /* Parse options used */
} loki_markdown_doc;

/* Heading information */
typedef struct loki_markdown_heading {
    int level;           /* Heading level (1-6) */
    char *text;          /* Heading text content */
} loki_markdown_heading;

/* Link information */
typedef struct loki_markdown_link {
    char *url;           /* Link URL */
    char *title;         /* Link title (may be NULL) */
    char *text;          /* Link text content */
} loki_markdown_link;

/* ======================= Parse Options ==================================== */

/* These correspond to cmark options - see cmark.h for details */
#define LOKI_MD_OPT_DEFAULT             CMARK_OPT_DEFAULT
#define LOKI_MD_OPT_SOURCEPOS           CMARK_OPT_SOURCEPOS
#define LOKI_MD_OPT_HARDBREAKS          CMARK_OPT_HARDBREAKS
#define LOKI_MD_OPT_SAFE                CMARK_OPT_SAFE
#define LOKI_MD_OPT_NOBREAKS            CMARK_OPT_NOBREAKS
#define LOKI_MD_OPT_NORMALIZE           CMARK_OPT_NORMALIZE
#define LOKI_MD_OPT_VALIDATE_UTF8       CMARK_OPT_VALIDATE_UTF8
#define LOKI_MD_OPT_SMART               CMARK_OPT_SMART

/* ======================= Parsing Functions ================================ */

/* Parse markdown text into document AST
 *
 * Parameters:
 *   text    - Markdown text to parse (UTF-8)
 *   len     - Length of text in bytes
 *   options - Parse options (LOKI_MD_OPT_*)
 *
 * Returns:
 *   Parsed document handle, or NULL on error
 *   Caller must free with loki_markdown_free()
 */
loki_markdown_doc *loki_markdown_parse(const char *text, size_t len, int options);

/* Parse markdown from file
 *
 * Parameters:
 *   filename - Path to markdown file
 *   options  - Parse options (LOKI_MD_OPT_*)
 *
 * Returns:
 *   Parsed document handle, or NULL on error
 *   Caller must free with loki_markdown_free()
 */
loki_markdown_doc *loki_markdown_parse_file(const char *filename, int options);

/* Free a parsed markdown document
 *
 * Parameters:
 *   doc - Document to free (may be NULL)
 */
void loki_markdown_free(loki_markdown_doc *doc);

/* ======================= Rendering Functions ============================== */

/* Render document to HTML
 *
 * Parameters:
 *   doc     - Parsed document
 *   options - Render options (LOKI_MD_OPT_*)
 *
 * Returns:
 *   HTML string, or NULL on error
 *   Caller must free() the returned string
 */
char *loki_markdown_render_html(loki_markdown_doc *doc, int options);

/* Render document to XML
 *
 * Parameters:
 *   doc     - Parsed document
 *   options - Render options (LOKI_MD_OPT_*)
 *
 * Returns:
 *   XML string, or NULL on error
 *   Caller must free() the returned string
 */
char *loki_markdown_render_xml(loki_markdown_doc *doc, int options);

/* Render document to man page format
 *
 * Parameters:
 *   doc     - Parsed document
 *   options - Render options (LOKI_MD_OPT_*)
 *   width   - Line width for wrapping (0 = no wrapping)
 *
 * Returns:
 *   Man page string, or NULL on error
 *   Caller must free() the returned string
 */
char *loki_markdown_render_man(loki_markdown_doc *doc, int options, int width);

/* Render document back to CommonMark markdown
 *
 * Parameters:
 *   doc     - Parsed document
 *   options - Render options (LOKI_MD_OPT_*)
 *   width   - Line width for wrapping (0 = no wrapping)
 *
 * Returns:
 *   Markdown string, or NULL on error
 *   Caller must free() the returned string
 */
char *loki_markdown_render_commonmark(loki_markdown_doc *doc, int options, int width);

/* Render document to LaTeX
 *
 * Parameters:
 *   doc     - Parsed document
 *   options - Render options (LOKI_MD_OPT_*)
 *   width   - Line width for wrapping (0 = no wrapping)
 *
 * Returns:
 *   LaTeX string, or NULL on error
 *   Caller must free() the returned string
 */
char *loki_markdown_render_latex(loki_markdown_doc *doc, int options, int width);

/* ======================= Direct Conversion Functions ====================== */

/* Convert markdown text directly to HTML (simple one-step API)
 *
 * This is a convenience function that parses and renders in one call.
 * For more control, use loki_markdown_parse() + loki_markdown_render_html().
 *
 * Parameters:
 *   text    - Markdown text (UTF-8)
 *   len     - Length of text in bytes
 *   options - Parse/render options (LOKI_MD_OPT_*)
 *
 * Returns:
 *   HTML string, or NULL on error
 *   Caller must free() the returned string
 */
char *loki_markdown_to_html(const char *text, size_t len, int options);

/* ======================= Document Analysis ================================ */

/* Count number of headings in document
 *
 * Parameters:
 *   doc - Parsed document
 *
 * Returns:
 *   Number of headings (h1-h6)
 */
int loki_markdown_count_headings(loki_markdown_doc *doc);

/* Count number of code blocks in document
 *
 * Parameters:
 *   doc - Parsed document
 *
 * Returns:
 *   Number of code blocks (fenced and indented)
 */
int loki_markdown_count_code_blocks(loki_markdown_doc *doc);

/* Count number of links in document
 *
 * Parameters:
 *   doc - Parsed document
 *
 * Returns:
 *   Number of links (inline and reference)
 */
int loki_markdown_count_links(loki_markdown_doc *doc);

/* ======================= Structure Extraction ============================= */

/* Extract all headings from document
 *
 * Parameters:
 *   doc   - Parsed document
 *   count - Output: number of headings found
 *
 * Returns:
 *   Array of headings, or NULL if none found or on error
 *   Caller must free with loki_markdown_free_headings()
 */
loki_markdown_heading *loki_markdown_extract_headings(loki_markdown_doc *doc, int *count);

/* Free heading array
 *
 * Parameters:
 *   headings - Array returned by loki_markdown_extract_headings()
 *   count    - Number of headings in array
 */
void loki_markdown_free_headings(loki_markdown_heading *headings, int count);

/* Extract all links from document
 *
 * Parameters:
 *   doc   - Parsed document
 *   count - Output: number of links found
 *
 * Returns:
 *   Array of links, or NULL if none found or on error
 *   Caller must free with loki_markdown_free_links()
 */
loki_markdown_link *loki_markdown_extract_links(loki_markdown_doc *doc, int *count);

/* Free link array
 *
 * Parameters:
 *   links - Array returned by loki_markdown_extract_links()
 *   count - Number of links in array
 */
void loki_markdown_free_links(loki_markdown_link *links, int count);

/* ======================= Utility Functions ================================ */

/* Get cmark library version string
 *
 * Returns:
 *   Version string (e.g., "0.30.3")
 */
const char *loki_markdown_version(void);

/* Validate markdown syntax
 *
 * Parameters:
 *   text - Markdown text to validate
 *   len  - Length of text in bytes
 *
 * Returns:
 *   1 if valid markdown, 0 otherwise
 */
int loki_markdown_validate(const char *text, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* LOKI_MARKDOWN_H */
