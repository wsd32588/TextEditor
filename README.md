# TextEditor

Windows 终端文本编辑器，基于 C99 + Win32 Console API + ANSI VT 转义序列。

## 项目结构

```
TextEditor/
├── CMakeLists.txt              # CMake 构建配置
├── README.md
│
├── include/                    # 头文件
│   ├── common.h                # 全局类型定义（EditorConfig、EditorRow、枚举、工具函数）
│   ├── editor.h                # 编辑器核心操作
│   ├── fileio.h                # 文件读写
│   ├── input.h                 # 按键分发
│   ├── render.h                # 渲染（AppendBuffer、editor_refresh_screen）
│   ├── syntax.h                # 语法高亮
│   └── terminal.h              # 终端原始模式、按键读取
│
└── src/                        # 源文件
    ├── Main/
    │   └── main.c              # 入口：VT 初始化、主循环
    ├── common.c                # editor_safe_realloc、editor_strdup
    ├── editor.c                # 核心编辑逻辑（插入/删除/换行）
    ├── fileio.c                # 文件打开与保存
    ├── input.c                 # 按键绑定表 + 分发
    ├── render.c                # 屏幕刷新、滚动、状态栏、消息栏
    ├── syntax.c                # 语法高亮引擎
    └── terminal.c              # 原始模式开关、转义序列解析
```

## 构建

```bash
# CMake + MinGW
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

或在 CLion 中直接打开项目根目录。

## 快捷键

| 按键 | 功能 |
|------|------|
| Ctrl-Q | 退出 |
| Ctrl-S | 保存 |
| 方向键 | 移动光标 |
| Enter | 换行 |
| Backspace | 删除光标前字符 |
| Delete | 删除光标处字符 |
| Tab | 插入制表符 |

## 功能

- 文件打开/保存（纯文本）
- 语法高亮（C 语言：关键字、数字、注释）
- 滚动视口跟随光标
- 状态栏显示文件名、行数、修改标记
- 5 秒自动消失的消息栏
- 备用屏幕缓冲区（类似 Vim）
