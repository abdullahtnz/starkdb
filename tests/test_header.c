#include "test_common.h"
#include <stdint.h>

static void write_raw(const char* path, const unsigned char* header, long body) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    if (header) fwrite(header, 1, 16, f);
    if (body > 0) {
        unsigned char* z = calloc(1, (size_t)body);
        if (z) { fwrite(z, 1, (size_t)body, f); free(z); }
    }
    fclose(f);
}

static long strip_header_to_legacy(const char* path) {
    long sz = t_file_size(path);
    if (sz < 16) return -1;
    FILE* in = fopen(path, "rb");
    if (!in) return -1;
    fseek(in, 16, SEEK_SET);
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.legacy", path);
    FILE* out = fopen(tmp, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    remove(path);
    rename(tmp, path);
    return sz - 16;
}

int test_header(void) {
    const char* p = "/tmp/stark_test_header";
    t_remove_all(p);

    // New database: header magic + version 1 + flags 0.
    {
        stark_db_t* db = stark_open(p, 0);
        T_ASSERT(db != NULL, "open new db");
        T_ASSERT(stark_add(db, 1, "x", 2) == STARK_OK, "add");
        stark_close(db);

        FILE* f = fopen("/tmp/stark_test_header.idx", "rb");
        T_ASSERT(f != NULL, "open idx");
        unsigned char h[16];
        T_ASSERT(fread(h, 1, 16, f) == 16, "read header");
        fclose(f);
        T_ASSERT(memcmp(h, "STARKDB\0", 8) == 0, "magic");
        uint32_t version, flags;
        memcpy(&version, h + 8, 4);
        memcpy(&flags, h + 12, 4);
        T_ASSERT(version == 1, "version 1");
        T_ASSERT(flags == 0, "flags 0");
    }

    // Empty database: .idx is exactly header + one root page.
    t_remove_all("/tmp/stark_test_empty");
    {
        stark_db_t* db = stark_open("/tmp/stark_test_empty", 0);
        T_ASSERT(db != NULL, "open empty");
        stark_close(db);
        T_ASSERT(t_file_size("/tmp/stark_test_empty.idx") == 16 + 65536,
                 "empty idx size = header + 1 page");
    }
    t_remove_all("/tmp/stark_test_empty");

    // Bad magic rejected.
    {
        unsigned char h[16];
        memset(h, 0, sizeof(h));
        memcpy(h, "NOTSTARK", 8);
        write_raw("/tmp/stark_test_header.idx", h, 65536);
        write_raw("/tmp/stark_test_header.dat", h, 0);
        stark_db_t* db = stark_open(p, 0);
        T_ASSERT(db == NULL, "bad magic rejected");
        if (db) stark_close(db);
    }

    // Newer version rejected.
    {
        unsigned char h[16];
        memset(h, 0, sizeof(h));
        memcpy(h, "STARKDB\0", 8);
        uint32_t v = 2, fl = 0;
        memcpy(h + 8, &v, 4);
        memcpy(h + 12, &fl, 4);
        write_raw("/tmp/stark_test_header.idx", h, 65536);
        write_raw("/tmp/stark_test_header.dat", h, 65536);
        stark_db_t* db = stark_open(p, 0);
        T_ASSERT(db == NULL, "version 2 rejected");
        if (db) stark_close(db);
    }

    // Non-page-multiple size rejected.
    {
        unsigned char h[16];
        memset(h, 0, sizeof(h));
        memcpy(h, "STARKDB\0", 8);
        uint32_t v = 1, fl = 0;
        memcpy(h + 8, &v, 4);
        memcpy(h + 12, &fl, 4);
        write_raw("/tmp/stark_test_header.idx", h, 100);
        remove("/tmp/stark_test_header.dat");
        stark_db_t* db = stark_open(p, 0);
        T_ASSERT(db == NULL, "non-multiple size rejected");
        if (db) stark_close(db);
    }

    // idx/dat header mismatch rejected.
    {
        unsigned char h[16];
        memset(h, 0, sizeof(h));
        memcpy(h, "STARKDB\0", 8);
        uint32_t v = 1, fl = 0;
        memcpy(h + 8, &v, 4);
        memcpy(h + 12, &fl, 4);
        write_raw("/tmp/stark_test_header.idx", h, 65536);   // valid header
        write_raw("/tmp/stark_test_header.dat", NULL, 65536); // legacy, no header
        stark_db_t* db = stark_open(p, 0);
        T_ASSERT(db == NULL, "header mismatch rejected");
        if (db) stark_close(db);
    }

    // Legacy database: rejected on open, upgrade_file() fixes it, data survives.
    t_remove_all("/tmp/stark_test_legacy");
    {
        stark_db_t* db = stark_open("/tmp/stark_test_legacy", 0);
        T_ASSERT(db != NULL, "open legacy seed");
        T_ASSERT(stark_add(db, 7, "world", 6) == STARK_OK, "legacy add");
        stark_close(db);

        T_ASSERT(strip_header_to_legacy("/tmp/stark_test_legacy.idx") > 0, "strip idx");
        T_ASSERT(strip_header_to_legacy("/tmp/stark_test_legacy.dat") > 0, "strip dat");

        db = stark_open("/tmp/stark_test_legacy", 0);
        T_ASSERT(db == NULL, "legacy rejected");
        if (db) stark_close(db);

        T_ASSERT(stark_upgrade_file("/tmp/stark_test_legacy") == STARK_OK, "upgrade");
        db = stark_open("/tmp/stark_test_legacy", 0);
        T_ASSERT(db != NULL, "open after upgrade");
        char b[64];
        size_t s = sizeof(b);
        T_ASSERT(stark_get(db, 7, b, &s) == STARK_OK && memcmp(b, "world", 5) == 0,
                 "data survives upgrade");
        stark_close(db);
    }
    t_remove_all("/tmp/stark_test_legacy");
    t_remove_all(p);

    printf("  header tests passed\n");
    return 0;
}
