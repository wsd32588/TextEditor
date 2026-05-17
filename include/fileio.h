#ifndef TEXTEDITOR_FILEIO_H
#define TEXTEDITOR_FILEIO_H

#include "common.h"

void editor_save(EditorConfig *ec);
void editor_open(EditorConfig *ec, const char *filename);

#endif // TEXTEDITOR_FILEIO_H
