/*
 * Safe memory — abort on OOM instead of returning NULL.
 *
 * editor_safe_realloc wraps realloc with fatal exit on failure.
 * Pass ptr = NULL for fresh allocations (realloc(NULL, n) ≡ malloc(n)).
 * editor_strdup clones a C string, fatally exiting on OOM.
 *
 * MSVC does not define strdup, so editor_strdup fills that gap.
 */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static EditorOomHandler g_oom_handler = NULL;

void editor_set_oom_handler(EditorOomHandler handler) {
    g_oom_handler = handler;
}

void *editor_safe_realloc(void *ptr, size_t size){
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    void *temp = realloc(ptr, size);
    if (temp == NULL) {
        if (g_oom_handler) g_oom_handler();
        perror("realloc failed: out of memory");
        exit(1);
    }
    return temp;
}

char *editor_strdup(const char *s){
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p == NULL) {
        if (g_oom_handler) g_oom_handler();
        perror("malloc failed: out of memory");
        exit(EXIT_FAILURE);
    }
    memcpy(p, s, len);
    return p;
}

Utf8Char utf8_decode(const char *s,int s_len){
    Utf8Char result = {0,1,1};
    if (s_len < 1) return result;
    unsigned char c = (unsigned char)s[0];
    result.codepoint = c;
    // 1个字节(ASCII)
    if (c <= 0x7F){
        result.display_width = 1;
        return result;
    }else if (c >= 0xC2 && c <= 0xDF && s_len >= 2){
        if (((unsigned char)s[1] & 0xC0) == 0x80){
            result.codepoint = ((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
            result.byte_len = 2;
            result.display_width = 1;
        }
    }else if (c >= 0xE0 && c <= 0xEF && s_len >= 3){
        if (((unsigned char)s[1] & 0xC0) == 0x80 &&
            ((unsigned char)s[2] & 0xC0) == 0x80){
            result.codepoint = ((c & 0x0F) << 12)
                            | (((unsigned char)s[1] & 0x3F) << 6)
                            | ((unsigned char)s[2] & 0x3F);
            result.byte_len = 3;
            result.display_width = 2;//CJK
        }
    }else if (c >= 0xF0 && c <= 0xF4 && s_len >= 4){
        if (((unsigned char)s[1] & 0xC0) == 0x80 &&
            ((unsigned char)s[2] & 0xC0) == 0x80 &&
            ((unsigned char)s[3] & 0xC0) == 0x80){
            result.codepoint = ((c & 0x07) << 18)
                            | (((unsigned char)s[1] & 0x3F) << 12)
                            | (((unsigned char)s[2] & 0x3F) << 6)
                            | ((unsigned char)s[3] & 0x3F);
            result.byte_len = 4;
            result.display_width = 2;//emoji
            }
    }
    return result;
}

int utf8_encode(uint32_t cp, char *out) {
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    out[0] = '?';
    return 1;
}

Utf8Step utf8_step(const char *s, int s_len){
    Utf8Char uc = utf8_decode(s, s_len);
    return (Utf8Step){ uc.byte_len, uc.display_width };
}

