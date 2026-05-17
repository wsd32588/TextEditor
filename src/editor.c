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
    ec->cursor_x = 0;
    ec->cursor_y = 0;
    ec->row_off = 0;
    ec->col_off = 0;
    ec->num_rows = 0;
    ec->row = NULL;
    ec->dirty = 0;
    ec->filename = NULL;
    ec->syntax = NULL;
    ec->gutter_width = 0;
    ec->search_query = NULL;
    ec->tab_stop = EDITOR_TAB_STOP;
    ec->overwrite_mode = 0;

    DWORD written;
    WriteFile(ec->hOUT, "\x1b[?1049h", 8, &written, NULL);

    if (terminal_get_window_size(ec, &ec->screen_rows, &ec->screen_cols) == -1) {
        printf("Failed to get window size!\n");
        exit(1);
    }
}
// ============================================================
//  清空当前文件占用的所有行内存
// ============================================================
void editor_free_all_row(EditorConfig *ec){
    for (int i = 0; i < ec->num_rows; i++){
        free(ec->row[i].chars);
        free(ec->row[i].render);
        free(ec->row[i].high_light);
    }
    free(ec->row);
    ec->row = NULL;
    ec->num_rows = 0;
    ec->dirty = 0;

    if (ec->filename){
        free(ec->filename);
        ec->filename = NULL;
    }
}

// ============================================================
//  坐标转换:  chars 空间 → render 空间
// ============================================================

int editor_row_cx_to_rx(EditorConfig *ec, EditorRow *row, int cx)
{
    int rx = 0;                         // 累加器：render-space 列号
    for (int j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (ec->tab_stop - 1) - (rx % ec->tab_stop);  // 跳到下个制表位
        rx++;
    }
    return rx;
}

// ============================================================
//  Render 缓冲区生成
// ============================================================

void editor_update_row(EditorConfig *ec, EditorRow *row)
{
    int tabs = 0;                       // 先数 tab 数，算 render 缓冲区大小
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') tabs++;
    }
    free(row->render);
    row->render = NULL;
    row->render = editor_safe_realloc(NULL,
                      row->size + tabs * (ec->tab_stop - 1) + 1);

    int idx = 0;                        // render 写入指针
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % ec->tab_stop != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->render_size = idx;

    editor_update_syntax(ec, row);
}

// ============================================================
//  插入行
// ============================================================

void editor_insert_raw(EditorConfig *ec, int at, const char *s, size_t len)
{
    if (at < 0 || at > ec->num_rows) return;
    if (s == NULL) return;

    ec->row = editor_safe_realloc(ec->row, (ec->num_rows + 1) * sizeof(EditorRow));

    if (at < ec->num_rows)
        memmove(&ec->row[at + 1], &ec->row[at],
                sizeof(EditorRow) * (ec->num_rows - at));

    ec->row[at].size = len;
    ec->row[at].chars = editor_safe_realloc(NULL, len + 1);
    memcpy(ec->row[at].chars, s, len);
    ec->row[at].chars[len] = '\0';

    ec->row[at].render_size = 0;
    ec->row[at].render = NULL;
    ec->row[at].high_light = NULL;
    ec->row[at].highlight_open_comment = 0;

    editor_update_row(ec, &ec->row[at]);

    ec->num_rows++;
    ec->dirty++;
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
    ec->dirty++;
}

// ============================================================
//  插入字符（自动创建行）
// ============================================================

void editor_insert_char(EditorConfig *ec, int c) {
    if (ec->cursor_y == ec->num_rows)
        editor_insert_raw(ec, ec->num_rows, "", 0);
    EditorRow *row = &ec->row[ec->cursor_y];

    //如果开启了覆盖模式，且光标没有越界，直接原地覆写
    if (ec->overwrite_mode && ec->cursor_x < row->size){
        row->chars[ec->cursor_x] = c;
        editor_update_row(ec, row);
        ec->cursor_x++;
        ec->dirty++;
    } else {
        editor_row_insert_char(ec, row, ec->cursor_x, c);
        ec->cursor_x++;
    }
}

// ============================================================
//  Backspace
// ============================================================

void editor_delChar(EditorConfig *ec) {
    if (ec->num_rows == 0) return;
    if (ec->cursor_y >= ec->num_rows) return;

    EditorRow *row = &ec->row[ec->cursor_y];

    if (ec->cursor_x > 0) {
        /* 行内退格：左侧字符覆盖当前位置 */
        memmove(&row->chars[ec->cursor_x - 1],
                &row->chars[ec->cursor_x],
                row->size - ec->cursor_x + 1);
        row->size--;
        editor_update_row(ec, row);
        ec->cursor_x--;
        ec->dirty++;
    } else if (ec->cursor_y > 0) {
        /* 行首退格：当前行合并到上一行末尾 */
        EditorRow *prev_row = &ec->row[ec->cursor_y - 1];
        int prev_len = prev_row->size;             // 合并前 prev_row 的长度

        prev_row->chars = editor_safe_realloc(prev_row->chars,
                              prev_len + row->size + 1);
        memcpy(&prev_row->chars[prev_len], row->chars, row->size);
        prev_row->chars[prev_len + row->size] = '\0';
        prev_row->size = prev_len + row->size;

        editor_update_row(ec, prev_row);

        /* 释放当前行，下移后续行填补空缺 */
        free(row->chars);
        free(row->render);
        free(row->high_light);

        int remaining = ec->num_rows - ec->cursor_y - 1;  // 当前行后的行数
        if (remaining > 0)
            memmove(&ec->row[ec->cursor_y],
                    &ec->row[ec->cursor_y + 1],
                    remaining * sizeof(EditorRow));
        memset(&ec->row[ec->num_rows - 1], 0, sizeof(EditorRow));

        ec->num_rows--;
        ec->cursor_y--;
        ec->cursor_x = prev_len;                   // 光标落在合并后位置
        ec->dirty++;
    }
}

// ============================================================
//  Delete
// ============================================================

void editor_deleteCharAtCursor(EditorConfig *ec) {
    if (ec->num_rows == 0) return;
    if (ec->cursor_y >= ec->num_rows) return;

    EditorRow *row = &ec->row[ec->cursor_y];

    if (ec->cursor_x < row->size) {
        /* 行内删除：右侧字符向左覆盖 */
        memmove(&row->chars[ec->cursor_x],
                &row->chars[ec->cursor_x + 1],
                row->size - ec->cursor_x);
        row->size--;
        editor_update_row(ec, row);
        ec->dirty++;
    } else if (ec->cursor_y < ec->num_rows - 1) {
        /* 行尾删除：下一行合并到当前行末尾 */
        EditorRow *next_row = &ec->row[ec->cursor_y + 1];

        row->chars = editor_safe_realloc(row->chars,
                              row->size + next_row->size + 1);
        memcpy(&row->chars[row->size], next_row->chars, next_row->size);
        row->chars[row->size + next_row->size] = '\0';
        row->size = row->size + next_row->size;
        editor_update_row(ec, row);

        free(next_row->chars);
        free(next_row->render);
        free(next_row->high_light);

        int remaining = ec->num_rows - ec->cursor_y - 2;  // next_row 后的行数
        if (remaining > 0)
            memmove(&ec->row[ec->cursor_y + 1],
                    &ec->row[ec->cursor_y + 2],
                    remaining * sizeof(EditorRow));
        memset(&ec->row[ec->num_rows - 1], 0, sizeof(EditorRow));

        ec->num_rows--;
        ec->dirty++;
    }
}

// ============================================================
//  Enter（拆分行）
// ============================================================

void editor_insert_newline(EditorConfig *ec) {
    if (ec->cursor_x == 0) {
        /* 行首 Enter：在当前行上方插入空行 */
        editor_insert_raw(ec, ec->cursor_y, "", 0);
        if (ec->cursor_y < ec->num_rows - 1)
            ec->cursor_y++;
    } else {
        /* 行中 Enter：光标右侧拆成新行 */
        EditorRow *row = &ec->row[ec->cursor_y];
        int new_size = row->size - ec->cursor_x;   // 拆出的新行长度

        editor_insert_raw(ec, ec->cursor_y + 1,
                         row->chars + ec->cursor_x, new_size);

        row = &ec->row[ec->cursor_y];               // insert_raw 可能 realloc，重新取指针
        row->chars[ec->cursor_x] = '\0';
        row->size = ec->cursor_x;
        editor_update_row(ec, row);
        ec->cursor_y++;
    }
    ec->cursor_x = 0;
    ec->dirty++;
}

// ============================================================
//  状态消息
// ============================================================

void editor_set_status_message(EditorConfig *ec, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ec->statusmsg, sizeof(ec->statusmsg), fmt, ap);
    va_end(ap);
    ec->statusmsg_time = time(NULL);
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
        int c = terminal_read_key(ec);

        if (c == KEY_BACKSPACE) {
            if (buf_len != 0)
                buf[--buf_len] = '\0';
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
        } else if (!iscntrl(c) && c < 128) {
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
 *   1. Store query in ec->search_query (frees previous).
 *   2. Broadcast editor_update_syntax() to every row — this
 *      paints HL_MATCH on all occurrences in render-space.
 *   3. Walk rows in a ring (starting from cursor_y) via strstr
 *      on row->chars and jump to the first match.
 *
 * On NULL (ESC / empty Enter):
 *   4. Free ec->search_query and re-run syntax to clear HL_MATCH.
 */
void editor_find(EditorConfig *ec) {
    char *query = editor_prompt(ec, "Search: %s (ESC to cancel)");

    if (query == NULL) {
        /* 取消搜索：清除高亮 */
        if (ec->search_query) {
            free(ec->search_query);
            ec->search_query = NULL;
            for (int i = 0; i < ec->num_rows; i++)
                editor_update_syntax(ec, &ec->row[i]);  // 重新扫描 → 清除 HL_MATCH
        }
        return;
    }

    /* 更新搜索词 */
    if (ec->search_query)
        free(ec->search_query);
    ec->search_query = query;

    /* 全行广播：标记所有匹配为 HL_MATCH */
    for (int i = 0; i < ec->num_rows; i++)
        editor_update_syntax(ec, &ec->row[i]);

    /* 环形搜索：从当前行往下找，找到就跳转 */
    int current = ec->cursor_y;              // 搜素起点（从下一行开始）
    for (int i = 0; i < ec->num_rows; i++) {
        current += 1;
        if (current >= ec->num_rows) current = 0;  // 绕回开头
        EditorRow *row = &ec->row[current];
        char *match = strstr(row->chars, query);    // chars-space 匹配
        if (match) {
            ec->cursor_y = current;
            ec->cursor_x = (int)(match - row->chars);  // chars-space 索引
            ec->row_off = ec->cursor_y;
            break;
        }
    }
}

// ============================================================
//  查找下一个（Ctrl+G）
// ============================================================

void editor_find_next(EditorConfig *ec) {
    if (ec->search_query == NULL) {
        editor_find(ec);
        return;
    }
    if (ec->num_rows == 0) return;

    int qlen = strlen(ec->search_query);
    if (qlen == 0) return;

    /* 同行搜索：从光标后找下一个匹配 */
    EditorRow *cur = &ec->row[ec->cursor_y];
    if (ec->cursor_x + qlen <= cur->size) {
        char *same_line = strstr(cur->chars + ec->cursor_x + qlen,
                                  ec->search_query);
        if (same_line) {
            ec->cursor_x = (int)(same_line - cur->chars);
            return;
        }
    }

    /* 跨行环形搜索 */
    int start = ec->cursor_y;
    for (int i = 1; i < ec->num_rows; i++) {
        int idx = (start + i) % ec->num_rows;
        EditorRow *row = &ec->row[idx];
        char *match = strstr(row->chars, ec->search_query);
        if (match) {
            ec->cursor_y = idx;
            ec->cursor_x = (int)(match - row->chars);
            ec->row_off = ec->cursor_y;
            if (idx <= start)
                editor_set_status_message(ec, "Search wrapped around");
            return;
        }
    }

    editor_set_status_message(ec, "No more matches");
}
