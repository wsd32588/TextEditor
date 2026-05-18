# TextEditor — Windows 终端文本编辑器

## 项目概况

Windows 终端文本编辑器，C99 编写，CMake + Ninja 构建，零第三方依赖（仅 kernel32）。

- 入口：`src/Main/main.c`
- 二进制：`cmake-build-debug/TextEditor.exe`
- 架构源于 kilo 编辑器，移植到 Windows 控制台 API

## 模块架构

```
main → editor_init / editor_refresh_screen / editor_process_key
        ├── terminal  — 原始模式、UTF-8 代码页、按键读取、窗口大小
        ├── common    — UTF-8 编解码（utf8_decode/encode/step）、安全内存分配
        ├── render    — AppendBuffer 屏幕绘制、RenderCell 颜色输出
        ├── input     — 函数指针按键分发表、光标移动
        ├── editor    — 核心编辑操作（插入/删除/换行/合并行）、坐标转换
        ├── fileio    — 文件打开/保存
        └── syntax    — 5 语言语法高亮（cell 级关键字/注释/搜索匹配）
```

所有函数接收 `EditorConfig *ec` 作为第一个参数，无全局变量。

## 已实现功能

- 备用屏幕缓冲区（`\x1b[?1049h/l`）
- 原始输入模式 + VT 处理
- 按键读取（方向键、Home/End/Del/PgUp/PgDn）
- 函数指针按键分发表（`input.c` 中 `KeyBinding` 数组）
- 光标移动（边界检查）
- 文本插入、Enter 换行、退格、Delete、Tab
- 文件打开（argv）+ 保存（默认 untitled.txt）
- C 语法高亮（关键字/注释/数字着色）
- 视口滚动（垂直 + 水平偏移）
- 状态栏（文件名/行数/修改标记）+ 消息栏（5 秒超时）
- 安全内存分配（`editor_safe_realloc`/`editor_strdup`）
- 覆盖模式 — INS 切换，UTF-8 安全逐字节覆写
- 快速打开 — Ctrl+P 搜索文件名直接打开
- 查找/查找下一个 — Ctrl+F/Ctrl+G，环形搜索 + 匹配高亮
- 行号跳转 — Ctrl+J 输入行号直接跳转
- 5 语言语法高亮 — C/C++/Python/Rust/Go/JS，多行注释状态机（迭代传播）
- UTF-8 全链路 — Utf8Char/Utf8Step/RenderCell 三层抽象，应用层零裸位运算
- 控制台 UTF-8 代码页自动切换（SetConsoleOutputCP）

## 关键数据流

```
输入字节 → chars[] → editor_update_row()
                       ├── Pass 1: utf8_step 数 cell
                       ├── Pass 2: utf8_decode → RenderCell {codepoint, byte_len, display_width, hl}
                       └── editor_update_syntax() → cells[].hl（cell 级着色）

显示帧:   cells[] → editor_draw_rows()
                     ├── cell_start 按 display_width 跳过 col_off
                     ├── cell_n    按 display_width 截断到 available_cols
                     └── utf8_encode(cell.codepoint) → UTF-8 字节 → AppendBuffer → WriteFile
```

坐标系统：
- `cursor_x/y` — chars-space（字节索引，\t 算 1 字节）
- `rx` — render-space（视觉列，\t 展开，中文占 2 列）
- 转换：`editor_row_cx_to_rx` / `editor_row_rx_to_cx`（均走 `utf8_step`）

## 文件清单

### 头文件
| 文件 | 职责 |
|------|------|
| `include/common.h` | EditorConfig/EditorRow/EditorKey 类型定义 |
| `include/editor.h` | 编辑操作函数声明 |
| `include/fileio.h` | 文件 I/O 声明 |
| `include/input.h` | 按键处理声明 |
| `include/render.h` | AppendBuffer + 渲染声明 |
| `include/syntax.h` | 语法高亮声明 |
| `include/terminal.h` | 终端控制声明 |

### 源文件
| 文件 | 行数 | 职责 |
|------|------|------|
| `src/Main/main.c` | ~32 | 入口，主循环 |
| `src/terminal.c` | ~170 | 原始模式、UTF-8 代码页、按键读取、转义序列解析 |
| `src/render.c` | ~280 | AppendBuffer，RenderCell 输出，滚动，屏幕绘制 |
| `src/editor.c` | ~570 | 核心编辑 + RenderCell 构建 + 坐标转换 + 搜索 |
| `src/input.c` | ~175 | 函数指针分发表 + 光标移动 + 快速打开 + 行跳转 |
| `src/fileio.c` | ~91 | 文件打开/保存 |
| `src/syntax.c` | ~300 | 5 语言 cell 级语法高亮引擎 |
| `src/common.c` | ~105 | UTF-8 编解码（utf8_decode/encode/step）+ 安全内存分配 |

约 1900 行 C 代码。

## 编译

```bash
# 项目根目录
mkdir -p build && cd build
cmake .. -G "Ninja"
ninja
```

CMakeLists.txt 要求 C99 + `-Wall -Wextra -Wpedantic`。链接 `kernel32`。

## 已知问题 / 待办项

- 字符串字面量语法高亮未实现
- 数字字面量语法高亮未实现
- 鼠标支持未实现
- 撤销/重做未实现

## Git 仓库

- 远程：`git@github.com:wsd32588/TextEditor.git`（SSH）
- `.gitignore` 排除：`build/`、`.claude/`、`.idea/`、`*.exe`、个人笔记
- VERSION_0.1 快照已打 zip 备份：`../TextEditor_VERSION_0.1.zip`

## 近期实现（2026-05-19 为止）

- **RenderCell 架构** — EditorRow 从字节缓冲 (render[]/high_light[]) 迁移至 RenderCell 数组 (cells[])，display loop 按 cell 迭代 + utf8_encode 输出
- **UTF-8 工具层** — Utf8Char/Utf8Step/utf8_decode/utf8_encode/utf8_step 统一入口，应用层 `(c & 0xC0)` 裸位运算从 15 处减至 2 处
- **语法高亮 cell 级重写** — cell_is_sep/cell_match_seq 替代 is_separator/strncmp，支持非 ASCII 标识符
- **多行注释迭代传播** — 递归 → while 循环，消除大文件栈溢出风险
- **控制台 UTF-8 代码页** — enable_raw_mode 自动 SetConsoleOutputCP(CP_UTF8)，修复中文在 GBK 终端乱码
- **行号跳转** — Ctrl+J 输入行号直接跳转
- **Ctrl+F 优先搜索当前行** — 先从光标处向后找，再跨行环形搜索
- **覆盖模式续字节保护** — 仅 leading byte 触发旧字符删除，保障 UTF-8 序列完整

## 变更日志要求

修改代码时必须同时在 `fix.log` 中追加记录。格式遵循现有惯例：

```
=========================================================
Revision N — YYYY-MM-DD
=========================================================

Summary
---------------------------------------------------------
一句话总结本次修改。
---------------------------------------------------------
Detail
---------------------------------------------------------

[1] 文件名 — 修改内容简述
    Before : 旧代码（关键部分）
    After  : 新代码（关键部分）
    Why    : 为什么这么改。说明 bug 的根因、触发条件、修复原理。

Affected Files
---------------------------------------------------------
  文件名             —  变更统计
```

## 风格指南

项目遵循 `Unified_Code_Style_Guide.md` 中的 C 规范：
- 类型 PascalCase，函数/变量 snake_case
- 全局变量 `g_` 前缀
- `#include` 顺序：本模块 → 标准库 → 项目内其他模块
- 错误处理返回 -1 + perror/fprintf(stderr)
- goto cleanup 统一资源释放出口
