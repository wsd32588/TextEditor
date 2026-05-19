/*
 * Core editing operations: row management, character insertion,
 * deletion, line splitting, and the search prompt.
 *
 * Every mutation of EditorRow::chars is followed by a call to
 * editor_update_row() to rebuild the render buffer and re-run
 * syntax highlighting, maintaining the invariant
 *   chars → render = 1:1 (with \t expanded to spaces).
 */
#include "editor.h"
#include "common.h"
#include "terminal.h"
#include "render.h"
#include "syntax.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  编辑器初始化
// ============================================================

void editor_init(EditorConfig *ec) {
    ec->view.cursor_x = 0;
    ec->view.cursor_y = 0;
    ec->view.row_off = 0;
    ec->view.col_off = 0;
    ec->doc.num_rows = 0;
    ec->doc.row = NULL;
    ec->doc.dirty = 0;
    ec->doc.filename = NULL;
    ec->settings.syntax = NULL;
    ec->view.gutter_width = 0;
    ec->ui.search_query = NULL;
    ec->settings.tab_stop = EDITOR_TAB_STOP;
    ec->ui.overwrite_mode = 0;

    DWORD written;
    WriteFile(ec->term.hOUT, "\x1b[?1049h", 8, &written, NULL);

    if (terminal_get_window_size(&ec->term, &ec->view) == -1) {
        printf("Failed to get window size!\n");
        exit(1);
    }
}
// ============================================================
//  OOM 紧急保存 — 尽最大努力写到 emergency_backup.txt
// ============================================================
void editor_emergency_save(EditorConfig *ec) {
    const char *path = "emergency_backup.txt";
    FILE *fp = fopen(path, "w");
    if (fp == NULL) return;

    for (int i = 0; i < ec->doc.num_rows; i++) {
        if (ec->doc.row[i].size > 0)
            fwrite(ec->doc.row[i].chars, 1, ec->doc.row[i].size, fp);
        fwrite("\n", 1, 1, fp);
    }
    fclose(fp);
}

// ============================================================
//  清空当前文件占用的所有行内存
// ============================================================
void editor_free_all_row(EditorConfig *ec){
    for (int i = 0; i < ec->doc.num_rows; i++){
        free(ec->doc.row[i].chars);
        free(ec->doc.row[i].cells);
    }
    free(ec->doc.row);
    ec->doc.row = NULL;
    ec->doc.num_rows = 0;
    ec->doc.dirty = 0;

    if (ec->doc.filename){
        free(ec->doc.filename);
        ec->doc.filename = NULL;
    }
    if (ec->ui.search_query) {
        free(ec->ui.search_query);
        ec->ui.search_query = NULL;
    }
    for (int i = 0; i < ec->doc.undo_count; i++) free(ec->doc.undo_stack[i].text);
    free(ec->doc.undo_stack);
    ec->doc.undo_stack = NULL;
    ec->doc.undo_count = 0;
    ec->doc.undo_cap = 0;
    for (int i = 0; i < ec->doc.redo_count; i++) free(ec->doc.redo_stack[i].text);
    free(ec->doc.redo_stack);
    ec->doc.redo_stack = NULL;
    ec->doc.redo_count = 0;
    ec->doc.redo_cap = 0;
}

// ============================================================
//  坐标转换:  chars 空间 → render 空间
// ============================================================

int editor_row_cx_to_rx(EditorConfig *ec, EditorRow *row, int cx)
{
    int rx = 0;
    int j = 0;
    while (j < cx && j < row->size) {
        if (row->chars[j] == '\t') {
            rx += (ec->settings.tab_stop - 1) - (rx % ec->settings.tab_stop) + 1;
            j++;
        } else {
            Utf8Step s = utf8_step(&row->chars[j], row->size - j);
            rx += s.cols;
            j += s.bytes;
        }
    }
    return rx;
}

/* 将终端屏幕的视觉列索引 (rx) 逆向转换为 chars 空间的字节索引 (cx) */
int editor_row_rx_to_cx(EditorConfig *ec, EditorRow *row, int rx)
{
    int cur_rx = 0;
    int cx = 0;
    while (cx < row->size) {
        if (row->chars[cx] == '\t') {
            int tab_width = (ec->settings.tab_stop - 1) - (cur_rx % ec->settings.tab_stop) + 1;
            if (cur_rx + tab_width > rx) return cx;
            cur_rx += tab_width;
            cx++;
        } else {
            Utf8Step s = utf8_step(&row->chars[cx], row->size - cx);
            if (cur_rx + s.cols > rx) return cx;
            cur_rx += s.cols;
            cx += s.bytes;
        }
    }
    return cx;
}

// ============================================================
//  Render 缓冲区生成
// ============================================================

void editor_update_row(EditorConfig *ec, EditorRow *row) {
    // Pass 1: count cells
    int cell_count = 0;
    int j = 0;
    int rx = 0;
    while (j < row->size) {
        if (row->chars[j] == '\t') {
            int spaces = (ec->settings.tab_stop - 1) - (rx % ec->settings.tab_stop) + 1;
            cell_count += spaces;
            rx += spaces;
            j++;
        } else {
            Utf8Step s = utf8_step(&row->chars[j], row->size - j);
            cell_count++;
            rx += s.cols;
            j += s.bytes;
        }
    }

    // Allocate
    free(row->cells);
    if (cell_count == 0) {
        row->cells = NULL;
        row->cell_count = 0;
        editor_update_syntax(&ec->doc, &ec->settings, &ec->ui, row);
        return;
    }
    row->cells = editor_safe_realloc(NULL, cell_count * sizeof(RenderCell));
    row->cell_count = cell_count;

    // Pass 2: fill cells
    int ci = 0;
    j = 0;
    rx = 0;
    while (j < row->size) {
        if (row->chars[j] == '\t') {
            int spaces = (ec->settings.tab_stop - 1) - (rx % ec->settings.tab_stop) + 1;
            for (int k = 0; k < spaces; k++)
                row->cells[ci++] = (RenderCell){' ', 0, 1, HL_NORMAL};
            rx += spaces;
            j++;
        } else {
            Utf8Char uc = utf8_decode(&row->chars[j], row->size - j);
            row->cells[ci++] = (RenderCell){uc.codepoint, (uint8_t)uc.byte_len,
                                            (uint8_t)uc.display_width, HL_NORMAL};
            rx += uc.display_width;
            j += uc.byte_len;
        }
    }

    editor_update_syntax(&ec->doc, &ec->settings, &ec->ui, row);
}

// ============================================================
// 撤销/重做(undo/redo)(Ctrl + Z,Ctrl + Y)
// ============================================================

static void ensure_stack_cap(UndoRecord **stack, int *cap, int needed) {
    if (*cap >= needed) return;
    int new_cap = *cap == 0 ? UNDO_INIT_CAP : *cap;
    while (new_cap < needed) new_cap *= 2;
    *stack = editor_safe_realloc(*stack, new_cap * sizeof(UndoRecord));
    *cap = new_cap;
}

//将操作压入Undo栈，并且自动清空Redo栈
void editor_push_undo(EditorConfig *ec,OpType type,int y,int x,const char *text,int len) {
    if (ec->doc.is_action_locked) return; //撤销/重做中,不记录新操作
    //新操作打断重做链，清空Redo栈
    for (int i = 0; i < ec->doc.redo_count; i++) {
        free (ec->doc.redo_stack[i].text);
    }
    ec->doc.redo_count = 0;
    time_t now = time(NULL);

    //(防抖合并)，连续输入字符
    if (type == OP_INSERT_CHARS && ec->doc.undo_count > 0) {
        UndoRecord *last = &ec->doc.undo_stack[ec->doc.undo_count-1];
        if (last->type ==OP_INSERT_CHARS
            && last->cursor_y == y
            && last->cursor_x  + last->text_len== x
            && (now - last->timestamp < 2)) {
            last->text = editor_safe_realloc(last->text, last->text_len + len + 1);
            memcpy(last->text + last->text_len, text, len);
            last->text[last->text_len] = '\0';
            last->timestamp = now;
            return;
            }
    }

    //(防抖合并),连续删除字符
    if (type == OP_DELETE_CHARS && ec->doc.undo_count > 0) {
        UndoRecord *last = &ec->doc.undo_stack[ec->doc.undo_count-1];
        if (last->type == OP_DELETE_CHARS
            && last->cursor_y == y
            && last->cursor_x == x + len
            && (now - last->timestamp) < 2) {
            char *old_text = last->text;
            int old_len = last->text_len;
            char *new_text = editor_safe_realloc(NULL, old_len + len + 1);
            memcpy(new_text, text, len);
            memcpy(new_text + len, old_text, old_len);
            new_text[old_len + len] = '\0';
            free(old_text);
            last->text = new_text;
            last->cursor_x = x;
            last->text_len += len;
            last->timestamp = now;
            return;
            }
    }

    ensure_stack_cap(&ec->doc.undo_stack, &ec->doc.undo_cap, ec->doc.undo_count + 1);

    UndoRecord *rec = &ec->doc.undo_stack[ec->doc.undo_count++];
    rec->type = type;
    rec->cursor_x = x;
    rec->cursor_y = y;
    rec->text = editor_safe_realloc(NULL, len + 1);
    if (len > 0) memcpy(rec->text,text,len);
    rec->text[len] = '\0';
    rec->text_len = len;
    rec->timestamp = now;
}

static void execute_op(EditorConfig *ec, UndoRecord *rec, int is_undo) {
    ec->doc.is_action_locked = 1; // 上锁

    OpType effective_type = rec->type;
    if (is_undo) {
        if (effective_type == OP_INSERT_CHARS) effective_type = OP_DELETE_CHARS;
        else if (effective_type == OP_DELETE_CHARS) effective_type = OP_INSERT_CHARS;
        else if (effective_type == OP_INSERT_LINE) effective_type = OP_DELETE_LINE;
        else if (effective_type == OP_DELETE_LINE) effective_type = OP_INSERT_LINE;
    }

    switch (effective_type) {
    case OP_INSERT_CHARS:
        ec->view.cursor_y = rec->cursor_y;
        ec->view.cursor_x = rec->cursor_x;
        for (int i = 0; i < rec->text_len; i++) {
            editor_row_insert_char(ec, &ec->doc.row[ec->view.cursor_y], ec->view.cursor_x++, rec->text[i]);
        }
        break;
    case OP_DELETE_CHARS:
        ec->view.cursor_y = rec->cursor_y;
        for (int i = 0; i < rec->text_len; i++) {
            editor_row_del_char(ec, &ec->doc.row[ec->view.cursor_y], rec->cursor_x);
        }
        ec->view.cursor_x = rec->cursor_x;
        break;
    case OP_INSERT_LINE:
        ec->view.cursor_y = rec->cursor_y;
        ec->view.cursor_x = rec->cursor_x;
        editor_insert_newline(ec);
        break;
    case OP_DELETE_LINE:
        ec->view.cursor_y = rec->cursor_y + 1;
        ec->view.cursor_x = 0;
        editor_delChar(ec);
        ec->view.cursor_y = rec->cursor_y;
        ec->view.cursor_x = rec->cursor_x;
        break;
    }

    ec->doc.is_action_locked = 0; // 解锁
}

void editor_undo (EditorConfig *ec) {
    if (ec->doc.undo_count == 0) return;
    UndoRecord rec = ec->doc.undo_stack[--ec->doc.undo_count];
    execute_op(ec,&rec,1);
    ensure_stack_cap(&ec->doc.redo_stack, &ec->doc.redo_cap, ec->doc.redo_count + 1);
    ec->doc.redo_stack[ec->doc.redo_count++] = rec;
}

void editor_redo (EditorConfig *ec) {
    if (ec->doc.redo_count == 0) return;
    UndoRecord rec = ec->doc.redo_stack[--ec->doc.redo_count];
    execute_op(ec,&rec,0);
    ensure_stack_cap(&ec->doc.undo_stack, &ec->doc.undo_cap, ec->doc.undo_count + 1);
    ec->doc.undo_stack[ec->doc.undo_count++] = rec;
}

// ============================================================
//  插入行
// ============================================================

void editor_insert_raw(EditorConfig *ec, int at, const char *s, size_t len)
{
    if (at < 0 || at > ec->doc.num_rows) return;
    if (s == NULL) return;

    ec->doc.row = editor_safe_realloc(ec->doc.row, (ec->doc.num_rows + 1) * sizeof(EditorRow));

    if (at < ec->doc.num_rows)
        memmove(&ec->doc.row[at + 1], &ec->doc.row[at],
                sizeof(EditorRow) * (ec->doc.num_rows - at));

    ec->doc.row[at].size = len;
    ec->doc.row[at].chars = editor_safe_realloc(NULL, len + 1);
    memcpy(ec->doc.row[at].chars, s, len);
    ec->doc.row[at].chars[len] = '\0';

    ec->doc.row[at].cell_count = 0;
    ec->doc.row[at].cells = NULL;
    ec->doc.row[at].highlight_open_comment = 0;
    ec->doc.row[at].syntax_hash = 0;

    editor_update_row(ec, &ec->doc.row[at]);

    ec->doc.num_rows++;
    ec->doc.dirty++;
}

// ============================================================
//  行内插入字符
// ============================================================

void editor_row_insert_char(EditorConfig *ec, EditorRow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;

    row->chars = editor_safe_realloc(row->chars, row->size + 2);
    if (at < row->size)
        memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->chars[at] = c;
    row->size++;
    row->chars[row->size] = '\0';

    editor_update_row(ec, row);
    ec->doc.dirty++;
}

// ============================================================
//  插入字符（自动创建行）
// ============================================================

void editor_insert_char(EditorConfig *ec, int c) {
    if (ec->view.cursor_y == ec->doc.num_rows)
        editor_insert_raw(ec, ec->doc.num_rows, "", 0);
    EditorRow *row = &ec->doc.row[ec->view.cursor_y];

    //如果开启了覆盖模式，且光标没有越界，直接原地覆写
    if (ec->ui.overwrite_mode && ec->view.cursor_x < row->size){
        if ((c & 0xC0) != 0x80){
            int old_len = utf8_step(&row->chars[ec->view.cursor_x], row->size - ec->view.cursor_x).bytes;

            memmove(&row->chars[ec->view.cursor_x], &row->chars[ec->view.cursor_x + old_len], row->size - ec->view.cursor_x - old_len + 1);
            row->size -= old_len;
        }
    }

    char byte_c = (char)c;
    editor_push_undo(ec, OP_INSERT_CHARS, ec->view.cursor_y, ec->view.cursor_x, &byte_c, 1);
    editor_row_insert_char(ec,row,ec->view.cursor_x,c);
    ec->view.cursor_x++;
}

// ============================================================
//  Backspace
// ============================================================

void editor_delChar(EditorConfig *ec) {
    if (ec->doc.num_rows == 0) return;
    if (ec->view.cursor_y >= ec->doc.num_rows) return;

    EditorRow *row = &ec->doc.row[ec->view.cursor_y];

    if (ec->view.cursor_x > 0) {
        ec->view.cursor_x = editor_row_del_char(ec, row, ec->view.cursor_x - 1);
    } else if (ec->view.cursor_y > 0) {
        /* 行首退格：当前行合并到上一行末尾 */
        EditorRow *prev_row = &ec->doc.row[ec->view.cursor_y - 1];
        int prev_len = prev_row->size;             // 合并前 prev_row 的长度

        editor_push_undo(ec, OP_DELETE_LINE, ec->view.cursor_y - 1, prev_len, NULL, 0);

        prev_row->chars = editor_safe_realloc(prev_row->chars,
                              prev_len + row->size + 1);
        memcpy(&prev_row->chars[prev_len], row->chars, row->size);
        prev_row->chars[prev_len + row->size] = '\0';
        prev_row->size = prev_len + row->size;

        editor_update_row(ec, prev_row);

        /* 释放当前行，下移后续行填补空缺 */
        free(row->chars);
        free(row->cells);

        int remaining = ec->doc.num_rows - ec->view.cursor_y - 1;  // 当前行后的行数
        if (remaining > 0)
            memmove(&ec->doc.row[ec->view.cursor_y],
                    &ec->doc.row[ec->view.cursor_y + 1],
                    remaining * sizeof(EditorRow));
        memset(&ec->doc.row[ec->doc.num_rows - 1], 0, sizeof(EditorRow));

        ec->doc.num_rows--;
        ec->view.cursor_y--;
        ec->view.cursor_x = prev_len;                   // 光标落在合并后位置
        ec->doc.dirty++;
    }
}

int editor_row_del_char(EditorConfig *ec, EditorRow *row, int at){
    if (at < 0 || at >= row->size) return ec->view.cursor_x;

    int j = 0;
    int start_at = 0;
    int len = 0;
    while (j <= at && j < row->size) {
        start_at = j;
        Utf8Step s = utf8_step(&row->chars[j], row->size - j);
        len = s.bytes;
        j += len;
    }

    editor_push_undo(ec, OP_DELETE_CHARS, ec->view.cursor_y, start_at, &row->chars[start_at], len);

    memmove(&row->chars[start_at], &row->chars[start_at + len],
            row->size - (start_at + len) + 1);
    row->size -= len;

    editor_update_row(ec, row);
    ec->doc.dirty++;

    return start_at;
}

// ============================================================
//  Delete
// ============================================================

void editor_deleteCharAtCursor(EditorConfig *ec) {
    if (ec->doc.num_rows == 0) return;
    if (ec->view.cursor_y >= ec->doc.num_rows) return;

    EditorRow *row = &ec->doc.row[ec->view.cursor_y];

    if (ec->view.cursor_x < row->size) {
        int char_len = utf8_step(&row->chars[ec->view.cursor_x], row->size - ec->view.cursor_x).bytes;
        memmove(&row->chars[ec->view.cursor_x],
                &row->chars[ec->view.cursor_x + char_len],
                row->size - ec->view.cursor_x - char_len + 1);
        row->size -= char_len;
        editor_update_row(ec, row);
        ec->doc.dirty++;
    } else if (ec->view.cursor_y < ec->doc.num_rows - 1) {
        /* 行尾删除：下一行合并到当前行末尾 */
        EditorRow *next_row = &ec->doc.row[ec->view.cursor_y + 1];

        row->chars = editor_safe_realloc(row->chars,
                              row->size + next_row->size + 1);
        memcpy(&row->chars[row->size], next_row->chars, next_row->size);
        row->chars[row->size + next_row->size] = '\0';
        row->size = row->size + next_row->size;
        editor_update_row(ec, row);

        free(next_row->chars);
        free(next_row->cells);

        int remaining = ec->doc.num_rows - ec->view.cursor_y - 2;  // next_row 后的行数
        if (remaining > 0)
            memmove(&ec->doc.row[ec->view.cursor_y + 1],
                    &ec->doc.row[ec->view.cursor_y + 2],
                    remaining * sizeof(EditorRow));
        memset(&ec->doc.row[ec->doc.num_rows - 1], 0, sizeof(EditorRow));

        ec->doc.num_rows--;
        ec->doc.dirty++;
    }
}

// ============================================================
//  Enter（拆分行）
// ============================================================

void editor_insert_newline(EditorConfig *ec) {
    editor_push_undo(ec, OP_INSERT_LINE, ec->view.cursor_y, ec->view.cursor_x, NULL, 0);
    if (ec->view.cursor_x == 0) {
        /* 行首 Enter：在当前行上方插入空行 */
        editor_insert_raw(ec, ec->view.cursor_y, "", 0);
        if (ec->view.cursor_y < ec->doc.num_rows - 1)
            ec->view.cursor_y++;
    } else {
        /* 行中 Enter：光标右侧拆成新行 */
        EditorRow *row = &ec->doc.row[ec->view.cursor_y];
        int new_size = row->size - ec->view.cursor_x;   // 拆出的新行长度

        editor_insert_raw(ec, ec->view.cursor_y + 1,
                         row->chars + ec->view.cursor_x, new_size);

        row = &ec->doc.row[ec->view.cursor_y];               // insert_raw 可能 realloc，重新取指针
        row->chars[ec->view.cursor_x] = '\0';
        row->size = ec->view.cursor_x;
        editor_update_row(ec, row);
        ec->view.cursor_y++;
    }
    ec->view.cursor_x = 0;
    ec->doc.dirty++;
}

// ============================================================
//  状态消息
// ============================================================

void editor_set_status_message(EditorConfig *ec, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ec->ui.statusmsg, sizeof(ec->ui.statusmsg), fmt, ap);
    va_end(ap);
    ec->ui.statusmsg_time = time(NULL);
}

// ============================================================
//  提示输入（迷你 readline）
// ============================================================

char *editor_prompt(EditorConfig *ec, const char *prompt)
{
    size_t buf_size = 128;               // 初始缓冲区大小
    char *buf = editor_safe_realloc(NULL, buf_size);
    size_t buf_len = 0;                  // 当前输入长度
    buf[0] = '\0';

    while (1) {
        editor_set_status_message(ec, prompt, buf);  // 把当前输入显示在消息栏
        editor_refresh_screen(ec);
        int c = terminal_read_key(&ec->term);
        if (c == KEY_BACKSPACE) {
            if (buf_len != 0) {
                int del_len = 1;
                while (del_len < 4 && buf_len - del_len > 0
                       && ((unsigned char)buf[buf_len - del_len] & 0xC0) == 0x80)
                    del_len++;
                buf_len -= del_len;
                buf[buf_len] = '\0';
            }
        } else if (c == KEY_ESC) {
            editor_set_status_message(ec, "");
            free(buf);
            return NULL;                 // ESC → 取消
        } else if (c == '\r') {
            if (buf_len != 0) {
                editor_set_status_message(ec, "");
                return buf;              // Enter → 返回输入
            } else {
                free(buf);
                return NULL;             // 空输入视为取消
            }
        } else if (!iscntrl(c) && c < 256) {
            if (buf_len == buf_size - 1) {
                buf_size *= 2;
                buf = editor_safe_realloc(buf, buf_size);
            }
            buf[buf_len++] = c;
            buf[buf_len] = '\0';
        }
    }
}

// ============================================================
//  搜索（环形搜索 + 高亮 state machine）
// ============================================================

/*
 * editor_find: search-query state machine.
 *
 * On non-NULL query:
 *   1. Store query in ec->ui.search_query (frees previous).
 *   2. Broadcast editor_update_syntax() to every row — this
 *      paints HL_MATCH on all occurrences in render-space.
 *   3. Walk rows in a ring (starting from cursor_y) via strstr
 *      on row->chars and jump to the first match.
 *
 * On NULL (ESC / empty Enter):
 *   4. Free ec->ui.search_query and re-run syntax to clear HL_MATCH.
 */
void editor_find(EditorConfig *ec) {
    char *query = editor_prompt(ec, "Search: %s (ESC to cancel)");

    if (query == NULL) {
        /* 取消搜索：清除高亮 */
        if (ec->ui.search_query) {
            free(ec->ui.search_query);
            ec->ui.search_query = NULL;
            for (int i = 0; i < ec->doc.num_rows; i++)
                editor_update_syntax(&ec->doc, &ec->settings, &ec->ui, &ec->doc.row[i]);  // 重新扫描 → 清除 HL_MATCH
        }
        return;
    }

    /* 更新搜索词 */
    if (ec->ui.search_query)
        free(ec->ui.search_query);
    ec->ui.search_query = query;

    /* 全行广播：标记所有匹配为 HL_MATCH */
    for (int i = 0; i < ec->doc.num_rows; i++)
        editor_update_syntax(&ec->doc, &ec->settings, &ec->ui, &ec->doc.row[i]);

    /* 1. 优先搜索当前行光标之后 */
    EditorRow *cur = &ec->doc.row[ec->view.cursor_y];
    char *match = strstr(cur->chars + ec->view.cursor_x, query);
    if (match) {
        ec->view.cursor_x = (int)(match - cur->chars);
        ec->view.row_off = ec->view.cursor_y;
    } else {
        /* 2. 跨行环形搜索：从下一行开始，绕回当前行 */
        int current = ec->view.cursor_y;
        for (int i = 0; i < ec->doc.num_rows; i++) {
            current += 1;
            if (current >= ec->doc.num_rows) current = 0;
            EditorRow *row = &ec->doc.row[current];
            match = strstr(row->chars, query);
            if (match) {
                ec->view.cursor_y = current;
                ec->view.cursor_x = (int)(match - row->chars);
                ec->view.row_off = ec->view.cursor_y;
                break;
            }
        }
    }
}

// ============================================================
//  查找下一个（Ctrl+G）
// ============================================================

void editor_find_next(EditorConfig *ec) {
    if (ec->ui.search_query == NULL) {
        editor_find(ec);
        return;
    }
    if (ec->doc.num_rows == 0) return;

    int qlen = strlen(ec->ui.search_query);
    if (qlen == 0) return;

    /* 同行搜索：从光标后找下一个匹配 */
    EditorRow *cur = &ec->doc.row[ec->view.cursor_y];
    if (ec->view.cursor_x + qlen <= cur->size) {
        char *same_line = strstr(cur->chars + ec->view.cursor_x + qlen,
                                  ec->ui.search_query);
        if (same_line) {
            ec->view.cursor_x = (int)(same_line - cur->chars);
            return;
        }
    }

    /* 跨行环形搜索 */
    int start = ec->view.cursor_y;
    for (int i = 1; i < ec->doc.num_rows; i++) {
        int idx = (start + i) % ec->doc.num_rows;
        EditorRow *row = &ec->doc.row[idx];
        char *match = strstr(row->chars, ec->ui.search_query);
        if (match) {
            ec->view.cursor_y = idx;
            ec->view.cursor_x = (int)(match - row->chars);
            ec->view.row_off = ec->view.cursor_y;
            if (idx <= start)
                editor_set_status_message(ec, "Search wrapped around");
            return;
        }
    }

    editor_set_status_message(ec, "No more matches");
}

// ============================================================
//  行号跳转（Ctrl+J）
// ============================================================

void editor_goto_line(EditorConfig *ec) {
    char *query = editor_prompt(ec, "Go to line: %s (ESC to cancel)");
    if (query == NULL) return;

    int line = atoi(query);
    free(query);

    if (line < 1) line = 1;
    if (line > ec->doc.num_rows) line = ec->doc.num_rows;

    ec->view.cursor_y = line - 1;
    ec->view.cursor_x = 0;
    ec->view.row_off = ec->view.cursor_y;
}


