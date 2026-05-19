/*
 * Syntax highlighting engine (keyword + comment + search match).
 *
 * g_high_light_data_base[] defines rules for each language.
 * editor_select_syntax_highlight() picks the right entry by
 * filename suffix and sets ec->settings.tab_stop accordingly.
 *
 * editor_update_syntax() annotates row->cells[].hl with colour tags
 * by scanning the cell array:
 *   1. Multiline comments (slash-star ... star-slash) are handled via
 *      a state machine that inherits highlight_open_comment from the
 *      previous row, with recursive re-highlight on state change.
 *   2. Single-line comments (//, #) are detected once and the rest
 *      of the line is filled with HL_COMMENT.
 *   3. Keywords are matched at word boundaries via strncmp.
 *      Trailing '|' in the keyword table marks type keywords -> HL_KEYWORD2.
 *   4. Active search terms (ec->ui.search_query) are highlighted last
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

static int cell_is_sep(uint32_t cp) {
    if (cp > 127) return 0;
    return isspace((int)cp) || strchr(",.()+-/*=~%<>[];", (int)cp) != NULL;
}

static int cell_match_seq(RenderCell *cells, int start, int count, const char *seq, int seq_len) {
    if (start + seq_len > count) return 0;
    for (int k = 0; k < seq_len; k++)
        if (cells[start + k].codepoint != (unsigned char)seq[k]) return 0;
    return 1;
}

// FNV-1a hash — 快速计算行内容指纹，用于增量语法跳过未变更行
static size_t row_hash(const char *s, int len) {
    size_t h = 14695981039346656037ULL;
    for (int i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

void editor_update_syntax(Document *doc, EditorSettings *settings, UIState *ui, EditorRow *start_row) {
    int current_idx = start_row - doc->row;

    while (current_idx < doc->num_rows) {
        EditorRow *row = &doc->row[current_idx];

        int in_comment = (current_idx > 0 && doc->row[current_idx - 1].highlight_open_comment);

        // 内容指纹 + 注释状态都匹配 → 跳过（行未修改，状态没变）
        size_t h = row_hash(row->chars, row->size);
        if (h == row->syntax_hash && row->highlight_open_comment == in_comment) {
            row->syntax_hash = h;
            break;
        }
        row->syntax_hash = h;

        if (row->cell_count == 0) {
            int changed = (row->highlight_open_comment != in_comment);
            row->highlight_open_comment = in_comment;
            if (!changed) break;
            current_idx++;
            continue;
        }

        for (int i = 0; i < row->cell_count; i++)
            row->cells[i].hl = HL_NORMAL;

        if (settings->syntax != NULL) {
            char **keywords = settings->syntax->keywords;
            char *scs = settings->syntax->single_line_comment_start;
            char *mcs = settings->syntax->multiline_comment_start;
            char *mce = settings->syntax->multiline_comment_end;

            int scs_len = scs ? strlen(scs) : 0;
            int mcs_len = mcs ? strlen(mcs) : 0;
            int mce_len = mce ? strlen(mce) : 0;

            int prev_sep = 1;
            int i = 0;

            while (i < row->cell_count) {
                uint32_t cp = row->cells[i].codepoint;

                /* 多行注释状态机 */
                if (mcs_len && mce_len) {
                    if (in_comment) {
                        row->cells[i].hl = HL_ML_COMMENT;
                        if (cell_match_seq(row->cells, i, row->cell_count, mce, mce_len)) {
                            for (int k = 0; k < mce_len; k++)
                                row->cells[i + k].hl = HL_ML_COMMENT;
                            i += mce_len;
                            in_comment = 0;
                            prev_sep = 1;
                            continue;
                        }
                        i++;
                        continue;
                    } else if (cell_match_seq(row->cells, i, row->cell_count, mcs, mcs_len)) {
                        for (int k = 0; k < mcs_len; k++)
                            row->cells[i + k].hl = HL_ML_COMMENT;
                        i += mcs_len;
                        in_comment = 1;
                        continue;
                    }
                }

                /* 单行注释检测 */
                if (scs_len && cell_match_seq(row->cells, i, row->cell_count, scs, scs_len)) {
                    for (int k = i; k < row->cell_count; k++)
                        row->cells[k].hl = HL_COMMENT;
                    break;
                }

                /* 词边界 → 匹配关键字 */
                if (prev_sep && cp < 128) {
                    int keyword_match = 0;
                    for (int j = 0; keywords[j]; j++) {
                        int klen = strlen(keywords[j]);
                        int kw2 = keywords[j][klen - 1] == '|';
                        if (kw2) klen--;
                        if (i + klen > row->cell_count) continue;

                        int match = 1;
                        for (int k = 0; k < klen; k++) {
                            if (row->cells[i + k].codepoint != (unsigned char)keywords[j][k]) {
                                match = 0; break;
                            }
                        }
                        if (match && (i + klen == row->cell_count ||
                                      cell_is_sep(row->cells[i + klen].codepoint))) {
                            for (int k = 0; k < klen; k++)
                                row->cells[i + k].hl = kw2 ? HL_KEYWORD2 : HL_KEYWORD1;
                            i += klen;
                            keyword_match = 1;
                            break;
                        }
                    }
                    if (keyword_match) { prev_sep = 0; continue; }
                }
                prev_sep = cell_is_sep(cp);
                i++;
            }
        } // end if (settings->syntax != NULL)

        /* 搜索匹配高亮 — 在 chars[] 层做 strstr 然后映射到 cell 索引 */
        if (ui->search_query != NULL) {
            int query_len = strlen(ui->search_query);
            if (query_len > 0 && row->size > 0) {
                const char *search_start = row->chars;
                const char *p;
                while ((p = strstr(search_start, ui->search_query)) != NULL) {
                    int byte_off = (int)(p - row->chars);
                    int byte_end = byte_off + query_len;
                    int bi = 0;
                    for (int ci = 0; ci < row->cell_count; ci++) {
                        int cell_bytes = row->cells[ci].byte_len;
                        if (cell_bytes == 0) cell_bytes = 1; /* Tab→空格 cell，占 chars 1 字节 */
                        if (bi >= byte_off && bi < byte_end)
                            row->cells[ci].hl = HL_MATCH;
                        if (bi >= byte_end) break;
                        bi += cell_bytes;
                    }
                    search_start = p + query_len;
                }
            }
        }

        int changed = (row->highlight_open_comment != in_comment);
        row->highlight_open_comment = in_comment;
        if (!changed) break;

        current_idx++;
    }
}

// ============================================================
//  语法嗅探
// ============================================================

void editor_select_syntax_highlight(Document *doc, EditorSettings *settings) {
    settings->syntax = NULL;
    settings->tab_stop = EDITOR_TAB_STOP;
    if (doc->filename == NULL) return;

    char *ext = strrchr(doc->filename, '.');

    for (int j = 0; j < g_high_light_data_base_entries; j++) {
        struct EditorSyntax *s = &g_high_light_data_base[j];
        int i = 0;
        while (s->file_match[i]) {
            int is_ext = (s->file_match[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->file_match[i])) ||
                (!is_ext && strstr(doc->filename, s->file_match[i]))) {
                settings->syntax = s;
                settings->tab_stop = s->tab_stop;
                return;
            }
            i++;
        }
    }
}
