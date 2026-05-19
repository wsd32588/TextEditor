#ifndef TEXTEDITOR_TERMINAL_H
#define TEXTEDITOR_TERMINAL_H

#include "common.h"

void disable_raw_mode(TerminalState *ts);
void enable_raw_mode(TerminalState *ts);
int terminal_read_key(TerminalState *ts);
int terminal_get_window_size(TerminalState *ts, ViewState *view);

#endif // TEXTEDITOR_TERMINAL_H
