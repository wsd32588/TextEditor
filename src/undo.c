/*
 * Undo/redo engine — stack-based with debounce merging.
 *
 * editor_push_undo records every mutation (insert/delete chars/lines).
 * Consecutive inserts or deletes within 2 s are merged into one record.
 * execute_op applies or reverses a single record; is_action_locked
 * prevents re-entrant recording during undo/redo execution.
 */
#include "undo.h"
#include "editor.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

static void ensure_stack_cap(UndoRecord **stack, int *cap, int needed) {
    if (*cap >= needed) return;
    int new_cap = *cap == 0 ? UNDO_INIT_CAP : *cap;
    while (new_cap < needed) new_cap *= 2;
    *stack = editor_safe_realloc(*stack, new_cap * sizeof(UndoRecord));
    *cap = new_cap;
}

void editor_push_undo(EditorConfig *ec, OpType type, int y, int x,
                      const char *text, int len) {
    if (ec->doc.is_action_locked) return;

    for (int i = 0; i < ec->doc.redo_count; i++)
        free(ec->doc.redo_stack[i].text);
    ec->doc.redo_count = 0;
    time_t now = time(NULL);

    /* merge consecutive inserts */
    if (type == OP_INSERT_CHARS && ec->doc.undo_count > 0) {
        UndoRecord *last = &ec->doc.undo_stack[ec->doc.undo_count - 1];
        if (last->type == OP_INSERT_CHARS
            && last->cursor_y == y
            && last->cursor_x + last->text_len == x
            && (now - last->timestamp < 2)) {
            last->text = editor_safe_realloc(last->text,
                                             last->text_len + len + 1);
            memcpy(last->text + last->text_len, text, len);
            last->text[last->text_len] = '\0';
            last->timestamp = now;
            return;
        }
    }

    /* merge consecutive deletes (prepend) */
    if (type == OP_DELETE_CHARS && ec->doc.undo_count > 0) {
        UndoRecord *last = &ec->doc.undo_stack[ec->doc.undo_count - 1];
        if (last->type == OP_DELETE_CHARS
            && last->cursor_y == y
            && last->cursor_x == x + len
            && (now - last->timestamp) < 2) {
            char *old_text = last->text;
            int old_len = last->text_len;
            char *new_text = editor_safe_realloc(NULL, old_len + len + 1);
            memcpy(new_text, text, len);
            memcpy(new_text + len, old_text, old_len);
            new_text[old_len + len] = '\0';
            free(old_text);
            last->text = new_text;
            last->cursor_x = x;
            last->text_len += len;
            last->timestamp = now;
            return;
        }
    }

    ensure_stack_cap(&ec->doc.undo_stack, &ec->doc.undo_cap,
                     ec->doc.undo_count + 1);

    UndoRecord *rec = &ec->doc.undo_stack[ec->doc.undo_count++];
    rec->type = type;
    rec->cursor_x = x;
    rec->cursor_y = y;
    rec->text = editor_safe_realloc(NULL, len + 1);
    if (len > 0) memcpy(rec->text, text, len);
    rec->text[len] = '\0';
    rec->text_len = len;
    rec->timestamp = now;
}

static void execute_op(EditorConfig *ec, UndoRecord *rec, int is_undo) {
    ec->doc.is_action_locked = 1;

    OpType effective_type = rec->type;
    if (is_undo) {
        if (effective_type == OP_INSERT_CHARS) effective_type = OP_DELETE_CHARS;
        else if (effective_type == OP_DELETE_CHARS) effective_type = OP_INSERT_CHARS;
        else if (effective_type == OP_INSERT_LINE) effective_type = OP_DELETE_LINE;
        else if (effective_type == OP_DELETE_LINE) effective_type = OP_INSERT_LINE;
    }

    switch (effective_type) {
    case OP_INSERT_CHARS:
        ec->view.cursor_y = rec->cursor_y;
        ec->view.cursor_x = rec->cursor_x;
        for (int i = 0; i < rec->text_len; i++)
            editor_row_insert_char(ec, &ec->doc.row[ec->view.cursor_y],
                                  ec->view.cursor_x++, rec->text[i]);
        break;
    case OP_DELETE_CHARS:
        ec->view.cursor_y = rec->cursor_y;
        for (int i = 0; i < rec->text_len; i++)
            editor_row_del_char(ec, &ec->doc.row[ec->view.cursor_y],
                               rec->cursor_x);
        ec->view.cursor_x = rec->cursor_x;
        break;
    case OP_INSERT_LINE:
        ec->view.cursor_y = rec->cursor_y;
        ec->view.cursor_x = rec->cursor_x;
        editor_insert_newline(ec);
        break;
    case OP_DELETE_LINE:
        ec->view.cursor_y = rec->cursor_y + 1;
        ec->view.cursor_x = 0;
        editor_delChar(ec);
        ec->view.cursor_y = rec->cursor_y;
        ec->view.cursor_x = rec->cursor_x;
        break;
    }

    ec->doc.is_action_locked = 0;
}

void editor_undo(EditorConfig *ec) {
    if (ec->doc.undo_count == 0) {
        editor_set_status_message(ec, "Nothing to undo (undo stack empty)");
        return;
    }
    UndoRecord rec = ec->doc.undo_stack[--ec->doc.undo_count];
    execute_op(ec, &rec, 1);
    ensure_stack_cap(&ec->doc.redo_stack, &ec->doc.redo_cap,
                     ec->doc.redo_count + 1);
    ec->doc.redo_stack[ec->doc.redo_count++] = rec;
    editor_set_status_message(ec, "Undo! (%d ops remaining)",
                              ec->doc.undo_count);
}

void editor_redo(EditorConfig *ec) {
    if (ec->doc.redo_count == 0) {
        editor_set_status_message(ec, "Nothing to redo (redo stack empty)");
        return;
    }
    UndoRecord rec = ec->doc.redo_stack[--ec->doc.redo_count];
    execute_op(ec, &rec, 0);
    ensure_stack_cap(&ec->doc.undo_stack, &ec->doc.undo_cap,
                     ec->doc.undo_count + 1);
    ec->doc.undo_stack[ec->doc.undo_count++] = rec;
    editor_set_status_message(ec, "Redo! (%d ops remaining)",
                              ec->doc.redo_count);
}
