/*
 * Screen rendering: builds an AppendBuffer of ANSI escape sequences
 * and text, then flushes it to the console in one WriteFile call.
 *
 * Draw order (top to bottom):
 *   editor_draw_rows      — text lines with line numbers + colour
 *   editor_draw_status_bar — file name / row count / cursor position
 *   editor_draw_message_bar — ephemeral status messages (5 s timeout)
 *
 * Horizontal scrolling uses render-space coordinates: col_off is a
 * byte offset into row->render[] (tabs already expanded), so the
 * display loop simply reads render[col_off + j] without any live
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
    if (ec->cursor_y < ec->row_off)
        ec->row_off = ec->cursor_y;
    if (ec->cursor_y >= ec->row_off + ec->screen_rows - 2)
        ec->row_off = ec->cursor_y - (ec->screen_rows - 2) + 1;

    /* 水平滚动：光标 render-space 位置 → col_off */
    int rx = 0;                               // 光标在 render-space 的列
    if (ec->cursor_y < ec->num_rows)
        rx = editor_row_cx_to_rx(ec, &ec->row[ec->cursor_y], ec->cursor_x);

    if (rx < ec->col_off)                     // 光标出了左边界
        ec->col_off = rx;
    if (rx >= ec->col_off + ec->screen_cols - ec->gutter_width - 1)  // 光标超出右边界
        ec->col_off = rx - (ec->screen_cols - ec->gutter_width - 1) + 1;
    if (ec->col_off < 0) ec->col_off = 0;
}

// ============================================================
//  文本行绘制
// ============================================================

static void editor_draw_rows(AppendBuffer *ab, EditorConfig *ec)
{
    for (int y = 0; y < ec->screen_rows - 2; y++) {
        int filerow = y + ec->row_off;           // 文件中实际的行号

        abuf_append(ab, "\x1b[K", 3);            // 清空本行

        char ln_buf[32];
        int ln_len;
        if (filerow < ec->num_rows) {
            /* 行号（青色） */
            ln_len = snprintf(ln_buf, sizeof(ln_buf), "\x1b[36m%*d \x1b[39m",
                              ec->gutter_width, filerow + 1);
            abuf_append(ab, ln_buf, ln_len);

            int available_cols = ec->screen_cols - ec->gutter_width - 1;

            /* Align col_off to character boundary per row, then
             * trim len so it doesn't end mid-UTF-8-byte-sequence.
             * Otherwise partial multi-byte chars or embedded \x1b
             * may be consumed by the terminal's UTF-8 decoder,
             * corrupting subsequent ANSI escape output. */
            int render_off = ec->col_off;
            if (render_off > 0 && render_off < ec->row[filerow].render_size) {
                while (render_off > 0 && ((unsigned char)ec->row[filerow].render[render_off] & 0xC0) == 0x80)
                    render_off--;
            }

            int len = ec->row[filerow].render_size - render_off;
            if (len < 0) len = 0;
            if (len > available_cols) len = available_cols;

            /* 截断到字符边界 */
            if (len > 0) {
                int end_pos = render_off + len;
                while (end_pos > render_off && ((unsigned char)ec->row[filerow].render[end_pos - 1] & 0xC0) == 0x80)
                    end_pos--;
                if (end_pos > render_off && ((unsigned char)ec->row[filerow].render[end_pos - 1] & 0xC0) == 0xC0)
                    end_pos--;
                len = end_pos - render_off;
            }

            int current_color = -1;
            for (int j = 0; j < len; j++) {
                int rx_index = render_off + j;
                unsigned char hl = ec->row[filerow].high_light
                                   ? ec->row[filerow].high_light[rx_index]
                                   : HL_NORMAL;

                if (hl == HL_NORMAL) {
                    if (current_color != -1) {     // 切回默认色
                        abuf_append(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                } else {
                    int color = editor_syntax_to_color(hl);
                    if (color != current_color) {  // 切换 ANSI 颜色
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abuf_append(ab, buf, clen);
                        current_color = color;
                    }
                }
                abuf_append(ab, &ec->row[filerow].render[rx_index], 1);
            }
            abuf_append(ab, "\x1b[39m", 5);       // 复位颜色
        } else {
            ln_len = snprintf(ln_buf, sizeof(ln_buf), "\x1b[36m%*s \x1b[39m",
                              ec->gutter_width, "~");
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

    const char *display_name = ec->filename ? ec->filename : "[No Name]";
    const char *line_label = (ec->num_rows == 1) ? "line" : "lines";
    const char *dirty_marker = ec->dirty ? "(modified)" : "";
    int len = snprintf(status, sizeof(status), "%.20s - %d %s %s",
                       display_name, ec->num_rows, line_label, dirty_marker);

    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                        ec->cursor_y + 1, ec->num_rows);
    if (len > ec->screen_cols) len = ec->screen_cols;

    abuf_append(ab, status, len);

    /* 左对齐文件名，右对齐行号 */
    while (len < ec->screen_cols) {
        if (ec->screen_cols - len == rlen) {     // 刚好够放 rstatus
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

void editor_draw_message_bar(AppendBuffer *ab, EditorConfig *ec)
{
    abuf_append(ab, "\x1b[K", 3);                // 清空本行
    int msglen = strlen(ec->statusmsg);
    if (msglen > ec->screen_cols) msglen = ec->screen_cols;

    if (msglen > 0 && time(NULL) - ec->statusmsg_time < 5)  // 5 秒超时
        abuf_append(ab, ec->statusmsg, msglen);
}

// ============================================================
//  全屏刷新（每帧入口）
// ============================================================

void editor_refresh_screen(EditorConfig *ec)
{
    /* 计算行号栏宽度：至少 4 位 */
    int max_lines = ec->num_rows > 0 ? ec->num_rows : 1;
    int digits = 1;
    while (max_lines >= 10) {
        digits++;
        max_lines /= 10;
    }
    if (digits < 4)
        digits = 4;
    ec->gutter_width = digits + 1;               // 数字 + 1 空格

    editor_scroll(ec);

    AppendBuffer ab = ABUF_INIT;
    abuf_append(&ab, "\x1b[?25l", 6);            // 隐藏光标
    abuf_append(&ab, "\x1b[H", 3);               // 光标归位 (1,1)

    editor_draw_rows(&ab, ec);                   // 文本行
    editor_draw_status_bar(&ab, ec);              // 状态栏
    editor_draw_message_bar(&ab, ec);             // 消息栏

    /* 光标定位：屏幕坐标 = (cursor_y - row_off + 1, rx - col_off + gutter + 2) */
    int rx = 0;                                  // render-space 光标列
    if (ec->cursor_y < ec->num_rows)
        rx = editor_row_cx_to_rx(ec, &ec->row[ec->cursor_y], ec->cursor_x);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
             (ec->cursor_y - ec->row_off) + 1,
             (rx - ec->col_off) + ec->gutter_width + 1);
    abuf_append(&ab, buf, strlen(buf));

    abuf_append(&ab, "\x1b[?25h", 6);            // 显示光标

    DWORD bytes_written;
    WriteFile(ec->hOUT, ab.b, ab.len, &bytes_written, NULL);
    abuf_free(&ab);
}
