#ifndef TEXTEDITOR_SYNTAX_H
#define TEXTEDITOR_SYNTAX_H

#include "common.h"

void editor_select_syntax_highlight(EditorConfig *ec);
void editor_update_syntax(EditorConfig *ec, EditorRow *row);

extern struct EditorSyntax g_high_light_data_base[];
extern const int g_high_light_data_base_entries;

#endif // TEXTEDITOR_SYNTAX_H
