# Future — 隐患、架构债务、待办清单

> 状态标记：`[ ]` 未修复 / `[x]` 已修复（括号内标注 fix.log 对应版本）
> 按严重程度排序，已修复的沉底。

---

## 🔴 严重 — 数据安全 / 崩溃风险

### [x] 文件保存非原子 — 已修复 (Revision 22 — 2026-05-19)
~~`editor_save` 直接 `fopen(filename, "w")` 覆写。写入中途崩溃 → 原文件已破坏，新内容未写完。~~
**已修**：先写 `filename.tmp`，fclose 后 `rename(tmp, filename)` 原子替换。rename 失败时提示 recovery 文件路径。
_来源：Kimi review_

### [x] OOM 直接退出 — 已修复 (Revision 22 — 2026-05-19)
~~`editor_safe_realloc` / `editor_strdup` 分配失败直接 `exit(1)`，不尝试保存。~~
**已修**：`EditorOomHandler` 回调机制 — OOM 时先调 `editor_emergency_save` → `emergency_backup.txt`，再 exit。handler 在 main.c 注册。
_来源：架构分析_

---

## 🟠 重要 — 数据正确性 / 逻辑缺陷

### [x] Undo/Redo 僵尸代码 — 已完整接入 (Revision 21 — 2026-05-19)
~~`common.h` 定义 `UndoRecord`/`undo_stack`/`redo_stack`，`editor.h` 声明 `editor_push_undo`/`editor_undo`/`editor_redo`，但 `input.c` 键位表中无 Ctrl+Z/Y 绑定。~~
**已修**：`editor_push_undo` 接入 editor_insert_char / editor_delChar / editor_insert_newline；`input.c` 绑定 KEY_CTRL_Z→editor_undo / KEY_CTRL_Y→editor_redo；含 2 秒防抖合并 + redo 链断开 + 栈满逐出。
_来源：Kimi review + 自检_

### [ ] `editor_insert_raw` 的 `realloc` 可导致悬空指针
`realloc(ec->doc.row, ...)` 可能移动整块内存。若调用方持有 `EditorRow *`（如 `editor_update_syntax` 的 `start_row`），`realloc` 后全部失效。
**修法**：`editor_update_syntax` 改用行索引而非裸指针；或在 `realloc` 调用点之后显式更新所有缓存指针。
_来源：Kimi review_

### [ ] `editor_delChar` 行合并后未触发 syntax 链式更新
删除行首字符（合并行）后，被合并行的高亮状态可能未传播到后续行。
_来源：Kimi review_

### [ ] `editor_insert_newline` 新行 `highlight_open_comment` 初始化为 0
换行后新行不继承上一行的多行注释状态，可能导致注释高亮闪烁一帧。
_来源：Kimi review_

### [ ] cursor_x 是 byte offset — 可能卡进多字节字符内部
`cursor_x` 本质是字节偏移，不是 codepoint index 或 grapheme index。当文本含 emoji、组合字符、变体选择器等复杂 Unicode 时，光标可能落在字符内部（splitting a multi-byte sequence）。
**修法**（远期）：引入 `cursor_bx`(byte) / `cursor_cx`(codepoint) / `cursor_rx`(render) 三套坐标，或 grapheme iterator。
_来源：ChatGPT review_

### [ ] 搜索 `strstr` 按字节匹配 — UTF-8 边界误匹配（实际概率极低）
搜索 "中" (E4 B8 AD) 时若文本包含某字符的中间字节恰好是 B8 或 AD，可能匹配到错误位置。
**修法**：在 `cells[]` 层面按 `codepoint` 匹配，不走原始字节。
_来源：Kimi review_

---

## 🟡 中等 — 性能 / 用户体验退化（规模上去后才触发）

### [ ] 每次按键全屏重绘
`main()` 循环中每帧 `editor_refresh_screen` 全量重建 `AppendBuffer`。对 1000+ 行文件浪费明显。
**修法**（远期）：引入脏行标记，仅重绘变更行。
_来源：Kimi review + 自检_

### [ ] `editor_update_row` 连续输入时重复调用
快速输入 "hello" 触发 5 次 `editor_update_row`，每次重建完整 `cells[]`。
**修法**（远期）：防抖批量更新。
_来源：Kimi review_

### [ ] 大文件 ~30000 次分配
`editor_open` 逐字符 `fgetc` + 每行独立 `malloc(chars + cells)`。10000 行 ≈ 30000 次堆分配。
**修法**（远期）：统一大缓冲区 + 行偏移索引 (`FlatDocument`)。
_来源：Kimi review_

### [ ] 语法高亮每个 cell 遍历全部关键字 — O(n×k) 倾向
`editor_update_syntax` 中对每个 `cell` 遍历 `syntax->keywords[]` 做字符串匹配。语言增多（Rust/TS/Java/Go/C++）后关键字表膨胀，逐 cell 遍历会明显变重。
**修法**（远期）：trie / hash keyword table / token scanner。
_来源：ChatGPT review_

### [ ] UTF-8 display_width 过于简化 — 仅 "4-byte → width 2"
当前 `display_width` 判断逻辑：4 字节 UTF-8 → 宽度 2，其他 → 1。但 Unicode 实际有 combining char、variation selector、emoji ZWJ sequence、skin tone modifier、flags (regional indicator pairs) 等大量例外，远不是字节数能决定的。
**修法**（远期）：引入 `wcwidth`/`wcswidth` 或绑 `icu`。
_来源：ChatGPT review_

---

## 🟢 低优先级 — 可维护性 / 扩展性

### [ ] 语法高亮硬编码
`g_high_light_data_base[]` 编译期常量，新增语言需改 `syntax.c` 重编译。
**修法**（远期）：JSON/INI 外部语法定义文件。
_来源：CLAUDE.md 已知问题 + Kimi review_

### [ ] 字符串字面量语法高亮未实现
_来源：CLAUDE.md 已知问题_

### [ ] 数字字面量语法高亮未实现
_来源：CLAUDE.md 已知问题_

### [ ] 鼠标支持未实现
_来源：CLAUDE.md 已知问题_

### [ ] 无配置持久化
`tab_stop`、`overwrite_mode` 等设置无法跨会话保存。
_来源：Kimi review_

### [ ] 零自动化测试
手动回归 UTF-8 / 语法高亮 / 搜索 / 覆盖模式成本极高。
_来源：Kimi review_

### [ ] `editor_prompt` 阻塞主循环 — 弹窗时 resize 花屏
Ctrl+P / Ctrl+F 弹窗期间 `terminal_get_window_size` 不会被调用，拖动窗口边界后渲染错乱。
_来源：Kimi review_

### [ ] 输入层键位表是平面数组 — 组合键爆炸时不够用
`g_key_bindings[]` 是 `key → function` 的一维映射。未来加入 Ctrl+K Ctrl+D、Vim leader key、macro、command mode 后，单键到函数的映射不够表达多键序列和模式上下文。
**修法**（远期）：引入 key chord 树 / 状态机，或按 EditorMode 分层。
_来源：ChatGPT review_

### [ ] 模式状态散落 — 缺少 EditorMode 枚举
当前 `overwrite_mode` 是一个单独的 int flag。未来出现 insert / normal / visual / command 等多模式后，散落的 flag 难以管理。
**修法**（远期）：`typedef enum { MODE_INSERT, MODE_NORMAL, MODE_VISUAL, MODE_COMMAND } EditorMode;`
_来源：ChatGPT review_

### [ ] 函数签名仍传递整个 `EditorConfig *`
拆分 5 子结构体后，67 个函数仍接收根对象。下一步应缩小为仅传需要的子结构体（如 `terminal.c` 只需 `TerminalState *`）。
_来源：自检_

---

## ✅ 已修复

### [x] 中文乱码 — 控制台 GBK→UTF-8 代码页 (Revision 19 — 2026-05-19)
`SetConsoleOutputCP(CP_UTF8)` + `SetConsoleCP(CP_UTF8)`，保存/恢复原始代码页。

### [x] `editor_free_all_row` 漏释放 `search_query` (Revision 19 — 2026-05-19)

### [x] EditorConfig god struct 平面字段拆分 (Revision 20 — 2026-05-19)
拆为 ViewState / Document / TerminalState / UIState / EditorSettings 5 子结构体。

---

_最后更新：2026-05-19 | 来源：Kimi review + ChatGPT review + 自检 + CLAUDE.md_
