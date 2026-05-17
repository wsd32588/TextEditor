/*
 * File I/O: open (read) and save (write) text files.
 *
 * editor_open reads a file line-by-line via fgetc, inserting each
 * line through editor_insert_raw which triggers render + syntax.
 * editor_save writes every row in order, separated by \n.
 * Tab-less filename prompts Save As via editor_prompt.
 */
#include "common.h"
#include "syntax.h"
#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  保存
// ============================================================

void editor_save(EditorConfig *ec)
{
    if (ec->filename == NULL) {
        /* 未命名文件 → 弹出 Save As 提示 */
        ec->filename = editor_prompt(ec, "Save as: %s (ESC to cancel)");
        if (ec->filename == NULL) {
            editor_set_status_message(ec, "Saved aborted");
            return;
        }
        editor_select_syntax_highlight(ec);              // 后缀确定 → 启用语法高亮
        for (int i = 0; i < ec->num_rows; i++)
            editor_update_row(ec, &ec->row[i]);
    }

    FILE *fp = fopen(ec->filename, "w");
    if (fp == NULL) {
        editor_set_status_message(ec, "Save failed! I/O error.");
        return;
    }

    size_t total_written = 0;
    for (int i = 0; i < ec->num_rows; i++) {
        if (ec->row[i].size > 0)
            total_written += fwrite(ec->row[i].chars, 1, ec->row[i].size, fp);
        total_written += fwrite("\n", 1, 1, fp);
    }
    fclose(fp);

    ec->dirty = 0;
    editor_set_status_message(ec, "Saved successfully! (%d bytes)", total_written);
}

// ============================================================
//  打开
// ============================================================

void editor_open(EditorConfig *ec, const char *filename)
{
    if (ec->filename) free(ec->filename);
    ec->filename = editor_strdup(filename);

    editor_select_syntax_highlight(ec);

    FILE *fp = fopen(filename, "r");
    if (!fp) return;

    char *line = NULL;
    size_t linecap = 0;
    size_t len = 0;
    int c;

    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') {
            editor_insert_raw(ec, ec->num_rows, line, len);
            len = 0;
        } else {
            if (len >= linecap) {
                linecap = (linecap == 0) ? 128 : linecap * 2;
                line = editor_safe_realloc(line, linecap);
            }
            line[len] = (char)c;
            len++;
        }
    }

    if (len > 0)
        editor_insert_raw(ec, ec->num_rows, line, len);

    free(line);
    fclose(fp);
    ec->dirty = 0;
}
