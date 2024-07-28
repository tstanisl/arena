#define ARENA_IMPLEMENTATION
#include "arena.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    size_t n;
    char **word;
} split_t;

split_t split_string_inplace(char *str, const char * seps, arena_t * a) {
    size_t n;
    _Bool in_word;

    n = 0;
    in_word = 0;
    for (char * s = str; *s; ++s) {
        if (in_word) {
            if (strchr(seps, *s))
                in_word = 0;
        } else {
            if (!strchr(seps, *s)) {
                ++n;
                in_word = 1;
            }
        }
    }

    split_t res = {
        .n = 0,
        .word = *ARENA_ALLOC(a, char*[n]),
    };

    in_word = 0;
    for (char * s = str; *s; ++s) {
        if (in_word) {
            if (strchr(seps, *s)) {
                *s = 0;
                in_word = 0;
            }
        } else {
            if (!strchr(seps, *s)) {
                res.word[res.n++] = s;  
                in_word = 1;
            }
        }
    }

    return res;
}

void print_words(char * str, arena_t a) {
    split_t split = split_string_inplace(str, " \t\n", &a);
    for (size_t i = 0; i < split.n; ++i)
        puts(split.word[i]);
}

int main() {
    arena_t * a = arena_init(1ull << 30);
    char buf[1024];
    while (fgets(buf, sizeof buf, stdin))
        print_words(buf, *a);

    arena_drop(a);
    return 0;
}
