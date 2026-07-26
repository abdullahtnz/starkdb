#include "storage.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Storage *storage_create(Pager *pager) {
    Storage *storage = calloc(1, sizeof(Storage));
    if (!storage) return NULL;

    storage->pager = pager;
    storage->current_page = 0;
    storage->current_offset = 0;

    if (pager->num_pages == 0) {
        pager_allocate_page(pager);
        storage->current_page = 0;
        storage->current_offset = DATA_PAGE_START_RESERVE;
    } else {
        storage->current_page = pager->num_pages - 1;
        void *page = pager_get_page(pager, storage->current_page);
        if (page) {
            uint32_t free_off;
            memcpy(&free_off, page, sizeof(uint32_t));
            storage->current_offset = free_off;
        }
    }

    return storage;
}

static uint32_t init_data_page(Pager *pager, uint32_t page_num) {
    void *page = pager_get_page(pager, page_num);
    if (!page) return 0;
    uint32_t free_off = DATA_PAGE_START_RESERVE;
    uint32_t zero = 0;
    memcpy(page, &free_off, sizeof(uint32_t));
    memcpy((char *)page + 4, &zero, sizeof(uint32_t));
    return free_off;
}

DB_Result storage_write(Storage *storage, const void *data, size_t size,
                        data_addr_t *addr) {
    if (!storage || !data || size == 0) return DB_ERROR;

    size_t total_size = sizeof(uint32_t) + size;

    void *page = pager_get_page(storage->pager, storage->current_page);
    if (!page) return DB_ERROR;

    if (storage->current_offset + total_size > PAGE_SIZE) {
        page_num_t new_page = pager_allocate_page(storage->pager);
        if (new_page == INVALID_PAGE) return DB_FULL;

        uint32_t new_off = init_data_page(storage->pager, new_page);
        storage->current_page = new_page;
        storage->current_offset = new_off;
        page = pager_get_page(storage->pager, storage->current_page);
    }

    uint32_t data_size = (uint32_t)size;
    memcpy((char *)page + storage->current_offset, &data_size, sizeof(uint32_t));
    memcpy((char *)page + storage->current_offset + sizeof(uint32_t), data, size);

    *addr = MAKE_ADDR(storage->current_page, storage->current_offset);

    storage->current_offset += total_size;

    uint32_t block_count;
    memcpy(&block_count, (char *)page + 4, sizeof(uint32_t));
    block_count++;
    memcpy((char *)page + 4, &block_count, sizeof(uint32_t));
    memcpy(page, &storage->current_offset, sizeof(uint32_t));

    return DB_SUCCESS;
}

DB_Result storage_read(Storage *storage, data_addr_t addr,
                       void *buffer, size_t *size) {
    if (!storage || !buffer || !size) return DB_ERROR;

    uint32_t page_num = ADDR_PAGE(addr);
    uint32_t offset = ADDR_OFFSET(addr);

    void *page = pager_get_page(storage->pager, page_num);
    if (!page) return DB_ERROR;

    uint32_t data_size;
    memcpy(&data_size, (char *)page + offset, sizeof(uint32_t));

    if (data_size == 0 || data_size > PAGE_SIZE) return DB_ERROR;

    if (*size < data_size) {
        *size = data_size;
        return DB_ERROR;
    }

    memcpy(buffer, (char *)page + offset + sizeof(uint32_t), data_size);
    *size = data_size;

    return DB_SUCCESS;
}

DB_Result storage_delete(Storage *storage, data_addr_t addr) {
    if (!storage) return DB_ERROR;

    uint32_t page_num = ADDR_PAGE(addr);
    uint32_t offset = ADDR_OFFSET(addr);

    void *page = pager_get_page(storage->pager, page_num);
    if (!page) return DB_ERROR;

    uint32_t zero = 0;
    memcpy((char *)page + offset, &zero, sizeof(uint32_t));

    return DB_SUCCESS;
}

size_t storage_get_size(Storage *storage, data_addr_t addr) {
    if (!storage) return 0;

    uint32_t page_num = ADDR_PAGE(addr);
    uint32_t offset = ADDR_OFFSET(addr);

    void *page = pager_get_page(storage->pager, page_num);
    if (!page) return 0;

    uint32_t data_size;
    memcpy(&data_size, (char *)page + offset, sizeof(uint32_t));
    return data_size;
}

void *storage_get_ptr(Storage *storage, data_addr_t addr, size_t *size) {
    if (!storage) return NULL;

    uint32_t page_num = ADDR_PAGE(addr);
    uint32_t offset = ADDR_OFFSET(addr);

    void *page = pager_get_page(storage->pager, page_num);
    if (!page) return NULL;

    if (size) {
        uint32_t data_size;
        memcpy(&data_size, (char *)page + offset, sizeof(uint32_t));
        *size = data_size;
    }

    return (char *)page + offset + sizeof(uint32_t);
}
