/*
 * Syntax highlighting engine (keyword + comment + search match).
 *
 * g_high_light_data_base[] defines rules for each language.
 * editor_select_syntax_highlight() picks the right entry by
 * filename suffix and sets ec->tab_stop accordingly.
 *
 * editor_update_syntax() annotates row->high_light[] (render-space)
 * with colour tags by scanning row->render[]:
 *   1. Multiline comments (slash-star ... star-slash) are handled via
 *      a state machine that inherits highlight_open_comment from the
 *      previous row, with recursive re-highlight on state change.
 *   2. Single-line comments (//, #) are detected once and the rest
 *      of the line is filled with HL_COMMENT.
 *   3. Keywords are matched at word boundaries via strncmp.
 *      Trailing '|' in the keyword table marks type keywords -> HL_KEYWORD2.
 *   4. Active search terms (ec->search_query) are highlighted last
 *      via strstr, potentially overriding keyword colours.
 */
#include <ctype.h>
#include <string.h>
#include "syntax.h"

// ==========================================
// 1. C / C++
// ==========================================
static char *c_extensions[] = { ".c", ".h", ".cpp", ".hpp", ".cc", NULL };
static char *c_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "bool|", NULL
};

// ==========================================
// 2. Python
// ==========================================
static char *python_extensions[] = { ".py", NULL };
static char *python_keywords[] = {
    "and", "as", "assert", "break", "class", "continue", "def", "del",
    "elif", "else", "except", "finally", "for", "from", "global", "if",
    "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass",
    "raise", "return", "try", "while", "with", "yield", "async", "await",
    "False|", "True|", "None|", "int|", "float|", "str|", "bool|", "list|",
    "dict|", "set|", "tuple|", NULL
};

// ==========================================
// 3. Rust
// ==========================================
static char *rust_extensions[] = { ".rs", NULL };
static char *rust_keywords[] = {
    "as", "break", "const", "continue", "crate", "else", "enum", "extern",
    "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move",
    "mut", "pub", "ref", "return", "self", "Self", "static", "struct",
    "super", "trait", "type", "unsafe", "use", "where", "while", "async", "await",
    "true|", "false|", "i8|", "i16|", "i32|", "i64|", "u8|", "u16|", "u32|",
    "u64|", "f32|", "f64|", "usize|", "isize|", "bool|", "char|", "str|", "String|", NULL
};

// ==========================================
// 4. Go
// ==========================================
static char *go_extensions[] = { ".go", NULL };
static char *go_keywords[] = {
    "break", "default", "func", "interface", "select", "case", "defer",
    "go", "map", "struct", "chan", "else", "goto", "package", "switch",
    "const", "fallthrough", "if", "range", "type", "continue", "for",
    "import", "return", "var",
    "true|", "false|", "nil|", "iota|", "bool|", "byte|", "complex64|",
    "complex128|", "error|", "float32|", "float64|", "int|", "int8|",
    "int16|", "int32|", "int64|", "rune|", "string|", "uint|", "uint8|",
    "uint16|", "uint32|", "uint64|", "uintptr|", NULL
};

// ==========================================
// 5. JavaScript / TypeScript
// ==========================================
static char *js_extensions[] = { ".js", ".ts", NULL };
static char *js_keywords[] = {
    "break", "case", "catch", "class", "const", "continue", "debugger",
    "default", "delete", "do", "else", "export", "extends", "finally",
    "for", "function", "if", "import", "in", "instanceof", "new", "return",
    "super", "switch", "this", "throw", "try", "typeof", "var", "void",
    "while", "with", "yield", "let", "await", "async",
    "true|", "false|", "null|", "undefined|", "NaN|", "Infinity|", NULL
};

static int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

// ============================================================
//  高亮数据库
// ============================================================

struct EditorSyntax g_high_light_data_base[] = {
    {
        c_extensions,
        c_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
        4
    },
    {
        python_extensions,
        python_keywords,
        "#", "\"\"\"", "\"\"\"",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
        4
    },
    {
        rust_extensions,
        rust_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
        4
    },
    {
        go_extensions,
        go_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
        8
    },
    {
        js_extensions,
        js_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
        2
    }
};

const int g_high_light_data_base_entries =
    sizeof(g_high_light_data_base) / sizeof(g_high_light_data_base[0]);

// ============================================================
//  语法高亮更新
// ============================================================

void editor_update_syntax(EditorConfig *ec, EditorRow *row) {

    if (row->render_size == 0) {
        free(row->high_light);
        row->high_light = NULL;
        return;
    }

    row->high_light = editor_safe_realloc(row->high_light, row->render_size);
    memset(row->high_light, HL_NORMAL, row->render_size);

    if (ec->syntax == NULL) return;

    char **keywords = ec->syntax->keywords;
    char *scs = ec->syntax->single_line_comment_start;
    char *mcs = ec->syntax->multiline_comment_start;
    char *mce = ec->syntax->multiline_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int idx = row - ec->row;
    int in_comment = (idx > 0 && ec->row[idx - 1].highlight_open_comment);

    int prev_sep = 1;
    int i = 0;

    while (i < row->render_size) {
        char c = row->render[i];

        /* 多行注释状态机（最高优先级） */
        if (mcs_len && mce_len) {
            if (in_comment) {
                row->high_light[i] = HL_ML_COMMENT;
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->high_light[i], HL_ML_COMMENT, mce_len);
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                }
                i++;
                continue;
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->high_light[i], HL_ML_COMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        /* 单行注释检测 */
        if (scs_len && !strncmp(&row->render[i], scs, scs_len)) {
            memset(&row->high_light[i], HL_COMMENT, row->render_size - i);
            break;
        }

        /* 词边界 → 匹配关键字 */
        if (prev_sep) {
            int keyword_match = 0;
            for (int j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2) klen--;

                if (!strncmp(&row->render[i], keywords[j], klen)
                    && is_separator(row->render[i + klen])) {
                    memset(&row->high_light[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    keyword_match = 1;
                    break;
                }
            }
            if (keyword_match) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }

    /* 搜索匹配高亮（覆盖关键字/注释颜色） */
    if (ec->search_query != NULL) {
        int query_len = strlen(ec->search_query);
        if (query_len > 0) {
            char *match = row->render;
            while ((match = strstr(match, ec->search_query)) != NULL) {
                int match_idx = match - row->render;
                memset(&row->high_light[match_idx], HL_MATCH, query_len);
                match += query_len;
            }
        }
    }

    /* 多行注释状态向下传染 */
    int changed = (row->highlight_open_comment != in_comment);
    row->highlight_open_comment = in_comment;
    if (changed && idx + 1 < ec->num_rows) {
        editor_update_syntax(ec, &ec->row[idx + 1]);
    }
}

// ============================================================
//  语法嗅探
// ============================================================

void editor_select_syntax_highlight(EditorConfig *ec) {
    ec->syntax = NULL;
    ec->tab_stop = EDITOR_TAB_STOP;
    if (ec->filename == NULL) return;

    char *ext = strrchr(ec->filename, '.');

    for (int j = 0; j < g_high_light_data_base_entries; j++) {
        struct EditorSyntax *s = &g_high_light_data_base[j];
        int i = 0;
        while (s->file_match[i]) {
            int is_ext = (s->file_match[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->file_match[i])) ||
                (!is_ext && strstr(ec->filename, s->file_match[i]))) {
                ec->syntax = s;
                ec->tab_stop = s->tab_stop;
                return;
            }
            i++;
        }
    }
}
