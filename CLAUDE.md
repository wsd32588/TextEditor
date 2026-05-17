# TextEditor — Windows 终端文本编辑器

## 项目概况

Windows 终端文本编辑器，C99 编写，CMake + Ninja 构建，零第三方依赖（仅 kernel32）。

- 入口：`src/Main/main.c`
- 二进制：`cmake-build-debug/TextEditor.exe`
- 架构源于 kilo 编辑器，移植到 Windows 控制台 API

## 模块架构

```
main → editor_init / editor_refresh_screen / editor_process_key
        ├── terminal  — 原始模式、按键读取、窗口大小
        ├── render    — AppendBuffer 屏幕绘制、语法高亮着色
        ├── input     — 函数指针按键分发表
        ├── editor    — 核心编辑操作（插入/删除/换行/合并行）
        ├── fileio    — 文件打开/保存
        └── syntax    — C 语法高亮引擎（关键字/注释/数字）
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
- 安全内存分配（`editor_safe_realloc`/`editor_safe_strdup`）

## 关键数据流

编辑器定义一个 EditorConfig (config)，它将 EditorRow 的动态数组（row/rows/num_rows）保存在 Editor 结构体上。render 层将 Editor 中的文本转换为 AppendBuffer，加上 ANSI 转义码（用于光标定位和语法高亮），然后一次写入 stdout。

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
| `src/Main/main.c` | ~47 | 入口，VT 初始化，主循环 |
| `src/terminal.c` | ~122 | 原始模式，按键读取，转义序列解析 |
| `src/render.c` | ~189 | AppendBuffer，滚动，屏幕绘制 |
| `src/editor.c` | ~233 | 核心编辑（插入/删除/分割行） |
| `src/input.c` | ~94 | 函数指针分发表 |
| `src/fileio.c` | ~73 | 文件打开/保存 |
| `src/syntax.c` | ~121 | C 语法高亮引擎 |
| `src/common.c` | ~33 | safe_realloc/safe_strdup 辅助函数 |

约 1100 行 C 代码。

## 编译

```bash
# 项目根目录
mkdir -p build && cd build
cmake .. -G "Ninja"
ninja
```

CMakeLists.txt 要求 C99 + `-Wall -Wextra -Wpedantic`。链接 `kernel32`。

## 已知问题 / 待办项

- 搜索功能（Ctrl+F）未实现
- 行跳转功能未实现
- 多行注释（`/* */`）语法高亮未实现
- 字符串字面量语法高亮未实现
- UTF-8 / 中文支持未实现
- 转义序列缓冲区 `seq[4]` 偏小，无法容纳 Ctrl+Arrow 等 6 字节序列
- 鼠标支持未实现

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
