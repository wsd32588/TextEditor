#ifndef TEXTEDITOR_COMMON_H
#define TEXTEDITOR_COMMON_H

#include <Windows.h>
#include <stdint.h>
#include <time.h>

// ============================================================
//  语法高亮类型
// ============================================================

typedef struct
{
    uint32_t codepoint;  // Unicode 码点
    int byte_len;        // 这个字符占几个字节
    int display_width;   // 终端视觉宽度
}Utf8Char;

Utf8Char utf8_decode(const char *s,int s_len);

typedef struct {
    int bytes;   // byte_len
    int cols;    // display_width
} Utf8Step;

Utf8Step utf8_step(const char *s, int s_len);

typedef struct {
    uint32_t codepoint;    // Unicode 码点 (用于识别字符)
    uint8_t  byte_len;     // 原文占用的字节数 (制表符占位填0)
    uint8_t  display_width;// 终端视觉宽度 (1或2)
    uint8_t  hl;           // 语法高亮颜色 (HL_NORMAL 等)
} RenderCell;

int utf8_encode(uint32_t cp,char *out);

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

typedef enum {
    OP_INSERT_CHARS,    // 插入字符（打字、粘贴）
    OP_DELETE_CHARS,    // 删除字符（Backspace, Delete）
    OP_INSERT_LINE,     // 换行（Enter键）
    OP_DELETE_LINE,     // 删除行（在行首按Backspace合并行）
}OpType;

typedef struct {
    OpType type;
    int cursor_y;     // 操作发生时的行号
    int cursor_x;     // 操作发生时的列号（chars 空间的字节索引）
    char *text;       // 插入或删除的具体文本
    int text_len;     // 文本字节数
    time_t timestamp; // 用于合并连续输入的防抖时间戳
} UndoRecord;

#define UNDO_INIT_CAP 64

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
 *                   ├── cells[]  (RenderCell array, tabs → space cells)
 *                   └── editor_update_syntax()
 *                        └── cells[].hl  (per-cell colour)
 *
 * Invariant: after any chars mutation, editor_update_row() must run
 * to rebuild cells[] and re-trigger highlighting.
 */
typedef struct {
    int size;                  // chars 字节数（逻辑长度，\t 算 1 字节）
    char *chars;               // 原始文本（含 \t）
    int cell_count;             // cells 数组长度
    RenderCell *cells;          // 渲染单元数组（\t 展开为空格 cell，byte_len=0）
    int highlight_open_comment; // 上行是否有未闭合多行注释
    size_t syntax_hash;         // chars[] 内容指纹 — 增量语法跳过未变更行
} EditorRow;

// ============================================================
//  子状态 — 按关注点拆分
// ============================================================

// 视图状态 — 光标位置、滚动偏移、屏幕尺寸
typedef struct {
    int cursor_x, cursor_y;
    int row_off, col_off;
    int screen_rows, screen_cols;
    int gutter_width;
} ViewState;

// 文档状态 — 文本数据、文件信息、修改历史
typedef struct {
    char *filename;
    int num_rows;
    EditorRow *row;
    int dirty;

    UndoRecord *undo_stack;
    int undo_count;
    int undo_cap;
    UndoRecord *redo_stack;
    int redo_count;
    int redo_cap;
    int is_action_locked;
} Document;

// 终端底层 — Windows 平台 API 句柄和原始模式备份
typedef struct {
    HANDLE hIN, hOUT;
    int raw_mode;
    DWORD dwOriginalInMode, dwOriginalOutMode;
    UINT uOriginalOutputCP, uOriginalInputCP;
} TerminalState;

// UI 交互 — 状态栏消息、输入模式、搜索词
typedef struct {
    char statusmsg[80];
    time_t statusmsg_time;
    int overwrite_mode;
    char *search_query;
} UIState;

// 编辑器设置 — 语法高亮规则和排版偏好
typedef struct {
    struct EditorSyntax *syntax;
    int tab_stop;
} EditorSettings;

// ============================================================
//  编辑器根状态（组合上述 5 个子结构）
// ============================================================

typedef struct EditorConfig {
    ViewState view;
    Document doc;
    TerminalState term;
    UIState ui;
    EditorSettings settings;
} EditorConfig;

// ============================================================
//  常量 & 工具宏
// ============================================================

#define EDITOR_TAB_STOP 4

enum EditorKeys {
    KEY_NULL = 0,
    KEY_CTRL_C = 3,   KEY_CTRL_D = 4,   KEY_CTRL_F = 6,   KEY_CTRL_G = 7,
    KEY_CTRL_H = 8,   KEY_CTRL_J = 10,  KEY_CTRL_P = 16,  KEY_CTRL_Z = 26,
    KEY_TAB = 9,
    KEY_ENTER = 13,
    KEY_CTRL_Q = 17,  KEY_CTRL_S = 19,  KEY_CTRL_U = 21,  KEY_CTRL_Y = 25,
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


typedef void (*EditorOomHandler)(void);
void editor_set_oom_handler(EditorOomHandler handler);
void *editor_safe_realloc(void *ptr, size_t size);
char *editor_strdup(const char *s);
int utf8_char_length(unsigned char c);
int utf8_char_display_width(unsigned char c);
#endif // TEXTEDITOR_COMMON_H
