#ifndef TEXTEDITOR_UNDO_H
#define TEXTEDITOR_UNDO_H

#include "common.h"

void editor_push_undo(EditorConfig *ec, OpType type, int y, int x, const char *text, int len);
void editor_undo(EditorConfig *ec);
void editor_redo(EditorConfig *ec);

#endif // TEXTEDITOR_UNDO_H
