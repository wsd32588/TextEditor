# TextEditor 项目函数参考

> 生成日期: 2026-05-16  
> 用途: 给 AI（Gemini 等）理解项目代码结构

---

## common.c — 工具函数

### `editor_safe_realloc`

| 项目 | 内容 |
|------|------|
| 作用 | OOM 安全的内存重分配。realloc 失败时打印错误并 exit(1)，调用方无需检查 NULL。 |
| 文件 | `src/common.c:9-17` |

```c
void *editor_safe_realloc(void *ptr, size_t size)
{
    void *temp = realloc(ptr, size);
    if (temp == NULL) {
        perror("realloc failed: out of memory");
        exit(1);
    }
    return temp;
}
```

### `editor_strdup`

| 项目 | 内容 |
|------|------|
| 作用 | strdup 的跨平台实现（MSVC 没有 strdup）。malloc + memcpy，OOM 时 exit。 |
| 文件 | `src/common.c:23-33` |

```c
char *editor_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p == NULL) { perror("malloc failed"); exit(EXIT_FAILURE); }
    memcpy(p, s, len);
    return p;
}
```

---

## terminal.c — 终端控制

### `enable_raw_mode`

| 项目 | 内容 |
|------|------|
| 作用 | 关闭 ECHO 和 LINE_INPUT，启用 VT 输入/输出处理。备份原始模式到 EditorConfig。 |
| 文件 | `src/terminal.c:19-36` |

```c
void enable_raw_mode(EditorConfig *ec) {
    ec->hIN = GetStdHandle(STD_INPUT_HANDLE);
    ec->hOUT = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(ec->hIN, &ec->dwOriginalInMode)) return;
    if (!GetConsoleMode(ec->hOUT, &ec->dwOriginalOutMode)) return;
    DWORD raw_in = ec->dwOriginalInMode;
    raw_in &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    raw_in |= ENABLE_WINDOW_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(ec->hIN, raw_in);
    DWORD raw_out = ec->dwOriginalOutMode;
    raw_out |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(ec->hOUT, raw_out);
    ec->raw_mode = 1;
}
```

### `disable_raw_mode`

| 项目 | 内容 |
|------|------|
| 作用 | 恢复终端到原始模式。有 raw_mode 标志保护，防止重复调用。 |
| 文件 | `src/terminal.c:11-17` |

```c
void disable_raw_mode(EditorConfig *ec) {
    if (ec->raw_mode) {
        SetConsoleMode(ec->hIN, ec->dwOriginalInMode);
        SetConsoleMode(ec->hOUT, ec->dwOriginalOutMode);
        ec->raw_mode = 0;
    }
}
```

### `terminal_get_window_size`

| 项目 | 内容 |
|------|------|
| 作用 | 通过 GetConsoleScreenBufferInfo 获取终端窗口行列数。返回 0 成功，-1 失败。 |
| 文件 | `src/terminal.c:38-46` |

```c
int terminal_get_window_size(EditorConfig *ec, int *rows, int *cols) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(ec->hOUT, &csbi)) return -1;
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 0;
}
```

### `terminal_read_char_timeout` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 带 100ms 超时的单字符读取。用于解析 \x1b 转义序列中的后续字节。返回 1 读到字符，0 超时。 |
| 文件 | `src/terminal.c:78-87` |

```c
static int terminal_read_char_timeout(EditorConfig *ec, char *c)
{
    if (WaitForSingleObject(ec->hIN, 100) == WAIT_OBJECT_0) {
        DWORD bytes_read;
        if (ReadFile(ec->hIN, c, 1, &bytes_read, NULL) && bytes_read == 1)
            return 1;
    }
    return 0;
}
```

### `terminal_read_key`

| 项目 | 内容 |
|------|------|
| 作用 | 阻塞读取一个按键。对普通字节直接返回 ASCII 值。对 \x1b 开头序列，读取后续 2-3 字节并与映射表匹配，返回 EditorKeys 枚举值。映射表支持方向键、Home/End/Del/PgUp/PgDn。无法识别的序列返回 KEY_ESC。 |
| 文件 | `src/terminal.c:93-122` |

```c
int terminal_read_key(EditorConfig *ec) {
    DWORD bytes_read;
    char c = '\0';
    while (ReadFile(ec->hIN, &c, 1, &bytes_read, NULL)) {
        if (bytes_read == 1) break;
    }
    if (c == '\x1b') {
        char seq[4] = {0};
        if (!terminal_read_char_timeout(ec, &seq[0])) return KEY_ESC;
        if (!terminal_read_char_timeout(ec, &seq[1])) return KEY_ESC;
        if (seq[0] == '[' && seq[1] >= '0' && seq[1] <= '9') {
            if (!terminal_read_char_timeout(ec, &seq[2])) return KEY_ESC;
        }
        for (int i = 0; i < g_esc_count; i++) {
            if (strcmp(seq, g_esc_mappings[i].seq) == 0)
                return g_esc_mappings[i].key_code;
        }
        return KEY_ESC;
    }
    return c;
}
```

---

## input.c — 按键处理

### `editor_cursor_move_up` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 光标上移。cursor_y > 0 时减 1。 |
| 文件 | `src/input.c:13-15` | `ec->cursor_y--;` |

### `editor_cursor_move_down` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 光标下移。cursor_y < num_rows - 1 时加 1。 |
| 文件 | `src/input.c:17-19` | `ec->cursor_y++;` |

### `editor_cursor_move_right` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 光标右移。cursor_x < row->size 时加 1（不超出当前行内容末尾）。 |
| 文件 | `src/input.c:21-26` |

```c
static void editor_cursor_move_right(EditorConfig *ec) {
    if (ec->cursor_y < ec->num_rows) {
        EditorRow *row = &ec->row[ec->cursor_y];
        if (ec->cursor_x < row->size) ec->cursor_x++;
    }
}
```

### `editor_cursor_move_left` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 光标左移。cursor_x > 0 时减 1。 |
| 文件 | `src/input.c:28-30` | `ec->cursor_x--;` |

### `editor_quit` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 退出编辑器：恢复原始模式，退出备用屏幕缓冲区，清屏，exit(0)。 |
| 文件 | `src/input.c:36-43` |

```c
static void editor_quit(EditorConfig *ec) {
    disable_raw_mode(ec);
    DWORD written;
    WriteFile(ec->hOUT, "\x1b[?1049l", 8, &written, NULL);
    printf("\x1b[2J"); printf("\x1b[H");
    exit(0);
}
```

### `editor_insert_tab` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 插入制表符。直接调用 editor_insert_char(ec, '\t') 。 |
| 文件 | `src/input.c:49-51` | `editor_insert_char(ec, '\t');` |

### `editor_process_key`

| 项目 | 内容 |
|------|------|
| 作用 | 按键分发入口。调用 terminal_read_key 获取键码，遍历 g_key_bindings 函数指针表执行对应动作。未绑定的可打印字符（32-126）直接插入。 |
| 文件 | `src/input.c:81-94` |

```c
void editor_process_key(EditorConfig *ec) {
    int c = terminal_read_key(ec);
    for (int i = 0; i < g_bindings_count; i++) {
        if (g_key_bindings[i].key_code == c) {
            g_key_bindings[i].action(ec);
            return;
        }
    }
    if (c >= 32 && c <= 126) {
        editor_insert_char(ec, c);
    }
}
```

### 按键绑定表 （全局数据）

| 项目 | 内容 |
|------|------|
| 作用 | 键码 → 函数的映射表。新增功能键时在此添加绑定。 |
| 文件 | `src/input.c:62-73` |

```c
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
};
```

---

## editor.c — 核心编辑操作

### `editor_init`

| 项目 | 内容 |
|------|------|
| 作用 | 初始化编辑器状态为零值，切入终端备用屏幕缓冲区（\x1b[?1049h），获取窗口尺寸。 |
| 文件 | `src/editor.c:16-35` |

```c
void editor_init(EditorConfig *ec) {
    ec->cursor_x = 0; ec->cursor_y = 0;
    ec->row_off = 0; ec->col_off = 0;
    ec->num_rows = 0; ec->row = NULL;
    ec->dirty = 0; ec->filename = NULL; ec->syntax = NULL;
    WriteFile(ec->hOUT, "\x1b[?1049h", 8, &written, NULL);
    terminal_get_window_size(ec, &ec->screen_rows, &ec->screen_cols);
}
```

### `editor_insert_raw`

| 项目 | 内容 |
|------|------|
| 作用 | 在行数组的指定位置插入一行。处理行数组扩容和 memmove 后移。初始化新行的所有字段，然后调用 editor_update_syntax。 |
| 文件 | `src/editor.c:41-77` |

```c
void editor_insert_raw(EditorConfig *ec, int at, const char *s, size_t len)
{
    if (at < 0 || at > ec->num_rows) return;
    ec->row = editor_safe_realloc(ec->row, (ec->num_rows + 1) * sizeof(EditorRow));
    if (at < ec->num_rows) {
        memmove(&ec->row[at + 1], &ec->row[at], sizeof(EditorRow) * (ec->num_rows - at));
        for (int j = at + 1; j < ec->num_rows; j++) ec->row[j].index++;
    }
    // 初始化行...
    ec->row[at].chars = malloc(len + 1);
    memcpy(ec->row[at].chars, s, len);
    ec->row[at].chars[len] = '\0';
    editor_update_syntax(ec, &ec->row[at]);
    ec->num_rows++; ec->dirty++;
}
```

### `editor_row_insert_char`

| 项目 | 内容 |
|------|------|
| 作用 | 在指定行的指定位置插入一个字符。处理行内 memmove 后移，更新语法高亮。 |
| 文件 | `src/editor.c:83-96` |

```c
void editor_row_insert_char(EditorConfig *ec, EditorRow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = editor_safe_realloc(row->chars, row->size + 2);
    if (at < row->size)
        memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->chars[at] = c;
    row->size++;
    row->chars[row->size] = '\0';
    editor_update_syntax(ec, row);
    ec->dirty++;
}
```

### `editor_insert_char`

| 项目 | 内容 |
|------|------|
| 作用 | 在光标位置插入字符。如果光标在最后一行之后，先创建空行。光标右移。 |
| 文件 | `src/editor.c:102-108` |

```c
void editor_insert_char(EditorConfig *ec, int c) {
    if (ec->cursor_y == ec->num_rows)
        editor_insert_raw(ec, ec->num_rows, "", 0);
    editor_row_insert_char(ec, &ec->row[ec->cursor_y], ec->cursor_x, c);
    ec->cursor_x++;
}
```

### `editor_delChar`

| 项目 | 内容 |
|------|------|
| 作用 | Backspace 处理。分两种情况：光标不在行首 → 删除光标前字符；光标在行首且不在首行 → 将当前行内容合并到上一行后，删除当前行。 |
| 文件 | `src/editor.c:114-156` |

```c
void editor_delChar(EditorConfig *ec) {
    if (ec->num_rows == 0 || ec->cursor_y >= ec->num_rows) return;
    EditorRow *row = &ec->row[ec->cursor_y];
    if (ec->cursor_x > 0) {
        // 删除光标前字符
        memmove(&row->chars[ec->cursor_x - 1], &row->chars[ec->cursor_x],
                row->size - ec->cursor_x + 1);
        row->size--; ec->cursor_x--;
    } else if (ec->cursor_y > 0) {
        // 合并到上一行
        EditorRow *prev_row = &ec->row[ec->cursor_y - 1];
        prev_row->chars = editor_safe_realloc(prev_row->chars,
                            prev_len + row->size + 1);
        memcpy(&prev_row->chars[prev_len], row->chars, row->size);
        // ... 删除当前行，memmove 后移 ...
        ec->num_rows--; ec->cursor_y--; ec->cursor_x = prev_len;
    }
    ec->dirty++;
}
```

### `editor_deleteCharAtCursor`

| 项目 | 内容 |
|------|------|
| 作用 | Delete 键处理。分两种情况：光标不在行尾 → 删除光标处字符；光标在行尾且不是最后一行 → 将下一行内容合并到当前行后，删除下一行。 |
| 文件 | `src/editor.c:162-199` |

```c
void editor_deleteCharAtCursor(EditorConfig *ec) {
    if (ec->num_rows == 0 || ec->cursor_y >= ec->num_rows) return;
    EditorRow *row = &ec->row[ec->cursor_y];
    if (ec->cursor_x < row->size) {
        // 删除光标处字符
        memmove(&row->chars[ec->cursor_x], &row->chars[ec->cursor_x + 1],
                row->size - ec->cursor_x);
        row->size--;
    } else if (ec->cursor_y < ec->num_rows - 1) {
        // 合并下一行到当前行
        EditorRow *next_row = &ec->row[ec->cursor_y + 1];
        row->chars = editor_safe_realloc(row->chars,
                        row->size + next_row->size + 1);
        memcpy(&row->chars[row->size], next_row->chars, next_row->size);
        row->size = row->size + next_row->size;
        // ... 删除下一行，memmove 后移 ...
        ec->num_rows--;
    }
    ec->dirty++;
}
```

### `editor_insert_newline`

| 项目 | 内容 |
|------|------|
| 作用 | Enter 键处理。光标在行首（cursor_x==0）→ 在当前位置插入空行；光标在行中 → 将光标后的内容分割为新行。 |
| 文件 | `src/editor.c:205-223` |

```c
void editor_insert_newline(EditorConfig *ec) {
    if (ec->cursor_x == 0) {
        editor_insert_raw(ec, ec->cursor_y, "", 0);
    } else {
        EditorRow *row = &ec->row[ec->cursor_y];
        editor_insert_raw(ec, ec->cursor_y + 1,
                         row->chars + ec->cursor_x, row->size - ec->cursor_x);
        row->chars[ec->cursor_x] = '\0';
        row->size = ec->cursor_x;
        ec->cursor_y++;
    }
    ec->cursor_x = 0; ec->dirty++;
}
```

### `editor_set_status_message`

| 项目 | 内容 |
|------|------|
| 作用 | 设置状态栏消息（格式化为 statusmsg 80 字节缓冲区），同时记录时间戳用于 5 秒超时清除。 |
| 文件 | `src/editor.c:229-236` |

```c
void editor_set_status_message(EditorConfig *ec, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ec->statusmsg, sizeof(ec->statusmsg), fmt, ap);
    va_end(ap);
    ec->statusmsg_time = time(NULL);
}
```

### `editor_prompt`

| 项目 | 内容 |
|------|------|
| 作用 | 提示输入函数（未完成）。目前只显示动态提示信息，返回的输入逻辑尚未实现。可用作搜索等交互式输入的骨架。 |
| 文件 | `src/editor.c:238-253` |

```c
char *editor_prompt(EditorConfig *ec, const char *prompt)
{
    size_t buf_size = 128;
    char *buf = malloc(buf_size);
    buf[0] = '\0';
    while (1) {
        editor_set_status_message(ec, prompt, buf);
        editor_refresh_screen(ec);
        int c = terminal_read_key(ec);
        // TODO: 处理输入字符/回车/ESC，填充 buf
    }
}
```

---

## fileio.c — 文件 I/O

### `editor_save`

| 项目 | 内容 |
|------|------|
| 作用 | 保存文件。如果 filename 为 NULL 则用 "untitled.txt"。逐行写入，每行后加 \n。保存后 dirty 置 0 并显示成功消息。 |
| 文件 | `src/fileio.c:12-31` |

```c
void editor_save(EditorConfig *ec)
{
    if (ec->filename == NULL)
        ec->filename = editor_strdup("untitled.txt");
    FILE *fp = fopen(ec->filename, "w");
    if (!fp) return;
    for (int i = 0; i < ec->num_rows; i++) {
        if (ec->row[i].size > 0)
            fwrite(ec->row[i].chars, 1, ec->row[i].size, fp);
        fwrite("\n", 1, 1, fp);
    }
    fclose(fp);
    ec->dirty = 0;
    editor_set_status_message(ec, "Saved successfully!");
}
```

### `editor_open`

| 项目 | 内容 |
|------|------|
| 作用 | 打开文件。先通过 editor_select_syntax_highlight 根据后缀匹配语法规则。逐字符读取（fgetc），遇到 \n 则插入一行。最后一行如果无 \n 结尾也插入。 |
| 文件 | `src/fileio.c:37-73` |

```c
void editor_open(EditorConfig *ec, const char *filename)
{
    free(ec->filename);
    ec->filename = editor_strdup(filename);
    editor_select_syntax_highlight(ec);
    FILE *fp = fopen(filename, "r");
    if (!fp) return;
    char *line = NULL; size_t linecap = 0; size_t len = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') {
            editor_insert_raw(ec, ec->num_rows, line, len);
            len = 0;
        } else {
            if (len >= linecap) {
                linecap = (linecap == 0) ? 128 : linecap * 2;
                line = editor_safe_realloc(line, linecap);
            }
            line[len++] = (char)c;
        }
    }
    if (len > 0) editor_insert_raw(ec, ec->num_rows, line, len);
    free(line); fclose(fp); ec->dirty = 0;
}
```

---

## syntax.c — 语法高亮

### `is_separator` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 判断字符是否为单词分隔符（空白、\0、标点符号）。用于切分关键词边界。 |
| 文件 | `src/syntax.c:18-20` |

```c
static int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}
```

### `editor_update_syntax`

| 项目 | 内容 |
|------|------|
| 作用 | 对单行进行语法高亮扫描。空行提前返回。扫描流程：先检查单行注释 → 在前一个字符为分隔符时匹配关键词（末尾 '|' 为 HL_KEYWORD2，否则 HL_KEYWORD1）。注意：多行注释和字符串字面量尚未实现。 |
| 文件 | `src/syntax.c:42-96` |

```c
void editor_update_syntax(EditorConfig *ec, EditorRow *row) {
    if (row->size == 0) { free(row->high_light); row->high_light = NULL; return; }
    row->high_light = editor_safe_realloc(row->high_light, row->size);
    memset(row->high_light, HL_NORMAL, row->size);
    if (ec->syntax == NULL) return;

    char **keywords = ec->syntax->keywords;
    char *scs = ec->syntax->single_line_comment_start;
    int scs_len = scs ? strlen(scs) : 0;
    int prev_sep = 1; int in_string = 0; int i = 0;

    while (i < row->size) {
        char c = row->chars[i];
        // 单行注释 //
        if (scs_len && !strncmp(&row->chars[i], scs, scs_len)) {
            memset(&row->high_light[i], HL_COMMENT, row->size - i);
            break;
        }
        // 关键词匹配
        if (prev_sep) {
            for (int j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2) klen--;
                if (!strncmp(&row->chars[i], keywords[j], klen)
                    && is_separator(row->chars[i + klen])) {
                    memset(&row->high_light[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen; keyword_match = 1; break;
                }
            }
            if (keyword_match) { prev_sep = 0; continue; }
        }
        prev_sep = is_separator(c);
        i++;
    }
}
```

### `editor_select_syntax_highlight`

| 项目 | 内容 |
|------|------|
| 作用 | 根据文件名后缀（如 .c/.h/.cpp）在 g_high_light_data_base 中匹配语法规则。匹配到则设置 ec->syntax，否则为 NULL。 |
| 文件 | `src/syntax.c:102-121` |

```c
void editor_select_syntax_highlight(EditorConfig *ec) {
    ec->syntax = NULL;
    if (ec->filename == NULL) return;
    char *ext = strrchr(ec->filename, '.');
    for (int j = 0; j < g_high_light_data_base_entries; j++) {
        struct EditorSyntax *s = &g_high_light_data_base[j];
        int i = 0;
        while (s->file_match[i]) {
            int is_ext = (s->file_match[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->file_match[i])) ||
                (!is_ext && strstr(ec->filename, s->file_match[i]))) {
                ec->syntax = s; return;
            }
            i++;
        }
    }
}
```

---

## render.c — 渲染引擎

### `abuf_append` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 向 AppendBuffer 追加数据。用 editor_safe_realloc 自动扩容，追加后指针可能变化。 |
| 文件 | `src/render.c:13-19` |

```c
static void abuf_append(AppendBuffer *ab, const char *s, int len)
{
    char *new_ptr = editor_safe_realloc(ab->b, ab->len + len);
    memcpy(new_ptr + ab->len, s, len);
    ab->b = new_ptr; ab->len += len;
}
```

### `abuf_free` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 释放 AppendBuffer 内部缓冲区。 |
| 文件 | `src/render.c:21-24` | `free(ab->b);` |

### `editor_syntax_to_color` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 将 HighlightType 枚举值映射为 ANSI 颜色码。COMMENT→36(青), KEYWORD1→33(黄), KEYWORD2→32(绿), STRING→35(紫), NUMBER→31(红), MATCH→34(蓝), 默认→37(白)。 |
| 文件 | `src/render.c:30-41` |

```c
static int editor_syntax_to_color(int hl) {
    switch (hl) {
    case HL_COMMENT:  return 36; case HL_KEYWORD1: return 33;
    case HL_KEYWORD2: return 32; case HL_STRING:   return 35;
    case HL_NUMBER:   return 31; case HL_MATCH:    return 34;
    default:          return 37;
    }
}
```

### `editor_scroll` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 根据光标位置调整 row_off/col_off 滚动偏移。光标移出可视区域时自动滚动。状态栏占用 2 行，所以可用行数为 screen_rows - 2。 |
| 文件 | `src/render.c:47-58` |

```c
static void editor_scroll(EditorConfig *ec)
{
    if (ec->cursor_y < ec->row_off)
        ec->row_off = ec->cursor_y;
    if (ec->cursor_y >= ec->row_off + ec->screen_rows - 2)
        ec->row_off = ec->cursor_y - (ec->screen_rows - 2) + 1;
    if (ec->cursor_x < ec->col_off)
        ec->col_off = ec->cursor_x;
    if (ec->cursor_x >= ec->col_off + ec->screen_cols)
        ec->col_off = ec->cursor_x - ec->screen_cols + 1;
}
```

### `editor_draw_rows` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 绘制文本内容区域（screen_rows - 2 行）。对每行：先 cls（\x1b[K），对文件内容行逐字符应用语法高亮 ANSI 着色，对空白行显示 ~。当前 tab 扩展尚未实现（render 字段为空，直接用 chars 输出）。 |
| 文件 | `src/render.c:64-110` |

```c
static void editor_draw_rows(AppendBuffer *ab, EditorConfig *ec)
{
    for (int y = 0; y < ec->screen_rows - 2; y++) {
        int filerow = y + ec->row_off;
        abuf_append(ab, "\x1b[K", 3);
        if (filerow < ec->num_rows) {
            int len = ec->row[filerow].size - ec->col_off;
            if (len < 0) len = 0;
            if (len > ec->screen_cols) len = ec->screen_cols;
            int current_color = -1;
            for (int j = 0; j < len; j++) {
                int ci = ec->col_off + j;
                unsigned char hl = ec->row[filerow].high_light
                                   ? ec->row[filerow].high_light[ci] : HL_NORMAL;
                // 颜色切换逻辑...
            }
        } else {
            abuf_append(ab, "~", 1);
        }
        abuf_append(ab, "\r\n", 2);
    }
}
```

### `editor_draw_status_bar` (static)

| 项目 | 内容 |
|------|------|
| 作用 | 绘制状态栏（反白显示）。左侧：文件名 + 行数 + (modified) 标记。右侧：当前行号/总行数。空格填充至对齐。 |
| 文件 | `src/render.c:116-144` |

```c
static void editor_draw_status_bar(AppendBuffer *ab, EditorConfig *ec)
{
    abuf_append(ab, "\x1b[7m", 4);  // 反白
    char status[80], rstatus[80];
    const char *display_name = ec->filename ? ec->filename : "[No Name]";
    snprintf(status, sizeof(status), "%.20s - %d lines %s",
             display_name, ec->num_rows, ec->dirty ? "(modified)" : "");
    snprintf(rstatus, sizeof(rstatus), "%d/%d",
             ec->cursor_y + 1, ec->num_rows);
    // ... 填充空格至对齐 ...
    abuf_append(ab, "\x1b[m", 3);    // 复位
    abuf_append(ab, "\r\n", 2);
}
```

### `editor_draw_message_bar`

| 项目 | 内容 |
|------|------|
| 作用 | 绘制消息栏。在 statusmsg 非空且当前时间距设置时间 < 5 秒时显示消息，否则清空。 |
| 文件 | `src/render.c:150-160` |

```c
void editor_draw_message_bar(AppendBuffer *ab, EditorConfig *ec)
{
    abuf_append(ab, "\x1b[K", 3);
    int msglen = strlen(ec->statusmsg);
    if (msglen > ec->screen_cols) msglen = ec->screen_cols;
    if (msglen > 0 && time(NULL) - ec->statusmsg_time < 5)
        abuf_append(ab, ec->statusmsg, msglen);
}
```

### `editor_refresh_screen`

| 项目 | 内容 |
|------|------|
| 作用 | 全屏刷新入口（每帧调用一次）。流程：更新滚动 → 构建 AppendBuffer（隐藏光标 → 定位到(1,1) → 画文本行 → 画状态栏 → 画消息栏 → 定位光标到实际位置 → 显示光标）→ WriteFile 一次性写入 → 释放缓冲区。 |
| 文件 | `src/render.c:166-189` |

```c
void editor_refresh_screen(EditorConfig *ec)
{
    editor_scroll(ec);
    AppendBuffer ab = ABUF_INIT;
    abuf_append(&ab, "\x1b[?25l", 6);  // 隐藏光标
    abuf_append(&ab, "\x1b[H", 3);     // 光标归位
    editor_draw_rows(&ab, ec);
    editor_draw_status_bar(&ab, ec);
    editor_draw_message_bar(&ab, ec);
    // 定位光标到实际位置
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
             (ec->cursor_y - ec->row_off) + 1,
             (ec->cursor_x - ec->col_off) + 1);
    abuf_append(&ab, buf, strlen(buf));
    abuf_append(&ab, "\x1b[?25h", 6);  // 显示光标
    WriteFile(ec->hOUT, ab.b, ab.len, &bytes_written, NULL);
    abuf_free(&ab);
}
```

---

## main.c — 程序入口

### `main`

| 项目 | 内容 |
|------|------|
| 作用 | 程序入口。初始化 VT 处理（IN/OUT），启用原始模式，初始化编辑器。如果 argv 指定了文件则打开。设置帮助提示消息。进入渲染→输入主循环。 |
| 文件 | `src/Main/main.c:9-47` |

```c
int main(int argc, char *argv[])
{
    EditorConfig ec = {0};
    // 启用 VT 处理（IN + OUT）
    enable_raw_mode(&ec);
    editor_init(&ec);
    editor_set_status_message(&ec, "HELP: Ctrl-S = save | Ctrl-Q = quit");
    if (argc >= 2) editor_open(&ec, argv[1]);
    while (1) {
        editor_refresh_screen(&ec);
        editor_process_key(&ec);
    }
}
```

---

## 关键数据类型

### `EditorConfig` — 编辑器总状态
文件: `include/common.h:58-78`

| 字段 | 类型 | 用途 |
|------|------|------|
| cursor_x, cursor_y | int | 逻辑光标位置（字符坐标） |
| render_x | int | 渲染光标 X（用于 tab 展开后定位） |
| row_off, col_off | int | 垂直/水平滚动偏移 |
| screen_rows, screen_cols | int | 终端窗口尺寸 |
| num_rows | int | 文本总行数 |
| row | EditorRow* | 行数组（动态） |
| dirty | int | 未保存标记 |
| filename | char* | 当前文件名 |
| statusmsg | char[80] | 状态栏消息缓冲区 |
| statusmsg_time | time_t | 消息时间戳 |
| syntax | EditorSyntax* | 当前语法高亮规则 |
| raw_mode | int | 是否处于原始模式 |
| hIN, hOUT | HANDLE | 标准输入/输出句柄 |
| dwOriginalIn/OutMode | DWORD | 原始控制台模式备份 |

### `EditorRow` — 单行数据
文件: `include/common.h:44-52`

| 字段 | 类型 | 用途 |
|------|------|------|
| index | int | 行号 |
| size | int | chars 实际长度 |
| render_size | int | 渲染宽度（tab 展开后） |
| chars | char* | 原始字符文本 |
| render | char* | 渲染后文本（含 tab 展开，未实现） |
| high_light | unsigned char* | 语法高亮标记数组 |
| highlight_open_comment | int | 上行是否有未闭合注释 |

### `AppendBuffer` — 输出缓冲区
文件: `include/render.h:7-10`

```c
typedef struct { char *b; int len; } AppendBuffer;
#define ABUF_INIT { NULL, 0 }
```

批量累积 ANSI 输出，每帧一次性 WriteFile，减少系统调用。

---

## 函数调用关系图

```
main()
├── enable_raw_mode()           [terminal]
├── editor_init()               [editor]
│   └── terminal_get_window_size() [terminal]
├── editor_open()               [fileio]
│   ├── editor_select_syntax_highlight() [syntax]
│   └── editor_insert_raw() × N  [editor]
│       └── editor_update_syntax()  [syntax]
└── while(1):
    ├── editor_refresh_screen()  [render]
    │   ├── editor_scroll()
    │   ├── editor_draw_rows()
    │   │   └── editor_syntax_to_color() × N
    │   ├── editor_draw_status_bar()
    │   ├── editor_draw_message_bar()
    │   └── abuf_append() / abuf_free()
    └── editor_process_key()     [input]
        ├── terminal_read_key()  [terminal]
        │   └── terminal_read_char_timeout() × 2~3
        └── g_key_bindings[].action()
            ├── editor_cursor_move_*()
            ├── editor_quit()
            │   └── disable_raw_mode() [terminal]
            ├── editor_save()          [fileio]
            ├── editor_insert_newline()
            ├── editor_delChar() / editor_deleteCharAtCursor()
            ├── editor_insert_tab()
            │   └── editor_insert_char()
            └── editor_insert_char() (default: printable)
                ├── editor_insert_raw()
                └── editor_row_insert_char()
                    └── editor_update_syntax()  [syntax]
```
