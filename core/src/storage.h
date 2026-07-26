#ifndef STORAGE_H
#define STORAGE_H

#include "constants.h"
#include "pager.h"

#define DATA_PAGE_START_RESERVE 8

typedef uint64_t data_addr_t;

#define MAKE_ADDR(p, o) (((data_addr_t)(p) << 32) | (data_addr_t)((o) & 0xFFFFFFFF))
#define ADDR_PAGE(a)    ((uint32_t)((a) >> 32))
#define ADDR_OFFSET(a)  ((uint32_t)((a) & 0xFFFFFFFF))

typedef struct {
    Pager *pager;
    uint32_t current_page;
    uint32_t current_offset;
} Storage;

Storage *storage_create(Pager *pager);
DB_Result storage_write(Storage *storage, const void *data, size_t size, data_addr_t *addr);
DB_Result storage_read(Storage *storage, data_addr_t addr, void *buffer, size_t *size);
DB_Result storage_delete(Storage *storage, data_addr_t addr);
size_t storage_get_size(Storage *storage, data_addr_t addr);
void *storage_get_ptr(Storage *storage, data_addr_t addr, size_t *size);

#endif
