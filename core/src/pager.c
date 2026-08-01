#include "pager.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

Pager *pager_open(const char *filename) {
    Pager *pager = calloc(1, sizeof(Pager));
    if (!pager) return NULL;

    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    pager->file = fopen(filename, "rb+");
    if (!pager->file) {
        pager->file = fopen(filename, "wb+");
        if (!pager->file) {
            free(pager);
            return NULL;
        }
    }

    pager->page_size = PAGE_SIZE;

    fseek(pager->file, 0, SEEK_END);
    long file_size = ftell(pager->file);
    pager->num_pages = file_size / PAGE_SIZE;

    if (file_size % PAGE_SIZE != 0) {
        fclose(pager->file);
        free(pager);
        return NULL;
    }

    return pager;
}

void pager_close(Pager *pager) {
    if (!pager) return;

    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        if (pager->pages[i]) {
            pager_flush_page(pager, i);
            free(pager->pages[i]);
            pager->pages[i] = NULL;
        }
    }

    fclose(pager->file);
    free(pager);
}

void *pager_get_page(Pager *pager, page_num_t page_num) {
    if (page_num > TABLE_MAX_PAGES) return NULL;

    if (!pager->pages[page_num]) {
        void *page = calloc(1, pager->page_size);
        if (!page) return NULL;

        if (fseek(pager->file, page_num * pager->page_size, SEEK_SET) != 0) {
            free(page);
            return NULL;
        }

        size_t bytes_read = fread(page, pager->page_size, 1, pager->file);
        if (bytes_read < 1 && !feof(pager->file)) {
            free(page);
            return NULL;
        }

        pager->pages[page_num] = page;

        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }
    }

    return pager->pages[page_num];
}

DB_Result pager_flush_page(Pager *pager, page_num_t page_num) {
    if (!pager->pages[page_num]) return DB_SUCCESS;

    if (fseek(pager->file, page_num * pager->page_size, SEEK_SET) != 0) {
        return DB_IO_ERROR;
    }

    size_t bytes_written = fwrite(pager->pages[page_num],
                                  pager->page_size, 1, pager->file);
    if (bytes_written < 1) {
        return DB_IO_ERROR;
    }

    return DB_SUCCESS;
}

DB_Result pager_flush_all(Pager *pager) {
    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        if (pager->pages[i]) {
            pager_flush_page(pager, i);
        }
    }

    if (fflush(pager->file) != 0) {
        return DB_IO_ERROR;
    }

    return DB_SUCCESS;
}

page_num_t pager_allocate_page(Pager *pager) {
    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        if (!pager->pages[i]) {
            void *page = calloc(1, pager->page_size);
            if (!page) return INVALID_PAGE;
            pager->pages[i] = page;

            if (i >= pager->num_pages) {
                pager->num_pages = i + 1;
            }
            return i;
        }
    }
    return INVALID_PAGE;
}
