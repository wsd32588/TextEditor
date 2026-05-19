#ifndef TEXTEDITOR_EDITOR_H
#define TEXTEDITOR_EDITOR_H

#include "common.h"

void editor_init(EditorConfig *ec);
void editor_insert_raw(EditorConfig *ec, int at, const char *s, size_t len);
void editor_set_status_message(EditorConfig *ec, const char *fmt, ...);
void editor_insert_char(EditorConfig *ec, int c);
void editor_row_insert_char(EditorConfig *ec, EditorRow *row, int at, int c);
void editor_delChar(EditorConfig *ec);
void editor_deleteCharAtCursor(EditorConfig *ec);
void editor_insert_newline(EditorConfig *ec);
char *editor_prompt(EditorConfig *ec, const char *prompt);
void editor_find(EditorConfig *ec);
void editor_find_next(EditorConfig *ec);
void editor_goto_line(EditorConfig *ec);
int editor_row_cx_to_rx(EditorConfig *ec, EditorRow *row,int cx);
int editor_row_rx_to_cx(EditorConfig *ec, EditorRow *row, int rx);
void editor_update_row(EditorConfig *ec, EditorRow *row);
void editor_free_all_row(EditorConfig *ec);
int  editor_row_del_char(EditorConfig *ec, EditorRow *row, int at);

void editor_push_undo(EditorConfig *ec, OpType type, int y, int x, const char *text, int len);
void editor_undo(EditorConfig *ec);
void editor_redo(EditorConfig *ec);
void editor_emergency_save(EditorConfig *ec);

#endif // TEXTEDITOR_EDITOR_H
