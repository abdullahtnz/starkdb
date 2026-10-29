#include "test_common.h"
#include <stdint.h>

static void write_header(const char* path, const char* magic, uint32_t version,
                         long body) {
    unsigned char h[16];
    memset(h, 0, sizeof(h));
    memcpy(h, magic, 8);
    memcpy(h + 8, &version, 4);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(h, 1, sizeof(h), f);
    if (body > 0) {
        unsigned char* z = calloc(1, (size_t)body);
        if (z) { fwrite(z, 1, (size_t)body, f); free(z); }
    }
    fclose(f);
}

int test_open_ex(void) {
    const char* p = "/tmp/stark_test_open_ex";
    t_remove_all(p);

    stark_open_error_t err = STARK_OPEN_ERR_INVALID_ARG;
    stark_db_t* db = NULL;

    // Successful fresh open.
    T_ASSERT(stark_open_ex(&db, p, 0, &err) == STARK_OK, "fresh open");
    T_ASSERT(db != NULL && err == STARK_OPEN_OK, "fresh codes");
    T_ASSERT(stark_last_open_error() == NULL, "fresh last error null");
    stark_close(db);
    t_remove_all(p);

    // Missing database with EXCL.
    db = NULL;
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(&db, p, STARK_OPEN_EXCL, &err) == STARK_ERROR,
             "missing EXCL");
    T_ASSERT(err == STARK_OPEN_ERR_NOT_FOUND, "missing code");
    T_ASSERT(strstr(stark_last_open_error(), "does not exist") != NULL,
             "missing message");

    // Corrupt magic.
    write_header("/tmp/stark_test_open_ex.idx", "NOTSTARK", 1, 65536);
    write_header("/tmp/stark_test_open_ex.dat", "NOTSTARK", 1, 65536);
    db = NULL;
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(&db, p, 0, &err) == STARK_ERROR, "corrupt rejected");
    T_ASSERT(err == STARK_OPEN_ERR_CORRUPT, "corrupt code");
    T_ASSERT(strstr(stark_last_open_error(), "not a STARKDB file") != NULL,
             "corrupt message");
    t_remove_all(p);

    // Future version.
    write_header("/tmp/stark_test_open_ex.idx", "STARKDB\0", 2, 0);
    write_header("/tmp/stark_test_open_ex.dat", "STARKDB\0", 2, 0);
    db = NULL;
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(&db, p, 0, &err) == STARK_ERROR, "version rejected");
    T_ASSERT(err == STARK_OPEN_ERR_VERSION, "version code");
    T_ASSERT(strstr(stark_last_open_error(), "newer") != NULL, "version message");
    t_remove_all(p);

    // idx exists, dat missing.
    write_header("/tmp/stark_test_open_ex.idx", "STARKDB\0", 1, 0);
    db = NULL;
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(&db, p, 0, &err) == STARK_ERROR, "missing dat rejected");
    T_ASSERT(err == STARK_OPEN_ERR_CORRUPT, "missing dat code");
    T_ASSERT(strstr(stark_last_open_error(), "dat file missing") != NULL,
             "missing dat message");
    t_remove_all(p);

    // Invalid arguments.
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(NULL, p, 0, &err) == STARK_INVALID_ARG, "null out_db");
    T_ASSERT(stark_open_ex(&db, NULL, 0, &err) == STARK_INVALID_ARG, "null path");
    T_ASSERT(stark_open_ex(&db, p, 0, NULL) == STARK_INVALID_ARG, "null out_err");
    T_ASSERT(stark_open_ex(&db, "", 0, &err) == STARK_INVALID_ARG, "empty path");
    T_ASSERT(strstr(stark_last_open_error(), "invalid path") != NULL,
             "invalid path message");

    t_remove_all(p);
    printf("  open_ex tests passed\n");
    return 0;
}
