/*
 * Screen rendering: builds an AppendBuffer of ANSI escape sequences
 * and text, then flushes it to the console in one WriteFile call.
 *
 * Draw order (top to bottom):
 *   editor_draw_rows      — text lines with line numbers + color
 *   editor_draw_status_bar — file name / row count / cursor position
 *   editor_draw_message_bar — ephemeral status messages (5 s timeout)
 *
 * Horizontal scrolling uses render-space coordinates: col_off is a
 * visual-column offset into the cells[] array (tabs already expanded),
 * so the display loop simply iterates cells without any live
 * tab expansion.
 */
#include "common.h"
#include "render.h"
#include "editor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <Windows.h>

// ============================================================
//  追加缓冲区
// ============================================================

static void abuf_append(AppendBuffer *ab, const char *s, int len)
{
    char *new_ptr = editor_safe_realloc(ab->b, ab->len + len); // 扩容
    memcpy(new_ptr + ab->len, s, len);
    ab->b = new_ptr;
    ab->len += len;
}

static void abuf_free(AppendBuffer *ab)
{
    free(ab->b);
}

// ============================================================
//  语法高亮 → ANSI 颜色
// ============================================================

static int editor_syntax_to_color(int hl)
{
    switch (hl) {
    case HL_COMMENT:    return 36;  // 青色
    case HL_ML_COMMENT: return 36;  // 青色（多行注释）
    case HL_KEYWORD1: return 33;  // 黄色（类型关键字）
    case HL_KEYWORD2: return 32;  // 绿色（控制流关键字）
    case HL_STRING:   return 35;  // 品红
    case HL_NUMBER:   return 31;  // 红色
    case HL_MATCH:    return 34;  // 蓝色（搜索匹配）
    default:          return 37;  // 白色
    }
}

// ============================================================
//  视口滚动
// ============================================================

static void editor_scroll(EditorConfig *ec)
{
    /* 垂直滚动：确保光标在可见区内 */
    if (ec->view.cursor_y < ec->view.row_off)
        ec->view.row_off = ec->view.cursor_y;
    if (ec->view.cursor_y >= ec->view.row_off + ec->view.screen_rows - 2)
        ec->view.row_off = ec->view.cursor_y - (ec->view.screen_rows - 2) + 1;

    /* 水平滚动：光标 render-space 位置 → col_off */
    int rx = 0;                               // 光标在 render-space 的列
    if (ec->view.cursor_y < ec->doc.num_rows)
        rx = editor_row_cx_to_rx(ec, &ec->doc.row[ec->view.cursor_y], ec->view.cursor_x);

    if (rx < ec->view.col_off)                     // 光标出了左边界
        ec->view.col_off = rx;
    if (rx >= ec->view.col_off + ec->view.screen_cols - ec->view.gutter_width - 1)  // 光标超出右边界
        ec->view.col_off = rx - (ec->view.screen_cols - ec->view.gutter_width - 1) + 1;
    if (ec->view.col_off < 0) ec->view.col_off = 0;
}

// ============================================================
//  文本行绘制
// ============================================================

static void editor_draw_rows(AppendBuffer *ab, EditorConfig *ec)
{
    for (int y = 0; y < ec->view.screen_rows - 2; y++) {
        int filerow = y + ec->view.row_off;           // 文件中实际的行号

        abuf_append(ab, "\x1b[K", 3);            // 清空本行

        char ln_buf[32];
        int ln_len;
        if (filerow < ec->doc.num_rows) {
            /* 行号（青色） */
            ln_len = snprintf(ln_buf, sizeof(ln_buf), "\x1b[36m%*d \x1b[39m",
                              ec->view.gutter_width, filerow + 1);
            abuf_append(ab, ln_buf, ln_len);

            int available_cols = ec->view.screen_cols - ec->view.gutter_width - 1;

            /* Walk cells[] by visual columns: map col_off → cell index,
             * then measure how many cells fit within available_cols. */
            int cell_start = 0;
            int current_rx = 0;
            while (cell_start < ec->doc.row[filerow].cell_count && current_rx < ec->view.col_off) {
                current_rx += ec->doc.row[filerow].cells[cell_start].display_width;
                cell_start++;
            }

            int cell_n = 0;
            int printed_rx = 0;
            while (cell_start + cell_n < ec->doc.row[filerow].cell_count) {
                int w = ec->doc.row[filerow].cells[cell_start + cell_n].display_width;
                if (printed_rx + w > available_cols) break;
                printed_rx += w;
                cell_n++;
            }

            int current_color = -1;
            for (int i = 0; i < cell_n; i++) {
                RenderCell *cell = &ec->doc.row[filerow].cells[cell_start + i];
                unsigned char hl = cell->hl;

                if (hl == HL_NORMAL) {
                    if (current_color != -1) {
                        abuf_append(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                } else {
                    int color = editor_syntax_to_color(hl);
                    if (color != current_color) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abuf_append(ab, buf, clen);
                        current_color = color;
                    }
                }

                char encoded[5];
                int elen = utf8_encode(cell->codepoint, encoded);
                abuf_append(ab, encoded, elen);
            }
            abuf_append(ab, "\x1b[39m", 5);
        } else {
            ln_len = snprintf(ln_buf, sizeof(ln_buf), "\x1b[36m%*s \x1b[39m",
                              ec->view.gutter_width, "~");
            abuf_append(ab, ln_buf, ln_len);       // 空白行显示波浪号
        }

        abuf_append(ab, "\r\n", 2);
    }
}

// ============================================================
//  状态栏
// ============================================================

static void editor_draw_status_bar(AppendBuffer *ab, EditorConfig *ec)
{
    abuf_append(ab, "\x1b[7m", 4);               // 反白
    char status[80], rstatus[80];

    const char *display_name = ec->doc.filename ? ec->doc.filename : "[No Name]";
    const char *line_label = (ec->doc.num_rows == 1) ? "line" : "lines";
    const char *dirty_marker = ec->doc.dirty ? "(modified)" : "";
    int len = snprintf(status, sizeof(status), "%.20s - %d %s %s",
                       display_name, ec->doc.num_rows, line_label, dirty_marker);

    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                        ec->view.cursor_y + 1, ec->doc.num_rows);
    if (len > ec->view.screen_cols) len = ec->view.screen_cols;

    abuf_append(ab, status, len);

    /* 左对齐文件名，右对齐行号 */
    while (len < ec->view.screen_cols) {
        if (ec->view.screen_cols - len == rlen) {     // 刚好够放 rstatus
            abuf_append(ab, rstatus, rlen);
            break;
        }
        abuf_append(ab, " ", 1);
        len++;
    }

    abuf_append(ab, "\x1b[m", 3);                // 复位属性
    abuf_append(ab, "\r\n", 2);
}

// ============================================================
//  消息栏
// ============================================================

static void editor_draw_message_bar(AppendBuffer *ab, EditorConfig *ec)
{
    abuf_append(ab, "\x1b[K", 3);                // 清空本行
    int msglen = strlen(ec->ui.statusmsg);
    if (msglen > ec->view.screen_cols) msglen = ec->view.screen_cols;

    if (msglen > 0 && time(NULL) - ec->ui.statusmsg_time < 5)  // 5 秒超时
        abuf_append(ab, ec->ui.statusmsg, msglen);
}

// ============================================================
//  全屏刷新（每帧入口）
// ============================================================

void editor_refresh_screen(EditorConfig *ec)
{
    /* 计算行号栏宽度：至少 4 位 */
    int max_lines = ec->doc.num_rows > 0 ? ec->doc.num_rows : 1;
    int digits = 1;
    while (max_lines >= 10) {
        digits++;
        max_lines /= 10;
    }
    if (digits < 4)
        digits = 4;
    ec->view.gutter_width = digits + 1;               // 数字 + 1 空格

    editor_scroll(ec);

    AppendBuffer ab = ABUF_INIT;
    abuf_append(&ab, "\x1b[?25l", 6);            // 隐藏光标
    abuf_append(&ab, "\x1b[H", 3);               // 光标归位 (1,1)

    editor_draw_rows(&ab, ec);                   // 文本行
    editor_draw_status_bar(&ab, ec);              // 状态栏
    editor_draw_message_bar(&ab, ec);             // 消息栏

    /* 光标定位：屏幕坐标 = (cursor_y - row_off + 1, rx - col_off + gutter + 2) */
    int rx = 0;                                  // render-space 光标列
    if (ec->view.cursor_y < ec->doc.num_rows)
        rx = editor_row_cx_to_rx(ec, &ec->doc.row[ec->view.cursor_y], ec->view.cursor_x);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
             (ec->view.cursor_y - ec->view.row_off) + 1,
             (rx - ec->view.col_off) + ec->view.gutter_width + 2);
    abuf_append(&ab, buf, strlen(buf));

    abuf_append(&ab, "\x1b[?25h", 6);            // 显示光标

    DWORD bytes_written;
    WriteFile(ec->term.hOUT, ab.b, ab.len, &bytes_written, NULL);
    abuf_free(&ab);
}
