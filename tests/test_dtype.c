#include "test_common.h"
#include <stdint.h>

int test_dtype(void) {
    const char* p = "/tmp/stark_test_dtype";
    t_remove_all(p);

    stark_db_t* db = stark_open(p, 0);
    T_ASSERT(db != NULL, "open");

    stark_dtype_t dts[] = {
        STARK_DTYPE_FLOAT32, STARK_DTYPE_FLOAT64, STARK_DTYPE_INT32,
        STARK_DTYPE_INT64, STARK_DTYPE_UINT8, STARK_DTYPE_INT8,
        STARK_DTYPE_INT16
    };
    size_t esz[] = { 4, 8, 4, 8, 1, 1, 2 };
    uint32_t key = 1;

    for (int d = 0; d < 7; d++) {
        size_t n = 10;
        unsigned char* data = malloc(esz[d] * n);
        T_ASSERT(data != NULL, "alloc");
        for (size_t i = 0; i < esz[d] * n; i++) data[i] = (unsigned char)(i * 7 + d);

        T_ASSERT(stark_store_binary(db, key, dts[d], data, 0, n) == STARK_OK, "store dtype");

        stark_dtype_t od;
        size_t oc = n, oe = 0;
        unsigned char out[1024];
        T_ASSERT(stark_load_binary(db, key, &od, out, &oc, &oe) == STARK_OK, "load dtype");
        T_ASSERT(od == dts[d], "dtype round-trip");
        T_ASSERT(oc == n && oe == esz[d], "count/elem_size round-trip");
        T_ASSERT(memcmp(out, data, esz[d] * n) == 0, "data round-trip");

        stark_dtype_t zd;
        void* zp;
        size_t zc, ze;
        T_ASSERT(stark_get_binary_ptr(db, key, &zd, &zp, &zc, &ze) == STARK_OK, "ptr dtype");
        T_ASSERT(zd == dts[d] && zc == n && ze == esz[d], "ptr metadata");

        free(data);
        key++;
    }

    // count == 0 round-trip
    T_ASSERT(stark_store_binary(db, 100, STARK_DTYPE_INT32, NULL, 4, 0) == STARK_OK,
             "store count 0");
    {
        stark_dtype_t od;
        size_t oc = 99, oe = 0;
        unsigned char dummy[1];
        T_ASSERT(stark_load_binary(db, 100, &od, dummy, &oc, &oe) == STARK_OK, "load count 0");
        T_ASSERT(oc == 0 && od == STARK_DTYPE_INT32, "count 0 metadata");
    }

    // Large (~60 KB) array
    {
        size_t n = 15000;
        float* fa = malloc(n * sizeof(float));
        T_ASSERT(fa != NULL, "alloc large");
        for (size_t i = 0; i < n; i++) fa[i] = (float)i;
        T_ASSERT(stark_store_binary(db, 200, STARK_DTYPE_FLOAT32, fa, 0, n) == STARK_OK,
                 "store large");
        float* fb = malloc(n * sizeof(float));
        T_ASSERT(fb != NULL, "alloc large out");
        size_t oc = n, oe = 0;
        stark_dtype_t od;
        T_ASSERT(stark_load_binary(db, 200, &od, fb, &oc, &oe) == STARK_OK, "load large");
        T_ASSERT(oc == n && od == STARK_DTYPE_FLOAT32, "large metadata");
        T_ASSERT(memcmp(fa, fb, n * sizeof(float)) == 0, "large data");
        free(fa);
        free(fb);
    }

    // Legacy format inference: [size_t elem_size][size_t count][data]
    {
        uint32_t k = 300;
        size_t es = 4, cnt = 3;
        uint8_t blob[16 + 12];
        memcpy(blob, &es, 8);
        memcpy(blob + 8, &cnt, 8);
        for (int i = 0; i < 12; i++) blob[16 + i] = (uint8_t)i;
        T_ASSERT(stark_put(db, k, blob, sizeof(blob)) == STARK_OK, "put legacy blob");

        stark_dtype_t od;
        size_t oc = 99, oe = 0;
        unsigned char out[64];
        T_ASSERT(stark_load_binary(db, k, &od, out, &oc, &oe) == STARK_OK, "load legacy");
        T_ASSERT(od == STARK_DTYPE_FLOAT32 && oc == 3 && oe == 4, "legacy inference");
        T_ASSERT(memcmp(out, blob + 16, 12) == 0, "legacy data");
    }

    // Corrupt: magic present but version = 2.
    {
        uint32_t k = 400;
        uint8_t blob[24];
        uint32_t magic = 0x53544259u;
        memcpy(blob, &magic, 4);
        blob[4] = 2;
        blob[5] = 0;
        uint16_t v = 2;
        memcpy(blob + 6, &v, 2);
        size_t es = 4, cnt = 1;
        memcpy(blob + 8, &es, 8);
        memcpy(blob + 16, &cnt, 8);
        T_ASSERT(stark_put(db, k, blob, sizeof(blob)) == STARK_OK, "put corrupt");

        stark_dtype_t od;
        size_t oc = 99, oe = 0;
        unsigned char out[16];
        T_ASSERT(stark_load_binary(db, k, &od, out, &oc, &oe) == STARK_ERROR,
                 "unsupported version rejected");
        T_ASSERT(strstr(stark_error(db), "unsupported binary header version") != NULL,
                 "version error message");
        // Database remains usable.
        T_ASSERT(t_stark_exists(db, 1), "db usable after corrupt read");
    }

    stark_close(db);
    t_remove_all(p);

    printf("  dtype tests passed\n");
    return 0;
}
