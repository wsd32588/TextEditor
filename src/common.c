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

void *editor_safe_realloc(void *ptr, size_t size){
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    void *temp = realloc(ptr, size);
    if (temp == NULL) {
        perror("realloc failed: out of memory");
        exit(1);
    }
    return temp;
}

char *editor_strdup(const char *s){
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p == NULL) {
        perror("malloc failed: out of memory");
        exit(EXIT_FAILURE);
    }
    memcpy(p, s, len);
    return p;
}

int utf8_char_length(unsigned char c){
    if (c >= 0x00 && c <= 0x7F) return 1;// 0xxxxxxx: ASCII
    if (c >= 0xC2 && c <= 0xDF) return 2;// 110xxxxx: 2 bytes
    if (c >= 0xE0 && c <= 0xEF) return 3;// 1110xxxx: 3 bytes (大部分中文)
    if (c >= 0xF0 && c <= 0xF4) return 4;// 11110xxx: 4 bytes (Emoji)

    return 1;// 兜底：如果是错乱的中间字节（10xxxxxx），当做1字节处理防止死循环
}

int utf8_char_display_width(unsigned char c){// 传入一个字符的首字节，估算它在终端里的视觉列宽
    if (c >= 0x00 && c <= 0x7F) return 1;

    int len = utf8_char_length(c);
    if (len == 3|| len == 4) return 2;// 中文和 Emoji 算作 2 列宽

    return 1;// 欧洲带音标的双字节字符等，通常还是 1 列宽
}
