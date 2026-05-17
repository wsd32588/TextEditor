# TextEditor — Project Overview

MinGW/Win32 terminal text editor, **zero external dependencies** (only `kernel32`). C99, CMake + Ninja build.

## Architecture

```
main → editor_init / editor_refresh_screen / editor_process_key
        ├── terminal — raw mode, key read, escape seq parsing, window size
        ├── render   — AppendBuffer, scroll, line/status/message drawing
        ├── input    — key dispatch table (LUT → handler function)
        ├── editor   — row ops, insert/delete/split, search prompt
        ├── fileio   — fopen/fgetc/fwrite file I/O
        └── syntax   — keyword + comment + search-match highlighting
```

All functions receive `EditorConfig *ec` as first argument. Zero globals in editor logic (syntax database is the only global data).

## Data Flow

### Text pipeline
```
chars[] (raw, \t = 1 byte)
    │
    ▼ editor_update_row()
    ├── render[] (tabs → spaces, display-ready)
    └── editor_update_syntax()
         └── high_light[] (per-byte colour tag, sized to render[])
```

**Invariant**: every `chars[]` mutation must call `editor_update_row()` to rebuild `render[]` and re-trigger highlighting.

### Frame pipeline (every keystroke)
```
editor_refresh_screen()
    ├── editor_scroll()        — clamp row_off/col_off to cursor + margins
    ├── editor_draw_rows()     — for each visible row:
    │                            read render[col_off + j] + high_light[j]
    │                            → ANSI-coloured text to AppendBuffer
    ├── editor_draw_status_bar()
    ├── editor_draw_message_bar()
    └── WriteFile(hOUT, buf)   — one-shot flush

editor_process_key()
    ├── terminal_read_key()    — ReadFile + \x1b sequence parser
    ├── g_key_bindings[] LUT   — dispatch to handler
    └── printable fallback     — insert as literal char
```

## Coordinate System

| Domain | Unit | Stored In | Usage |
|--------|------|-----------|-------|
| chars-space | logical byte index | `cursor_x`, `cursor_y` | editing, strstr search |
| render-space | visual column | `col_off`, `rx` (computed) | horizontal scroll, cursor position |
| screen-space | terminal cell | `row_off` | vertical scroll |

Conversion: `editor_row_cx_to_rx(ec, row, cursor_x) → rx` walks `row->chars[0..cx]` and counts columns with `tab_stop` expansion.

`col_off` is stored in render-space: the display loop indexes `render[col_off + j]` directly. Cursor screen X = `(rx - col_off) + gutter_width + 2`.

## Key Data Structures

### EditorConfig (root state)
- `cursor_x/y` — chars-space cursor
- `row_off / col_off` — scroll offsets (row = row index, col = render-space byte offset)
- `row[]` — dynamic array of EditorRow, `num_rows` entries
- `screen_rows/cols` — terminal dimensions
- `syntax` — active language rules (NULL = plain text)
- `tab_stop` — active indent width, from matched syntax or EDITOR_TAB_STOP (4)
- `search_query` — active search term (NULL = no highlight)
- `hIN/hOUT` — Win32 console handles
- `dwOriginalInMode/OutMode` — saved for raw-mode restore

### EditorRow (one text line)
- `size` — bytes in `chars` (logical, tabs count as 1)
- `render_size` — bytes in `render` (tabs expanded to spaces)
- `chars` — raw input text
- `render` — display-ready copy (\t → spaces)
- `high_light` — colour tags, `render_size` bytes, 1:1 with `render`

### AppendBuffer (output batch)
```c
typedef struct { char *b; int len; } AppendBuffer;
#define ABUF_INIT { NULL, 0 }
```
Accumulates ANSI text across draw calls, flushed once per frame via `WriteFile`.

## Key Functions

### editor.c
| Function | Role |
|----------|------|
| `editor_init` | Enter alternate screen (`\x1b[?1049h`), query terminal size |
| `editor_row_cx_to_rx` | chars-space column → render-space column (with tab expansion) |
| `editor_update_row` | Generate `render[]` from `chars[]`, then calls `editor_update_syntax` |
| `editor_insert_raw` | Insert a new row at index `at` (shift rows down) |
| `editor_row_insert_char` | Insert one char into an existing row at position `at` |
| `editor_insert_char` | Insert char at cursor (auto-creates row if past EOF) |
| `editor_delChar` | Backspace: erase char OR merge current row into previous |
| `editor_deleteCharAtCursor` | Delete: erase char at cursor OR merge next row into current |
| `editor_insert_newline` | Enter: split row at cursor OR insert blank line |
| `editor_set_status_message` | Set ephemeral status text + timestamp |
| `editor_prompt` | Mini readline loop for search/save-as prompts |
| `editor_find` | Search: store query, broadcast highlighting, ring-walk to first match |

### render.c
| Function | Role |
|----------|------|
| `abuf_append` / `abuf_free` | AppendBuffer helpers |
| `editor_syntax_to_color` | HighlightType enum → ANSI SGR code |
| `editor_scroll` | Clamp row_off/col_off so cursor stays in viewport |
| `editor_draw_rows` | Draw line numbers + colourised text for each visible row |
| `editor_draw_status_bar` | File name / row count / modified flag / cursor position |
| `editor_draw_message_bar` | Ephemeral message, auto-clears after 5 s |
| `editor_refresh_screen` | Frame entry: compute gutter → scroll → draw → cursor → flush |

### input.c
| Function | Role |
|----------|------|
| `editor_process_key` | Read one key → dispatch via g_key_bindings[] → printable fallback |

### terminal.c
| Function | Role |
|----------|------|
| `enable_raw_mode` | Disable echo, line input; enable VT sequences |
| `disable_raw_mode` | Restore original console modes |
| `terminal_get_window_size` | Query console buffer for rows/cols |
| `terminal_read_key` | Blocking ReadFile + \x1b sequence parser (100 ms timeout) |

### syntax.c
| Function | Role |
|----------|------|
| `editor_update_syntax` | Annotate `high_light[]` with keyword/comment/search colours |
| `editor_select_syntax_highlight` | Match filename suffix → set `ec->syntax + ec->tab_stop` |

### fileio.c
| Function | Role |
|----------|------|
| `editor_save` | fwrite each row + \n; prompts Save As if filename is NULL |
| `editor_open` | fgetc line-by-line → editor_insert_raw |

## Syntax Highlighting DB

5 languages, defined in `g_high_light_data_base[]`:

| Language | Suffixes | tab_stop | Comment |
|----------|----------|----------|---------|
| C/C++ | .c .h .cpp .hpp .cc | 4 | // |
| Python | .py | 4 | # |
| Rust | .rs | 4 | // |
| Go | .go | 8 | // |
| JS/TS | .js .ts | 2 | // |

Keyword table convention: trailing `|` = HL_KEYWORD2 (type keywords), no `|` = HL_KEYWORD1 (control flow).

## Build

```bash
cmake -G Ninja -B build .
ninja -C build
./build/TextEditor.exe [filename]
```

## Key Bindings

| Key | Action |
|-----|--------|
| Arrows | Move cursor |
| Enter | Newline / split row |
| Backspace | Delete char before cursor / merge with prev row |
| Delete | Delete char at cursor / merge with next row |
| Tab | Insert \t |
| Ctrl+S | Save (prompts Save As if untitled) |
| Ctrl+F | Search (ring search, ESC to cancel) |
| Ctrl+Q | Quit |
| Printable (32–126) | Insert character |

## Known Gaps

- Multiline comment highlighting (`/* */`) not implemented
- String literal highlighting not implemented
- UTF-8 / Unicode not supported (byte-oriented throughout)
- Mouse events not supported
- `seq[8]` can still overflow on exotic escape sequences
- No undo/redo
- No line-jump command
