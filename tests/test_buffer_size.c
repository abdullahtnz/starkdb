#include "test_common.h"
#include <stdint.h>

int test_buffer_size(void) {
    const char* p = "/tmp/stark_test_buffer";
    t_remove_all(p);

    stark_db_t* db = stark_open(p, 0);
    T_ASSERT(db != NULL, "open");
    T_ASSERT(stark_put(db, 1, "hello", 5) == STARK_OK, "put");

    // Numeric getter probe / too small / exact.
    size_t sz = 0;
    T_ASSERT(stark_get(db, 1, NULL, &sz) == STARK_BUFFER_TOO_SMALL, "probe code");
    T_ASSERT(sz == 5, "probe required size");

    char small[2];
    sz = sizeof(small);
    T_ASSERT(stark_get(db, 1, small, &sz) == STARK_BUFFER_TOO_SMALL, "small code");
    T_ASSERT(sz == 5, "small required size");

    char exact[8];
    sz = sizeof(exact);
    T_ASSERT(stark_get(db, 1, exact, &sz) == STARK_OK, "exact ok");
    T_ASSERT(sz == 5 && memcmp(exact, "hello", 5) == 0, "exact data");

    sz = 0;
    T_ASSERT(stark_get(db, 999, NULL, &sz) == STARK_NOT_FOUND, "missing probe");
    T_ASSERT(stark_get(db, 1, NULL, NULL) == STARK_INVALID_ARG, "null size");

    sz = 0;
    T_ASSERT(stark_get(db, 1, exact, &sz) == STARK_BUFFER_TOO_SMALL,
             "zero capacity code");
    T_ASSERT(sz == 5, "zero capacity required");

    // String getter uses the same semantics.
    sz = 0;
    T_ASSERT(stark_get_str(db, "no-such-key", NULL, &sz) == STARK_NOT_FOUND,
             "string missing probe");

    // Typed getter reports the required size.
    FieldDef f;
    memset(&f, 0, sizeof(f));
    strcpy(f.name, "v");
    f.type = TYPE_INT;
    f.size = 4;
    f.offset = 0;
    T_ASSERT(stark_define_type(db, "num", &f, 1) == STARK_OK, "define type");
    T_ASSERT(stark_add_typed(db, "num", 7, "v=123") == STARK_OK, "add typed");

    char tiny[4];
    T_ASSERT(stark_get_typed(db, "num", 7, tiny, sizeof(tiny)) ==
             STARK_BUFFER_TOO_SMALL, "typed too small");
    T_ASSERT(strstr(stark_error(db), "required") != NULL, "typed required message");

    char typed[128];
    T_ASSERT(stark_get_typed(db, "num", 7, typed, sizeof(typed)) == STARK_OK,
             "typed ok");
    T_ASSERT(strcmp(typed, "v=123") == 0, "typed data");
    T_ASSERT(stark_get_typed(db, "num", 7, typed, 0) == STARK_BUFFER_TOO_SMALL,
             "typed zero size");

    // Binary loader honors the input count as capacity.
    float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    T_ASSERT(stark_store_binary(db, 100, STARK_DTYPE_FLOAT32, values, 0, 4) ==
             STARK_OK, "store binary");

    float out[4];
    size_t count = 2, elem = 0;
    stark_dtype_t dt;
    T_ASSERT(stark_load_binary(db, 100, &dt, out, &count, &elem) ==
             STARK_BUFFER_TOO_SMALL, "binary too small");
    T_ASSERT(count == 4 && elem == 4, "binary required count");
    T_ASSERT(strstr(stark_error(db), "required") != NULL, "binary required message");

    count = 0;
    T_ASSERT(stark_load_binary(db, 100, &dt, NULL, &count, &elem) ==
             STARK_BUFFER_TOO_SMALL, "binary null probe");
    T_ASSERT(count == 4, "binary null probe count");

    count = 4;
    T_ASSERT(stark_load_binary(db, 100, &dt, out, &count, &elem) == STARK_OK,
             "binary exact");
    T_ASSERT(count == 4 && elem == 4 && memcmp(out, values, sizeof(values)) == 0,
             "binary data");

    // Cursor getter probe / too small / exact.
    stark_cursor_t* cur = stark_cursor_create(db);
    T_ASSERT(cur != NULL, "cursor create");
    T_ASSERT(stark_cursor_first(cur) == STARK_OK, "cursor first");
    uint32_t key = 0;
    sz = 0;
    T_ASSERT(stark_cursor_get(cur, &key, NULL, &sz) == STARK_BUFFER_TOO_SMALL,
             "cursor probe code");
    T_ASSERT(key == 1 && sz == 5, "cursor probe data");

    char csmall[2];
    sz = sizeof(csmall);
    T_ASSERT(stark_cursor_get(cur, &key, csmall, &sz) == STARK_BUFFER_TOO_SMALL,
             "cursor small code");
    T_ASSERT(sz == 5, "cursor small size");

    char cexact[8];
    sz = sizeof(cexact);
    T_ASSERT(stark_cursor_get(cur, &key, cexact, &sz) == STARK_OK, "cursor exact");
    T_ASSERT(sz == 5 && memcmp(cexact, "hello", 5) == 0, "cursor exact data");
    stark_cursor_destroy(cur);

    stark_close(db);
    t_remove_all(p);
    printf("  buffer size tests passed\n");
    return 0;
}
