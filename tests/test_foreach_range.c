#include "test_common.h"
#include <stdint.h>

typedef struct {
    int count;
    uint32_t last;
    int verify_order;
    int stop_after;
    stark_db_t* db;
    int write_on_first;
    int close_on_first;
    int failed;
} IterCtx;

static int iter_cb(uint32_t key, const void* value, size_t value_size, void* user) {
    IterCtx* ctx = (IterCtx*)user;
    (void)value;
    (void)value_size;

    if (ctx->verify_order && ctx->count > 0 && key < ctx->last) ctx->failed = 1;
    ctx->last = key;
    ctx->count++;

    if (ctx->write_on_first && ctx->count == 1 && ctx->db) {
        stark_put(ctx->db, key + 5000000, "x", 1);
    }
    if (ctx->close_on_first && ctx->count == 1 && ctx->db) {
        stark_close(ctx->db);
        ctx->db = NULL;
    }
    if (ctx->stop_after > 0 && ctx->count >= ctx->stop_after) return 1;
    return 0;
}

int test_foreach_range(void) {
    const char* p = "/tmp/stark_test_foreach";
    t_remove_all(p);

    stark_db_t* db = stark_open(p, 0);
    T_ASSERT(db != NULL, "open");

    char v[32];
    for (uint32_t i = 0; i < 10; i++) {
        snprintf(v, sizeof(v), "p%u", i);
        T_ASSERT(stark_add(db, 1000000 + i, v, strlen(v) + 1) == STARK_OK, "add p");
    }
    for (uint32_t i = 0; i < 5; i++) {
        snprintf(v, sizeof(v), "c%u", i);
        T_ASSERT(stark_add(db, 2000000 + i, v, strlen(v) + 1) == STARK_OK, "add c");
    }
    for (uint32_t i = 0; i < 3; i++) {
        snprintf(v, sizeof(v), "m%u", i);
        T_ASSERT(stark_add(db, 3000000 + i, v, strlen(v) + 1) == STARK_OK, "add m");
    }
    for (uint32_t i = 0; i < 2; i++) {
        snprintf(v, sizeof(v), "x%u", i);
        T_ASSERT(stark_add(db, 10000000 + i, v, strlen(v) + 1) == STARK_OK, "add meta");
    }

    IterCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.verify_order = 1;
    T_ASSERT(stark_foreach_range(db, 1000000, 2000000, iter_cb, &ctx) == STARK_OK,
             "players range");
    T_ASSERT(ctx.count == 10 && !ctx.failed, "players count/order");

    memset(&ctx, 0, sizeof(ctx));
    T_ASSERT(stark_foreach_range(db, 2000000, 3000000, iter_cb, &ctx) == STARK_OK,
             "clubs range");
    T_ASSERT(ctx.count == 5, "clubs count");

    memset(&ctx, 0, sizeof(ctx));
    ctx.verify_order = 1;
    T_ASSERT(stark_foreach_range(db, 0, UINT32_MAX, iter_cb, &ctx) == STARK_OK,
             "full range");
    T_ASSERT(ctx.count == 20 && !ctx.failed, "full count/order");

    memset(&ctx, 0, sizeof(ctx));
    ctx.stop_after = 3;
    T_ASSERT(stark_foreach_range(db, 1000000, 2000000, iter_cb, &ctx) == STARK_OK,
             "early stop ok");
    T_ASSERT(ctx.count == 3, "early stop count");

    memset(&ctx, 0, sizeof(ctx));
    T_ASSERT(stark_foreach_range(db, 5000000, 5000000, iter_cb, &ctx) == STARK_OK,
             "min == max");
    T_ASSERT(ctx.count == 0, "min == max count");
    T_ASSERT(stark_foreach_range(db, 6000000, 5000000, iter_cb, &ctx) == STARK_OK,
             "min > max");
    T_ASSERT(ctx.count == 0, "min > max count");

    T_ASSERT(stark_foreach_range(db, 4000000, 5000000, iter_cb, &ctx) == STARK_OK,
             "empty range");
    T_ASSERT(ctx.count == 0, "empty range count");

    // A write from the callback invalidates the cursor and aborts iteration.
    memset(&ctx, 0, sizeof(ctx));
    ctx.write_on_first = 1;
    ctx.db = db;
    T_ASSERT(stark_foreach_range(db, 1000000, 2000000, iter_cb, &ctx) == STARK_ERROR,
             "write during iteration rejected");
    T_ASSERT(ctx.count == 1, "write aborted after first callback");
    T_ASSERT(strstr(stark_error(db), "modified during iteration") != NULL,
             "write message");
    T_ASSERT(stark_exists(db, 6000000), "callback write applied");

    stark_close(db);
    t_remove_all(p);

    // Closing the database inside the callback reports STARK_CLOSED safely.
    const char* q = "/tmp/stark_test_foreach_closed";
    t_remove_all(q);
    stark_db_t* db2 = stark_open(q, 0);
    T_ASSERT(db2 != NULL, "open close-test db");
    T_ASSERT(stark_add(db2, 1, "a", 2) == STARK_OK, "add close-test key");
    memset(&ctx, 0, sizeof(ctx));
    ctx.db = db2;
    ctx.close_on_first = 1;
    T_ASSERT(stark_foreach_range(db2, 0, 100, iter_cb, &ctx) == STARK_CLOSED,
             "closed during iteration");
    T_ASSERT(ctx.count == 1, "closed after first callback");
    t_remove_all(q);

    printf("  foreach_range tests passed\n");
    return 0;
}
