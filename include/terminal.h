#ifndef TEXTEDITOR_TERMINAL_H
#define TEXTEDITOR_TERMINAL_H

#include "common.h"

void disable_raw_mode(EditorConfig *ec);
void enable_raw_mode(EditorConfig *ec);
int terminal_read_key(EditorConfig *ec);
int terminal_get_window_size(EditorConfig *ec, int *rows, int *cols);

#endif // TEXTEDITOR_TERMINAL_H
