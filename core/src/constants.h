#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>
#include <stdbool.h>

#define PAGE_SIZE 65536         // 64KB pages - supports large ML arrays
#define TABLE_MAX_PAGES 200      // Max pages in memory
#define INVALID_PAGE UINT32_MAX

// File format header (identical at the start of both .idx and .dat files).
// Layout (little-endian):
//   offset 0  : 8-byte magic "STARKDB\0"
//   offset 8  : uint32 version
//   offset 12 : uint32 flags (reserved, must be 0)
//   offset 16 : page data begins
#define HEADER_SIZE 16
#define STARKDB_MAGIC "STARKDB\0"
#define STARKDB_MAGIC_LEN 8
#define STARKDB_VERSION 1u

// Compaction sentinel magic (file "<base>.compact").
#define COMPACT_SENTINEL_MAGIC "STARKCOMP"

// Common return codes
typedef enum {
    DB_SUCCESS = 0,
    DB_ERROR = -1,
    DB_NOT_FOUND = -2,
    DB_FULL = -3,
    DB_IO_ERROR = -4,
    DB_MEMORY_ERROR = -5,
    DB_BUFFER_TOO_SMALL = -6
} DB_Result;

// Common data types
typedef uint32_t page_num_t;
typedef uint64_t offset_t;

#endif