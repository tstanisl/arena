/*
MIT License

Copyright (c) 2024 Tomasz Stanisławski

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct arena {
    char * data;
    size_t left;
} arena_t;

typedef void arena_failure_f(arena_t *, size_t size, size_t align, void * ctx);

arena_t * arena_init(size_t size);
arena_t * arena_init_ext(size_t size, arena_failure_f cb, void * cb_ctx);
void arena_drop(arena_t * a);
static void * arena_alloc(arena_t * a, size_t size, size_t align);

#define ARENA_ALLOC(a,T) ((__typeof__(T)*)arena_alloc(a, sizeof(T), _Alignof(T)))
 
#if defined(__has_feature)
#   if __has_feature(address_sanitizer) // for clang
#       define __SANITIZE_ADDRESS__ // GCC already sets this
#   endif
#endif

#ifdef __SANITIZE_ADDRESS__
#  include <sanitizer/asan_interface.h>
#else
#  define ASAN_POISON_MEMORY_REGION(...) (void)0
#  define ASAN_UNPOISON_MEMORY_REGION(...) (void)0
#endif

static inline
#ifdef __GNUC__
__attribute__((malloc))
#endif
void * arena_alloc(arena_t * a, size_t size, size_t align)
{
    size_t skip = -(uintptr_t)a->data & (align - 1);
    size_t esize = size + skip;
    if (esize <= a->left) { // hot path
        char * addr = a->data + skip;
        a->left -= esize;
        a->data += esize;

        size_t to_poison = size + 256 < a->left ? size + 256 : a->left;
        (void)to_poison; // silence compiler for non-sanitized builds
        ASAN_POISON_MEMORY_REGION(a->data, to_poison);
        ASAN_UNPOISON_MEMORY_REGION(addr, size);

        return addr;
    }

    void * arena_failure(arena_t*,size_t,size_t);
    return arena_failure(a, size, align);
}

#endif // ARENA_H

/***********************************
 *   I M P L E M E N T A T I O N   *
 ***********************************/

#ifdef ARENA_IMPLEMENTATION

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/mman.h>
#include <unistd.h>

typedef struct {
    char * addr;
    size_t size;
    arena_failure_f * cb;
    void * cb_ctx;
    arena_t base;
} arena_meta_t;

static inline arena_meta_t * to_meta(arena_t *a) {
    return (void*)(a->data + a->left);
}

arena_t * arena_init_ext(size_t size, arena_failure_f cb, void * cb_ctx) {
    size_t pg_size = sysconf(_SC_PAGE_SIZE);
    size_t am_size = sizeof(arena_meta_t);
    size_t mm_size = (size + am_size + pg_size - 1) & -pg_size;

    char * addr = mmap(NULL, mm_size, PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (addr == MAP_FAILED)
        return 0;

    arena_meta_t * am = (void*)(addr + mm_size - am_size);
    char * data = (char*)am - size;

    *am = (arena_meta_t) {
        .addr = addr,
        .size = size,
        .cb = cb,
        .cb_ctx = cb_ctx,
        .base = { data, size },
    };

    ASAN_POISON_MEMORY_REGION(addr, (char*)am - addr);

    assert(to_meta(&am->base) == am);

    return &am->base;
}

static void arena__default_failure(arena_t *a, size_t size, size_t align, void* ctx) {
    (void)ctx;
    arena_meta_t * am = to_meta(a);

    fprintf(stderr, "Critical: allocation (size=%zu align=%zu) failed from arena of size %zu\n", size, align, am->size);
    abort();
}


arena_t * arena_init(size_t size) {
    return arena_init_ext(size, arena__default_failure, 0);
}

void arena_drop(arena_t * a) {
    arena_meta_t * am = to_meta(a);
    assert(&am->base == a && "arena_drop() for arena not from arena_init()");
    size_t mm_size = (char*)(am + 1) - (char*)am->addr;
    munmap(am->addr, mm_size);
}

void * arena_failure(arena_t * a, size_t size, size_t align) {
    arena_meta_t * am = to_meta(a);
    am->cb(a, size, align, am->cb_ctx);
    return 0;
}

#endif // ARENA_IMPLEMENTATION
