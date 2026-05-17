#ifndef TEXTEDITOR_RENDER_H
#define TEXTEDITOR_RENDER_H

#include "common.h"

// 追加缓冲区：批量累积输出后一次性写入，减少系统调用
typedef struct {
    char *b;
    int len;
} AppendBuffer;

#define ABUF_INIT { NULL, 0 }

void editor_refresh_screen(EditorConfig *ec);

#endif // TEXTEDITOR_RENDER_H
