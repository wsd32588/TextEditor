/*
 * Entry point and terminal initialisation (VT processing + raw mode).
 *
 * Enables ENABLE_VIRTUAL_TERMINAL_PROCESSING on the output handle so
 * ANSI escape sequences (\x1b[...) are interpreted by the Windows
 * console.  Without this the editor would print raw escape codes.
 */
#include "common.h"
#include "input.h"
#include "terminal.h"
#include "render.h"
#include "editor.h"
#include "fileio.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    EditorConfig ec = {0};

    enable_raw_mode(&ec);
    editor_init(&ec);

    editor_set_status_message(&ec, "HELP: Ctrl-S = save | Ctrl-Q = quit");
    if (argc >= 2)
        editor_open(&ec, argv[1]);

    while (1) {
        editor_refresh_screen(&ec);
        editor_process_key(&ec);
    }
    return 0;
}
