#ifndef BTREE_H
#define BTREE_H

#include "constants.h"
#include "pager.h"

typedef enum {
    NODE_INTERNAL,
    NODE_LEAF
} NodeType;

typedef struct {
    NodeType type;
    uint32_t is_root;
    page_num_t parent;
} NodeHeader;

#define LEAF_NODE_MAX_CELLS 31

typedef struct {
    NodeHeader header;
    uint32_t num_cells;
    uint32_t keys[LEAF_NODE_MAX_CELLS];
    uint64_t values[LEAF_NODE_MAX_CELLS];
} LeafNode;

#define INTERNAL_NODE_MAX_KEYS 30
#define INTERNAL_NODE_MAX_CHILDREN 31

typedef struct {
    NodeHeader header;
    uint32_t num_keys;
    uint32_t keys[INTERNAL_NODE_MAX_KEYS];
    page_num_t children[INTERNAL_NODE_MAX_CHILDREN];
} InternalNode;

typedef struct {
    Pager *pager;
    page_num_t root_page_num;
} BTree;

BTree *btree_create(Pager *pager);
DB_Result btree_insert(BTree *tree, uint32_t key, uint64_t value);
DB_Result btree_find(BTree *tree, uint32_t key, uint64_t *value);
DB_Result btree_delete(BTree *tree, uint32_t key);

uint32_t btree_count_keys(BTree *tree);
typedef struct BTreeCursor BTreeCursor;
BTreeCursor *btree_cursor_create(BTree *tree);
DB_Result btree_cursor_first(BTreeCursor *cursor);
DB_Result btree_cursor_last(BTreeCursor *cursor);
DB_Result btree_cursor_next(BTreeCursor *cursor);
DB_Result btree_cursor_prev(BTreeCursor *cursor);
DB_Result btree_cursor_get(BTreeCursor *cursor, uint32_t *key, uint64_t *value);
void btree_cursor_destroy(BTreeCursor *cursor);

#endif
