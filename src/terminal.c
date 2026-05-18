/*
 * Terminal raw mode and VT escape-sequence input.
 *
 * enable_raw_mode/disable_raw_mode toggle the Win32 console
 * flags so that every keystroke is delivered immediately
 * (no line buffering, no echo) and ANSI/VT input sequences
 * are recognised.
 *
 * terminal_read_key blocks for one keystroke.  When the byte
 * is \x1b (ESC) it attempts to read the continuation bytes
 * within a 100 ms window and looks them up in g_esc_mappings[]
 * to recognise cursor keys, Home/End, Delete, Page Up/Down.
 * If the continuation times out, the key is reported as ESC.
 */
#include "terminal.h"
#include "common.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================
//  原始模式
// ============================================================

void disable_raw_mode(EditorConfig *ec) {
    if (ec->raw_mode) {
        SetConsoleMode(ec->hIN, ec->dwOriginalInMode);
        SetConsoleMode(ec->hOUT, ec->dwOriginalOutMode);
        SetConsoleOutputCP(ec->uOriginalOutputCP);
        SetConsoleCP(ec->uOriginalInputCP);
        ec->raw_mode = 0;
    }
}

void enable_raw_mode(EditorConfig *ec) {
    ec->hIN = GetStdHandle(STD_INPUT_HANDLE);
    ec->hOUT = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Switch console I/O to UTF-8 so multi-byte characters survive the
     * code-page round-trip. Save originals to restore on quit. */
    ec->uOriginalOutputCP = GetConsoleOutputCP();
    ec->uOriginalInputCP  = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

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

// ============================================================
//  窗口尺寸
// ============================================================

int terminal_get_window_size(EditorConfig *ec, int *rows, int *cols) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(ec->hOUT, &csbi))
        return -1;

    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 0;
}

// ============================================================
//  转义序列 → 键码
// ============================================================

/*
 * VT escape sequence → EditorKey lookup.
 *
 * After receiving \x1b the driver reads up to 3 continuation
 * bytes.  The lookup string omits the leading \x1b; e.g.
 * "\x1b[A" (cursor up) is stored as "[A".
 */
typedef struct {
    const char *seq;
    int key_code;
} EscSeqMapping;

static const EscSeqMapping g_esc_mappings[] = {
    {"[A",  KEY_ARROW_UP},
    {"[B",  KEY_ARROW_DOWN},
    {"[C",  KEY_ARROW_RIGHT},
    {"[D",  KEY_ARROW_LEFT},
    {"[H",  KEY_HOME},
    {"[F",  KEY_END},
    {"[1~", KEY_HOME},
    {"[2~", KEY_INS},
    {"[3~", KEY_DEL},
    {"[4~", KEY_END},
    {"[5~", KEY_PAGE_UP},
    {"[6~", KEY_PAGE_DOWN},
    {"[7~", KEY_HOME},
    {"[8~", KEY_END},
    {"OH",  KEY_HOME},
    {"OF",  KEY_END}
};

static const int g_esc_count = sizeof(g_esc_mappings) / sizeof(g_esc_mappings[0]);

/*
 * Read one byte with a 100 ms timeout.
 * Returns 1 on success, 0 on timeout.
 */
static int terminal_read_char_timeout(EditorConfig *ec, char *c)
{
    if (WaitForSingleObject(ec->hIN, 100) == WAIT_OBJECT_0) {
        DWORD bytes_read;
        if (ReadFile(ec->hIN, c, 1, &bytes_read, NULL) && bytes_read == 1)
            return 1;
    }
    return 0;
}

// ============================================================
//  核心按键读取
// ============================================================

int terminal_read_key(EditorConfig *ec) {
    DWORD bytes_read;
    unsigned char c = '\0';

    /* 阻塞读取一个字节 */
    while (ReadFile(ec->hIN, &c, 1, &bytes_read, NULL)) {
        if (bytes_read == 1) break;
    }

    if (c == '\x1b') {
        char seq[8] = {0};                 // 后缀字节（不含前导 \x1b）

        if (!terminal_read_char_timeout(ec, &seq[0])) return KEY_ESC;
        if (!terminal_read_char_timeout(ec, &seq[1])) return KEY_ESC;

        if (seq[0] == '[' && seq[1] >= '0' && seq[1] <= '9')  // \x1b[5~ 等 3 字节序列
            if (!terminal_read_char_timeout(ec, &seq[2])) return KEY_ESC;

        for (int i = 0; i < g_esc_count; i++) {
            if (strcmp(seq, g_esc_mappings[i].seq) == 0)
                return g_esc_mappings[i].key_code;
        }
        return KEY_ESC;                    // 未知序列 → 当作 ESC
    }

    return c;                              // 普通字符 / Ctrl 键
}
