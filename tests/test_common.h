#ifndef STARK_TEST_COMMON_H
#define STARK_TEST_COMMON_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stark.h>

// Each test function returns 0 on success, 1 on failure.
#define T_ASSERT(cond, msg)                                                    \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

// Remove the common STARKDB files for base path `p` (with suffix `base`).
static inline void t_remove_all(const char* p) {
    static const char* suffixes[] = {
        ".idx", ".dat", ".txn", ".compact",
        ".idx.new", ".dat.new", ".idx.old", ".dat.old",
        ".idx.tmp", ".dat.tmp"
    };
    char path[1024];
    for (unsigned i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        snprintf(path, sizeof(path), "%s%s", p, suffixes[i]);
        remove(path);
    }
}

static inline long t_file_size(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static inline int t_file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static inline int t_copy_file(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return -1;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    }
    fclose(in);
    fclose(out);
    return 0;
}

static inline int t_stark_exists(stark_db_t* db, uint32_t key) {
    char b[1024];
    size_t s = sizeof(b);
    return stark_get(db, key, b, &s) == STARK_OK;
}

static inline int t_str_exists(stark_db_t* db, const char* key) {
    char b[4096];
    size_t s = sizeof(b);
    return stark_get_str(db, key, b, &s) == STARK_OK;
}

#endif
