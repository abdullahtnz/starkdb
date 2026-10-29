#include "btree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BTREE_DEBUG 0

static LeafNode *get_leaf_node(Pager *pager, page_num_t page_num) {
    return (LeafNode *)pager_get_page(pager, page_num);
}

static InternalNode *get_internal_node(Pager *pager, page_num_t page_num) {
    return (InternalNode *)pager_get_page(pager, page_num);
}

static void initialize_leaf_node(void *page) {
    LeafNode *node = (LeafNode *)page;
    node->header.type = NODE_LEAF;
    node->header.is_root = 0;
    node->header.parent = INVALID_PAGE;
    node->num_cells = 0;
}

static void initialize_internal_node(void *page) {
    InternalNode *node = (InternalNode *)page;
    node->header.type = NODE_INTERNAL;
    node->header.is_root = 0;
    node->header.parent = INVALID_PAGE;
    node->num_keys = 0;
}

BTree *btree_create(Pager *pager) {
    BTree *tree = malloc(sizeof(BTree));
    if (!tree) return NULL;

    tree->pager = pager;

    if (pager->num_pages > 0) {
        tree->root_page_num = 0;
    } else {
        tree->root_page_num = pager_allocate_page(pager);
        void *root_node = pager_get_page(pager, tree->root_page_num);
        initialize_leaf_node(root_node);
        ((LeafNode *)root_node)->header.is_root = 1;
    }

    return tree;
}

static DB_Result leaf_node_insert(LeafNode *node, uint32_t key, uint64_t value) {
    if (node->num_cells >= LEAF_NODE_MAX_CELLS) {
        return DB_FULL;
    }

    int insertion_point = 0;
    while (insertion_point < (int)node->num_cells && node->keys[insertion_point] < key) {
        insertion_point++;
    }

    for (int i = node->num_cells; i > insertion_point; i--) {
        node->keys[i] = node->keys[i - 1];
        node->values[i] = node->values[i - 1];
    }

    node->keys[insertion_point] = key;
    node->values[insertion_point] = value;
    node->num_cells++;

    return DB_SUCCESS;
}

typedef struct {
    int split;            // 1 if the subtree split into two siblings
    uint32_t promote_key; // separator key to insert in the parent
    page_num_t new_page;  // new right sibling page when split
} InsertResult;

// Recursively insert (key,value) into the subtree rooted at page_num.
// If the subtree splits, res->split is set and promote_key/new_page describe
// the new right sibling. Splits propagate naturally up to any height.
static DB_Result insert_rec(BTree *tree, page_num_t page_num,
                            uint32_t key, uint64_t value, InsertResult *res) {
    void *node = pager_get_page(tree->pager, page_num);
    if (!node) return DB_ERROR;
    NodeHeader *header = (NodeHeader *)node;
    res->split = 0;

    if (header->type == NODE_LEAF) {
        LeafNode *leaf = (LeafNode *)node;

        // Existing key: overwrite value in place (counted once).
        for (uint32_t i = 0; i < leaf->num_cells; i++) {
            if (leaf->keys[i] == key) {
                leaf->values[i] = value;
                return DB_SUCCESS;
            }
        }

        if (leaf->num_cells < LEAF_NODE_MAX_CELLS) {
            return leaf_node_insert(leaf, key, value);
        }

        // Full leaf: merge the new entry then split into two halves.
        uint32_t tmp_keys[LEAF_NODE_MAX_CELLS + 1];
        uint64_t tmp_vals[LEAF_NODE_MAX_CELLS + 1];
        uint32_t n = 0, ip = 0;
        while (ip < leaf->num_cells && leaf->keys[ip] < key) {
            tmp_keys[n] = leaf->keys[ip];
            tmp_vals[n] = leaf->values[ip];
            n++; ip++;
        }
        tmp_keys[n] = key;
        tmp_vals[n] = value;
        n++;
        while (ip < leaf->num_cells) {
            tmp_keys[n] = leaf->keys[ip];
            tmp_vals[n] = leaf->values[ip];
            n++; ip++;
        }

        uint32_t left_count = n / 2;
        page_num_t new_page = pager_allocate_page(tree->pager);
        if (new_page == INVALID_PAGE) return DB_FULL;
        LeafNode *new_leaf = get_leaf_node(tree->pager, new_page);
        initialize_leaf_node((void *)new_leaf);
        new_leaf->header.parent = leaf->header.parent;

        for (uint32_t i = 0; i < left_count; i++) {
            leaf->keys[i] = tmp_keys[i];
            leaf->values[i] = tmp_vals[i];
        }
        leaf->num_cells = left_count;

        for (uint32_t i = left_count; i < n; i++) {
            new_leaf->keys[i - left_count] = tmp_keys[i];
            new_leaf->values[i - left_count] = tmp_vals[i];
        }
        new_leaf->num_cells = n - left_count;

        res->split = 1;
        res->promote_key = new_leaf->keys[0];
        res->new_page = new_page;
        return DB_SUCCESS;
    }

    // Internal node: recurse into the appropriate child.
    InternalNode *internal = (InternalNode *)node;
    int child_index = 0;
    while (child_index < (int)internal->num_keys &&
           key >= internal->keys[child_index]) {
        child_index++;
    }

    InsertResult child_res;
    DB_Result r = insert_rec(tree, internal->children[child_index], key, value, &child_res);
    if (r != DB_SUCCESS) return r;
    if (!child_res.split) return DB_SUCCESS;

    // A child split: insert (promote_key, new_page) here.
    if (internal->num_keys < INTERNAL_NODE_MAX_KEYS) {
        for (uint32_t i = internal->num_keys; i > (uint32_t)child_index; i--) {
            internal->keys[i] = internal->keys[i - 1];
            internal->children[i + 1] = internal->children[i];
        }
        internal->keys[child_index] = child_res.promote_key;
        internal->children[child_index + 1] = child_res.new_page;
        internal->num_keys++;
        return DB_SUCCESS;
    }

    // Full internal node: merge then split into two halves.
    uint32_t tmp_keys[INTERNAL_NODE_MAX_KEYS + 1];            // 31
    page_num_t tmp_children[INTERNAL_NODE_MAX_CHILDREN + 1];  // 32

    uint32_t j = 0;
    for (uint32_t i = 0; i < (uint32_t)child_index; i++) {
        tmp_keys[j++] = internal->keys[i];
    }
    tmp_keys[j++] = child_res.promote_key;
    for (uint32_t i = (uint32_t)child_index; i < internal->num_keys; i++) {
        tmp_keys[j++] = internal->keys[i];
    }

    uint32_t jc = 0;
    for (uint32_t i = 0; i <= (uint32_t)child_index; i++) {
        tmp_children[jc++] = internal->children[i];
    }
    tmp_children[jc++] = child_res.new_page;
    for (uint32_t i = (uint32_t)child_index + 1; i <= internal->num_keys; i++) {
        tmp_children[jc++] = internal->children[i];
    }

    uint32_t mid = INTERNAL_NODE_MAX_KEYS / 2;  // 15
    page_num_t new_page = pager_allocate_page(tree->pager);
    if (new_page == INVALID_PAGE) return DB_FULL;
    InternalNode *new_internal = get_internal_node(tree->pager, new_page);
    initialize_internal_node((void *)new_internal);
    new_internal->header.parent = internal->header.parent;

    for (uint32_t i = 0; i < mid; i++) {
        internal->keys[i] = tmp_keys[i];
    }
    for (uint32_t i = 0; i <= mid; i++) {
        internal->children[i] = tmp_children[i];
    }
    internal->num_keys = mid;

    uint32_t nk = j;  // total keys in tmp (num_keys+1)
    uint32_t nc = jc; // total children in tmp (num_keys+2)
    for (uint32_t i = mid + 1; i < nk; i++) {
        new_internal->keys[i - mid - 1] = tmp_keys[i];
    }
    for (uint32_t i = mid + 1; i < nc; i++) {
        new_internal->children[i - mid - 1] = tmp_children[i];
    }
    new_internal->num_keys = nk - mid - 1;

    res->split = 1;
    res->promote_key = tmp_keys[mid];
    res->new_page = new_page;
    return DB_SUCCESS;
}

DB_Result btree_insert(BTree *tree, uint32_t key, uint64_t value) {
    InsertResult res;
    DB_Result r = insert_rec(tree, tree->root_page_num, key, value, &res);
    if (r != DB_SUCCESS) return r;

    if (res.split) {
        // Keep the root pinned at page 0 so it can be recovered on reopen
        // (the on-disk format has no separate root-page metadata). Relocate the
        // old root's (left-half) content to a fresh page, then turn page 0 into
        // the new internal root pointing at [relocated_left, right_sibling].
        page_num_t root_page = tree->root_page_num;
        page_num_t left_page = pager_allocate_page(tree->pager);
        if (left_page == INVALID_PAGE) return DB_FULL;

        void *left_buf = pager_get_page(tree->pager, left_page);
        void *root_buf = pager_get_page(tree->pager, root_page);
        if (!left_buf || !root_buf) return DB_ERROR;

        memcpy(left_buf, root_buf, tree->pager->page_size);
        ((NodeHeader *)left_buf)->is_root = 0;
        ((NodeHeader *)left_buf)->parent = root_page;

        memset(root_buf, 0, tree->pager->page_size);
        InternalNode *new_root = (InternalNode *)root_buf;
        new_root->header.type = NODE_INTERNAL;
        new_root->header.is_root = 1;
        new_root->header.parent = INVALID_PAGE;
        new_root->keys[0] = res.promote_key;
        new_root->children[0] = left_page;
        new_root->children[1] = res.new_page;
        new_root->num_keys = 1;

        void *right = pager_get_page(tree->pager, res.new_page);
        if (right) ((NodeHeader *)right)->parent = root_page;

        // root_page_num stays the same (page 0).
    }

    return DB_SUCCESS;
}

DB_Result btree_find(BTree *tree, uint32_t key, uint64_t *value) {
    page_num_t current_page = tree->root_page_num;
    void *node = pager_get_page(tree->pager, current_page);
    NodeHeader *header = (NodeHeader *)node;

    while (header->type == NODE_INTERNAL) {
        InternalNode *internal = (InternalNode *)node;

        int child_index = 0;
        while (child_index < (int)internal->num_keys && key >= internal->keys[child_index]) {
            child_index++;
        }

        current_page = internal->children[child_index];

        node = pager_get_page(tree->pager, current_page);
        header = (NodeHeader *)node;
    }

    LeafNode *leaf = (LeafNode *)node;

    for (int i = 0; i < (int)leaf->num_cells; i++) {
        if (leaf->keys[i] == key) {
            *value = leaf->values[i];
            return DB_SUCCESS;
        }
    }

    return DB_NOT_FOUND;
}

DB_Result btree_delete(BTree *tree, uint32_t key) {
    if (!tree || !tree->pager) return DB_ERROR;

    page_num_t current_page = tree->root_page_num;
    void *node = pager_get_page(tree->pager, current_page);
    NodeHeader *header = (NodeHeader *)node;

    while (header->type == NODE_INTERNAL) {
        InternalNode *internal = (InternalNode *)node;

        int child_index = 0;
        while (child_index < (int)internal->num_keys && key >= internal->keys[child_index]) {
            child_index++;
        }

        current_page = internal->children[child_index];
        node = pager_get_page(tree->pager, current_page);
        header = (NodeHeader *)node;
    }

    LeafNode *leaf = (LeafNode *)node;

    int found_index = -1;
    for (int i = 0; i < (int)leaf->num_cells; i++) {
        if (leaf->keys[i] == key) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) {
        return DB_NOT_FOUND;
    }

    for (int i = found_index; i < (int)leaf->num_cells - 1; i++) {
        leaf->keys[i] = leaf->keys[i + 1];
        leaf->values[i] = leaf->values[i + 1];
    }

    leaf->num_cells--;

    pager_flush_page(tree->pager, current_page);
    return DB_SUCCESS;
}

static uint32_t count_keys_recursive(BTree *tree, page_num_t page_num) {
    void *node = pager_get_page(tree->pager, page_num);
    if (!node) return 0;
    NodeHeader *header = (NodeHeader *)node;

    if (header->type == NODE_LEAF) {
        return ((LeafNode *)node)->num_cells;
    }

    InternalNode *internal = (InternalNode *)node;
    uint32_t count = 0;
    for (uint32_t i = 0; i <= internal->num_keys; i++) {
        count += count_keys_recursive(tree, internal->children[i]);
    }
    return count;
}

uint32_t btree_count_keys(BTree *tree) {
    if (!tree) return 0;
    if (tree->pager->num_pages == 0) return 0;
    return count_keys_recursive(tree, tree->root_page_num);
}

static uint32_t node_height(BTree *tree, page_num_t page_num) {
    void *node = pager_get_page(tree->pager, page_num);
    if (!node) return 0;
    NodeHeader *header = (NodeHeader *)node;
    if (header->type == NODE_LEAF) return 1;

    InternalNode *internal = (InternalNode *)node;
    uint32_t max_child = 0;
    for (uint32_t i = 0; i <= internal->num_keys; i++) {
        uint32_t h = node_height(tree, internal->children[i]);
        if (h > max_child) max_child = h;
    }
    return 1 + max_child;
}

uint32_t btree_height(const BTree *tree) {
    if (!tree || !tree->pager) return 0;
    if (tree->pager->num_pages == 0) return 0;
    return node_height((BTree *)tree, tree->root_page_num);
}

struct BTreeCursor {
    BTree *tree;
    page_num_t *path_pages;
    int *path_indices;
    int depth;   // index of the leaf level in the path (= height - 1)
    int valid;
};

// Descend from `page_num` (at path `level`) to the leftmost leaf.
static DB_Result descend_left(BTreeCursor *cursor, page_num_t page_num, int level) {
    if (level >= 32) return DB_ERROR;
    void *node = pager_get_page(cursor->tree->pager, page_num);
    if (!node) return DB_ERROR;

    while (((NodeHeader *)node)->type == NODE_INTERNAL) {
        InternalNode *internal = (InternalNode *)node;
        cursor->path_pages[level] = page_num;
        cursor->path_indices[level] = 0;
        level++;
        if (level >= 32) return DB_ERROR;
        page_num = internal->children[0];
        node = pager_get_page(cursor->tree->pager, page_num);
        if (!node) return DB_ERROR;
    }

    cursor->path_pages[level] = page_num;
    cursor->path_indices[level] = 0;
    cursor->depth = level;
    return DB_SUCCESS;
}

// Descend from `page_num` (at path `level`) to the rightmost leaf.
static DB_Result descend_right(BTreeCursor *cursor, page_num_t page_num, int level) {
    if (level >= 32) return DB_ERROR;
    void *node = pager_get_page(cursor->tree->pager, page_num);
    if (!node) return DB_ERROR;

    while (((NodeHeader *)node)->type == NODE_INTERNAL) {
        InternalNode *internal = (InternalNode *)node;
        cursor->path_pages[level] = page_num;
        cursor->path_indices[level] = (int)internal->num_keys;
        level++;
        if (level >= 32) return DB_ERROR;
        page_num = internal->children[internal->num_keys];
        node = pager_get_page(cursor->tree->pager, page_num);
        if (!node) return DB_ERROR;
    }

    cursor->path_pages[level] = page_num;
    cursor->path_indices[level] = (int)((LeafNode *)node)->num_cells - 1;
    cursor->depth = level;
    return DB_SUCCESS;
}

// Move the cursor path to the next leaf in key order (may be empty).
static DB_Result move_to_next_leaf(BTreeCursor *cursor) {
    for (int d = cursor->depth - 1; d >= 0; d--) {
        InternalNode *internal = get_internal_node(cursor->tree->pager, cursor->path_pages[d]);
        if (!internal) return DB_ERROR;
        if (cursor->path_indices[d] + 1 <= (int)internal->num_keys) {
            cursor->path_indices[d] += 1;
            page_num_t child = internal->children[cursor->path_indices[d]];
            return descend_left(cursor, child, d + 1);
        }
    }
    return DB_NOT_FOUND;
}

// Move the cursor path to the previous leaf in key order (may be empty).
static DB_Result move_to_prev_leaf(BTreeCursor *cursor) {
    for (int d = cursor->depth - 1; d >= 0; d--) {
        InternalNode *internal = get_internal_node(cursor->tree->pager, cursor->path_pages[d]);
        if (!internal) return DB_ERROR;
        if (cursor->path_indices[d] - 1 >= 0) {
            cursor->path_indices[d] -= 1;
            page_num_t child = internal->children[cursor->path_indices[d]];
            return descend_right(cursor, child, d + 1);
        }
    }
    return DB_NOT_FOUND;
}

BTreeCursor *btree_cursor_create(BTree *tree) {
    if (!tree) return NULL;
    BTreeCursor *cursor = calloc(1, sizeof(BTreeCursor));
    if (!cursor) return NULL;
    cursor->tree = tree;
    cursor->path_pages = malloc(32 * sizeof(page_num_t));
    cursor->path_indices = malloc(32 * sizeof(int));
    if (!cursor->path_pages || !cursor->path_indices) {
        free(cursor->path_pages);
        free(cursor->path_indices);
        free(cursor);
        return NULL;
    }
    cursor->depth = 0;
    cursor->valid = 0;
    return cursor;
}

DB_Result btree_cursor_first(BTreeCursor *cursor) {
    if (!cursor) return DB_ERROR;
    DB_Result r = descend_left(cursor, cursor->tree->root_page_num, 0);
    if (r != DB_SUCCESS) return r;

    for (;;) {
        LeafNode *leaf = get_leaf_node(cursor->tree->pager, cursor->path_pages[cursor->depth]);
        if (!leaf) return DB_ERROR;
        if (leaf->num_cells > 0) {
            cursor->path_indices[cursor->depth] = 0;
            cursor->valid = 1;
            return DB_SUCCESS;
        }
        r = move_to_next_leaf(cursor);
        if (r != DB_SUCCESS) { cursor->valid = 0; return DB_NOT_FOUND; }
    }
}

DB_Result btree_cursor_last(BTreeCursor *cursor) {
    if (!cursor) return DB_ERROR;
    DB_Result r = descend_right(cursor, cursor->tree->root_page_num, 0);
    if (r != DB_SUCCESS) return r;

    for (;;) {
        LeafNode *leaf = get_leaf_node(cursor->tree->pager, cursor->path_pages[cursor->depth]);
        if (!leaf) return DB_ERROR;
        if (leaf->num_cells > 0) {
            cursor->path_indices[cursor->depth] = (int)leaf->num_cells - 1;
            cursor->valid = 1;
            return DB_SUCCESS;
        }
        r = move_to_prev_leaf(cursor);
        if (r != DB_SUCCESS) { cursor->valid = 0; return DB_NOT_FOUND; }
    }
}

DB_Result btree_cursor_seek(BTreeCursor *cursor, uint32_t key) {
    if (!cursor) return DB_ERROR;

    page_num_t page_num = cursor->tree->root_page_num;
    void *node = pager_get_page(cursor->tree->pager, page_num);
    if (!node) return DB_ERROR;

    int level = 0;
    while (((NodeHeader *)node)->type == NODE_INTERNAL) {
        InternalNode *internal = (InternalNode *)node;
        int child_index = 0;
        while (child_index < (int)internal->num_keys &&
               key >= internal->keys[child_index]) {
            child_index++;
        }

        if (level >= 32) return DB_ERROR;
        cursor->path_pages[level] = page_num;
        cursor->path_indices[level] = child_index;
        level++;

        page_num = internal->children[child_index];
        node = pager_get_page(cursor->tree->pager, page_num);
        if (!node) return DB_ERROR;
    }

    LeafNode *leaf = (LeafNode *)node;
    int idx = 0;
    while (idx < (int)leaf->num_cells && leaf->keys[idx] < key) {
        idx++;
    }

    cursor->path_pages[level] = page_num;
    cursor->path_indices[level] = idx;
    cursor->depth = level;
    cursor->valid = 1;

    if (idx < (int)leaf->num_cells) {
        return DB_SUCCESS;
    }

    // The target is past this leaf; advance to the next non-empty leaf.
    for (;;) {
        DB_Result r = move_to_next_leaf(cursor);
        if (r != DB_SUCCESS) {
            cursor->valid = 0;
            return DB_NOT_FOUND;
        }
        LeafNode *next = get_leaf_node(cursor->tree->pager,
                                       cursor->path_pages[cursor->depth]);
        if (!next) {
            cursor->valid = 0;
            return DB_ERROR;
        }
        if (next->num_cells > 0) {
            cursor->path_indices[cursor->depth] = 0;
            cursor->valid = 1;
            return DB_SUCCESS;
        }
    }
}

DB_Result btree_cursor_next(BTreeCursor *cursor) {
    if (!cursor || !cursor->valid) return DB_NOT_FOUND;

    LeafNode *leaf = get_leaf_node(cursor->tree->pager, cursor->path_pages[cursor->depth]);
    if (!leaf) return DB_ERROR;

    int idx = cursor->path_indices[cursor->depth];
    if (idx + 1 < (int)leaf->num_cells) {
        cursor->path_indices[cursor->depth] = idx + 1;
        return DB_SUCCESS;
    }

    for (;;) {
        DB_Result r = move_to_next_leaf(cursor);
        if (r != DB_SUCCESS) { cursor->valid = 0; return DB_NOT_FOUND; }
        LeafNode *nl = get_leaf_node(cursor->tree->pager, cursor->path_pages[cursor->depth]);
        if (!nl) return DB_ERROR;
        if (nl->num_cells > 0) {
            cursor->path_indices[cursor->depth] = 0;
            cursor->valid = 1;
            return DB_SUCCESS;
        }
    }
}

DB_Result btree_cursor_prev(BTreeCursor *cursor) {
    if (!cursor || !cursor->valid) return DB_NOT_FOUND;

    LeafNode *leaf = get_leaf_node(cursor->tree->pager, cursor->path_pages[cursor->depth]);
    if (!leaf) return DB_ERROR;

    int idx = cursor->path_indices[cursor->depth];
    if (idx - 1 >= 0) {
        cursor->path_indices[cursor->depth] = idx - 1;
        return DB_SUCCESS;
    }

    for (;;) {
        DB_Result r = move_to_prev_leaf(cursor);
        if (r != DB_SUCCESS) { cursor->valid = 0; return DB_NOT_FOUND; }
        LeafNode *pl = get_leaf_node(cursor->tree->pager, cursor->path_pages[cursor->depth]);
        if (!pl) return DB_ERROR;
        if (pl->num_cells > 0) {
            cursor->path_indices[cursor->depth] = (int)pl->num_cells - 1;
            cursor->valid = 1;
            return DB_SUCCESS;
        }
    }
}

DB_Result btree_cursor_get(BTreeCursor *cursor, uint32_t *key, uint64_t *value) {
    if (!cursor || !cursor->valid) return DB_NOT_FOUND;
    if (!key || !value) return DB_ERROR;

    LeafNode *leaf = get_leaf_node(cursor->tree->pager, cursor->path_pages[cursor->depth]);
    int idx = cursor->path_indices[cursor->depth];
    if (!leaf || idx < 0 || idx >= (int)leaf->num_cells) return DB_ERROR;

    *key = leaf->keys[idx];
    *value = leaf->values[idx];
    return DB_SUCCESS;
}

void btree_cursor_destroy(BTreeCursor *cursor) {
    if (!cursor) return;
    free(cursor->path_pages);
    free(cursor->path_indices);
    free(cursor);
}
