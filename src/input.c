/*
 * Key dispatch: maps raw key codes to handler functions.
 *
 * g_key_bindings[] is a small LUT (key_code → void action(ec)).
 * editor_process_key reads one key, walks the table, and falls
 * through to printable-character insertion for 32..126.
 *
 * Wrappers like editor_insert_tab exist solely to match the
 * dispatch signature (EditorConfig*) when the core function
 * takes a raw char value.
 */
#include "common.h"
#include "input.h"
#include "editor.h"
#include "undo.h"
#include "terminal.h"
#include "fileio.h"
#include <stdio.h>
#include <stdlib.h>


// ============================================================
//  光标移动
// ============================================================

static void editor_cursor_move_up(EditorConfig *ec) {
    if (ec->view.cursor_y > 0) {
        EditorRow *cur_row = &ec->doc.row[ec->view.cursor_y];
        int rx = editor_row_cx_to_rx(ec, cur_row, ec->view.cursor_x);
        ec->view.cursor_y--;
        ec->view.cursor_x = editor_row_rx_to_cx(ec, &ec->doc.row[ec->view.cursor_y], rx);
    }
}

static void editor_cursor_move_down(EditorConfig *ec) {
    if (ec->view.cursor_y < ec->doc.num_rows - 1) {
        EditorRow *cur_row = &ec->doc.row[ec->view.cursor_y];
        int rx = editor_row_cx_to_rx(ec, cur_row, ec->view.cursor_x);
        ec->view.cursor_y++;
        ec->view.cursor_x = editor_row_rx_to_cx(ec, &ec->doc.row[ec->view.cursor_y], rx);
    }
}

static void editor_cursor_move_right(EditorConfig *ec) {
    if (ec->view.cursor_y < ec->doc.num_rows) {
        EditorRow *row = &ec->doc.row[ec->view.cursor_y];
        if (ec->view.cursor_x < row->size) {
            Utf8Step s = utf8_step(&row->chars[ec->view.cursor_x], row->size - ec->view.cursor_x);
            ec->view.cursor_x += s.bytes;
            if (ec->view.cursor_x > row->size) ec->view.cursor_x = row->size;
        }
    }
}

static void editor_cursor_move_left(EditorConfig *ec) {
    if (ec->view.cursor_x > 0) {
        EditorRow *row = &ec->doc.row[ec->view.cursor_y];
        int j = 0;
        int prev_start = 0;
        while (j < ec->view.cursor_x && j < row->size) {
            prev_start = j;
            Utf8Step s = utf8_step(&row->chars[j], row->size - j);
            j += s.bytes;
        }
        ec->view.cursor_x = prev_start;
    } else if (ec->view.cursor_y > 0) {
        ec->view.cursor_y--;
        ec->view.cursor_x = ec->doc.row[ec->view.cursor_y].size;
    }
}

// ============================================================
//  退出
// ============================================================

static void editor_quit(EditorConfig *ec) {
    disable_raw_mode(&ec->term);
    DWORD written;
    WriteFile(ec->term.hOUT, "\x1b[?1049l", 8, &written, NULL);  // 退出备用屏幕缓冲区
    printf("\x1b[2J");                                         // 清屏
    printf("\x1b[H");                                          // 光标归位
    exit(0);
}

// ============================================================
//  Tab 插入（dispatch 适配层）
// ============================================================

static void editor_insert_tab(EditorConfig *ec) {
    editor_insert_char(ec, '\t');
}

// ============================================================
//  Insert 切换（覆盖/插入模式）
// ============================================================

static void editor_toggle_overwrite(EditorConfig *ec) {
    ec->ui.overwrite_mode = !ec->ui.overwrite_mode;
    editor_set_status_message(ec, ec->ui.overwrite_mode
                              ? "Overwrite mode"
                              : "Insert mode");
}

static void editor_quick_open(EditorConfig *ec){
    if (ec->doc.dirty > 0){
        editor_set_status_message(ec,"WARNING: Unsaved changes! Save with Ctrl+S first.");
        return;
    }

    char *new_filename = editor_prompt(ec,"Quick Open: %s (ESC to cancel)");
    if (new_filename == NULL){
        editor_set_status_message(ec,"WARNING: No filename provided,Open aborted");
        return;
    }

    editor_free_all_row(ec);

    ec->view.cursor_x = 0;
    ec->view.cursor_y = 0;
    ec->view.col_off = 0;
    ec->view.row_off = 0;
    ec->ui.overwrite_mode = 0;

    editor_open(ec,new_filename);
    free(new_filename);

    editor_set_status_message(ec,"Open complete! Successfully loaded file.");
}

// ============================================================
//  按键绑定表
// ============================================================

typedef struct {
    int key_code;
    void (*action)(EditorConfig *);
} KeyBinding;

static const KeyBinding g_key_bindings[] = {
    {KEY_ARROW_UP,    editor_cursor_move_up},
    {KEY_ARROW_DOWN,  editor_cursor_move_down},
    {KEY_ARROW_LEFT,  editor_cursor_move_left},
    {KEY_ARROW_RIGHT, editor_cursor_move_right},
    {KEY_CTRL_Q,      editor_quit},
    {KEY_CTRL_S,      editor_save},
    {KEY_ENTER,       editor_insert_newline},
    {KEY_BACKSPACE,   editor_delChar},
    {KEY_TAB,         editor_insert_tab},
    {KEY_DEL,         editor_deleteCharAtCursor},
    {KEY_CTRL_F,      editor_find},
    {KEY_CTRL_G,      editor_find_next},
    {KEY_INS,         editor_toggle_overwrite},
    {KEY_CTRL_P,editor_quick_open},
    {KEY_CTRL_J,editor_goto_line},
    {KEY_CTRL_Z,editor_undo},
    {KEY_CTRL_Y,editor_redo}
};

static const int g_bindings_count = sizeof(g_key_bindings) / sizeof(g_key_bindings[0]);

// ============================================================
//  按键分发
// ============================================================

void editor_process_key(EditorConfig *ec) {
    int c = terminal_read_key(&ec->term);

    for (int i = 0; i < g_bindings_count; i++) {
        if (g_key_bindings[i].key_code == c) {
            g_key_bindings[i].action(ec);
            return;
        }
    }

    if (c >= 32 && c <= 255)
        editor_insert_char(ec, c);
}
