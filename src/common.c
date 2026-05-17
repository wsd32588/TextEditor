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

void *editor_safe_realloc(void *ptr, size_t size)
{
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

char *editor_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p == NULL) {
        perror("malloc failed: out of memory");
        exit(EXIT_FAILURE);
    }
    memcpy(p, s, len);
    return p;
}
