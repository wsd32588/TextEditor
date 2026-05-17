#ifndef TEXTEDITOR_COMMON_H
#define TEXTEDITOR_COMMON_H

#include <Windows.h>
#include <time.h>

// ============================================================
//  语法高亮类型
// ============================================================

typedef enum {
    HL_NORMAL = 0,
    HL_NONPRINT,
    HL_COMMENT,
    HL_ML_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
} HighlightType;

typedef enum {
    HL_FLAG_NONE         = 0,
    HL_HIGHLIGHT_STRINGS = (1 << 0),
    HL_HIGHLIGHT_NUMBERS = (1 << 1)
} HighlightFlag;

/*
 * Syntax rule table for one programming language.
 *
 * file_match[] — suffixes (".c") or substrings to detect this language
 * keywords[]   — token list; trailing '|' = HL_KEYWORD2 (type keywords)
 * tab_stop     — indent width in spaces for this language
 */
struct EditorSyntax {
    char **file_match;
    char **keywords;
    char *single_line_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags;
    int tab_stop;
};

// ============================================================
//  文本行
// ============================================================

/*
 * One line of text and its display derivatives.
 *
 * Data flow:  input → chars → editor_update_row()
 *                   ├── render[]  (tabs → spaces, sized render_size)
 *                   └── editor_update_syntax()
 *                        └── high_light[]  (per-byte colour, sized render_size)
 *
 * Invariant: after any chars mutation, editor_update_row() must run
 * to rebuild render[] and re-trigger highlighting.
 */
typedef struct {
    int size;                  // chars 字节数（逻辑长度，\t 算 1 字节）
    int render_size;           // render 字节数（物理长度，\t 已展开）
    char *chars;               // 原始文本（含 \t）
    char *render;              // 显示用文本（\t → 空格）
    unsigned char *high_light; // 颜色标记数组（render-space 索引）
    int highlight_open_comment; // 上行是否有未闭合多行注释
} EditorRow;

// ============================================================
//  编辑器状态
// ============================================================

/*
 * Root state threaded through every function (no globals).
 *
 * Coordinates use two domains:
 *   cursor_x/y   — chars-space (logical, \t = 1 byte)
 *   col_off      — render-space (physical, \t = tab_stop columns)
 *   row_off      — first visible row index
 *
 * Conversions: editor_row_cx_to_rx(cursor_x) → render-space column.
 *
 * Syntax highlighting auto-detects from filename via
 * editor_select_syntax_highlight() and sets tab_stop accordingly.
 */
typedef struct {
    /* ---- 光标 ---- */
    int cursor_x, cursor_y;      // chars-space 光标位置

    /* ---- 滚动偏移 ---- */
    int row_off;                 // 垂直：首个可见行的索引
    int col_off;                 // 水平：render-space 偏移量

    /* ---- 终端尺寸 ---- */
    int screen_rows;             // 可视行数
    int screen_cols;             // 可视列数

    /* ---- 文本数据 ---- */
    int num_rows;                // 总行数
    EditorRow *row;              // 行数组（动态分配）

    /* ---- 文件状态 ---- */
    int dirty;                   // >0 表示有未保存修改
    char *filename;              // 当前文件路径（NULL = 未命名）

    /* ---- 状态/消息栏 ---- */
    char statusmsg[80];          // 底部消息文本
    time_t statusmsg_time;       // 消息时间戳（5 秒后自动清除）

    /* ---- 语法高亮 ---- */
    struct EditorSyntax *syntax; // 当前语言规则（NULL = 纯文本）
    char *search_query;          // 搜索关键词（NULL = 无搜索高亮）

    /* ---- 排版 ---- */
    int gutter_width;            // 行号栏宽度（动态计算）
    int tab_stop;                // 当前缩进宽度（来自 syntax 或默认 4）

    int overwrite_mode;          // 0 为插入模式，1 为覆盖模式

    /* ---- 终端模式 ---- */
    int raw_mode;                // 是否处于原始模式
    HANDLE hIN;                  // 标准输入句柄
    HANDLE hOUT;                 // 标准输出句柄
    DWORD dwOriginalInMode;      // 原始控制台输入模式备份
    DWORD dwOriginalOutMode;     // 原始控制台输出模式备份
} EditorConfig;

// ============================================================
//  常量 & 工具宏
// ============================================================

#define EDITOR_TAB_STOP 4

enum EditorKeys {
    KEY_NULL = 0,
    KEY_CTRL_C = 3,   KEY_CTRL_D = 4,   KEY_CTRL_F = 6,   KEY_CTRL_G = 7,
    KEY_CTRL_H = 8,   KEY_CTRL_P = 16,
    KEY_TAB = 9,
    KEY_ENTER = 13,
    KEY_CTRL_Q = 17,  KEY_CTRL_S = 19,  KEY_CTRL_U = 21,
    KEY_ESC = 27,
    KEY_BACKSPACE = 127,

    KEY_ARROW_LEFT  = 1000, KEY_ARROW_RIGHT,
    KEY_ARROW_UP,            KEY_ARROW_DOWN,
    KEY_DEL,                 KEY_HOME,     KEY_END,
    KEY_PAGE_UP,             KEY_PAGE_DOWN,
    KEY_INS,
};

// ============================================================
//  工具函数
// ============================================================

void *editor_safe_realloc(void *ptr, size_t size);
char *editor_strdup(const char *s);
int utf8_char_length(unsigned char c);
int utf8_char_display_width(unsigned char c);
#endif // TEXTEDITOR_COMMON_H
