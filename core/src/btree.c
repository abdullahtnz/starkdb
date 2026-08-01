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
    while (insertion_point < node->num_cells && node->keys[insertion_point] < key) {
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

static DB_Result split_leaf_node(BTree *tree, LeafNode *old_node, page_num_t old_page_num) {
    page_num_t new_page_num = pager_allocate_page(tree->pager);
    if (new_page_num == INVALID_PAGE) return DB_FULL;

    LeafNode *new_node = get_leaf_node(tree->pager, new_page_num);
    initialize_leaf_node((void *)new_node);

    int split_point = LEAF_NODE_MAX_CELLS / 2;

    for (int i = split_point; i < LEAF_NODE_MAX_CELLS; i++) {
        new_node->keys[i - split_point] = old_node->keys[i];
        new_node->values[i - split_point] = old_node->values[i];
    }
    new_node->num_cells = LEAF_NODE_MAX_CELLS - split_point;
    old_node->num_cells = split_point;

    uint32_t new_key = new_node->keys[0];

    if (old_node->header.is_root) {
        page_num_t root_page_num = pager_allocate_page(tree->pager);
        if (root_page_num == INVALID_PAGE) return DB_FULL;

        InternalNode *root = get_internal_node(tree->pager, root_page_num);
        initialize_internal_node(root);
        root->header.is_root = 1;

        root->children[0] = old_page_num;
        root->keys[0] = new_key;
        root->children[1] = new_page_num;
        root->num_keys = 1;

        old_node->header.is_root = 0;
        old_node->header.parent = root_page_num;
        new_node->header.parent = root_page_num;

        tree->root_page_num = root_page_num;
    } else {
        InternalNode *parent = get_internal_node(tree->pager, old_node->header.parent);

        int insert_index = 0;
        while (insert_index <= parent->num_keys &&
               parent->children[insert_index] != old_page_num) {
            insert_index++;
        }

        for (int i = parent->num_keys; i > insert_index; i--) {
            parent->keys[i] = parent->keys[i - 1];
            parent->children[i + 1] = parent->children[i];
        }

        parent->keys[insert_index] = new_key;
        parent->children[insert_index + 1] = new_page_num;
        parent->num_keys++;

        new_node->header.parent = old_node->header.parent;

        if (parent->num_keys >= INTERNAL_NODE_MAX_KEYS) {
            page_num_t new_internal_page = pager_allocate_page(tree->pager);
            if (new_internal_page == INVALID_PAGE) return DB_FULL;

            InternalNode *new_internal = get_internal_node(tree->pager, new_internal_page);
            initialize_internal_node(new_internal);

            int mid = INTERNAL_NODE_MAX_KEYS / 2;
            uint32_t promote_key = parent->keys[mid];

            for (int i = mid + 1; i < parent->num_keys; i++) {
                new_internal->keys[i - mid - 1] = parent->keys[i];
                new_internal->children[i - mid - 1] = parent->children[i];
            }
            new_internal->children[parent->num_keys - mid - 1] = parent->children[parent->num_keys];
            new_internal->num_keys = parent->num_keys - mid - 1;
            parent->num_keys = mid;

            InternalNode *parent_of_parent = NULL;
            page_num_t parent_parent_page = parent->header.parent;

            if (parent->header.is_root) {
                page_num_t new_root_page = pager_allocate_page(tree->pager);
                if (new_root_page == INVALID_PAGE) return DB_FULL;

                InternalNode *new_root = get_internal_node(tree->pager, new_root_page);
                initialize_internal_node(new_root);
                new_root->header.is_root = 1;

                new_root->keys[0] = promote_key;
                new_root->children[0] = old_node->header.parent;
                new_root->children[1] = new_internal_page;
                new_root->num_keys = 1;

                parent->header.is_root = 0;
                parent->header.parent = new_root_page;
                new_internal->header.parent = new_root_page;

                tree->root_page_num = new_root_page;
            } else if (parent_parent_page != INVALID_PAGE) {
                parent_of_parent = get_internal_node(tree->pager, parent_parent_page);

                int ins = 0;
                while (ins < parent_of_parent->num_keys &&
                       parent_of_parent->children[ins] != old_node->header.parent) {
                    ins++;
                }

                for (int i = parent_of_parent->num_keys; i > ins; i--) {
                    parent_of_parent->keys[i] = parent_of_parent->keys[i - 1];
                    parent_of_parent->children[i + 1] = parent_of_parent->children[i];
                }

                parent_of_parent->keys[ins] = promote_key;
                parent_of_parent->children[ins + 1] = new_internal_page;
                parent_of_parent->num_keys++;

                new_internal->header.parent = parent_parent_page;
            }
        }
    }

    return DB_SUCCESS;
}

DB_Result btree_insert(BTree *tree, uint32_t key, uint64_t value) {
    page_num_t current_page = tree->root_page_num;
    void *node = pager_get_page(tree->pager, current_page);
    NodeHeader *header = (NodeHeader *)node;

    while (header->type == NODE_INTERNAL) {
        InternalNode *internal = (InternalNode *)node;

        int child_index = 0;
        while (child_index < internal->num_keys && key >= internal->keys[child_index]) {
            child_index++;
        }

        current_page = internal->children[child_index];
        node = pager_get_page(tree->pager, current_page);
        header = (NodeHeader *)node;
    }

    LeafNode *leaf = (LeafNode *)node;

    if (leaf->num_cells >= LEAF_NODE_MAX_CELLS) {
        DB_Result result = split_leaf_node(tree, leaf, current_page);
        if (result != DB_SUCCESS) return result;
        return btree_insert(tree, key, value);
    }

    return leaf_node_insert(leaf, key, value);
}

DB_Result btree_find(BTree *tree, uint32_t key, uint64_t *value) {
    page_num_t current_page = tree->root_page_num;
    void *node = pager_get_page(tree->pager, current_page);
    NodeHeader *header = (NodeHeader *)node;

    while (header->type == NODE_INTERNAL) {
        InternalNode *internal = (InternalNode *)node;

        int child_index = 0;
        while (child_index < internal->num_keys && key >= internal->keys[child_index]) {
            child_index++;
        }

        current_page = internal->children[child_index];

        node = pager_get_page(tree->pager, current_page);
        header = (NodeHeader *)node;
    }

    LeafNode *leaf = (LeafNode *)node;

    for (int i = 0; i < leaf->num_cells; i++) {
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
        while (child_index < internal->num_keys && key >= internal->keys[child_index]) {
            child_index++;
        }

        current_page = internal->children[child_index];
        node = pager_get_page(tree->pager, current_page);
        header = (NodeHeader *)node;
    }

    LeafNode *leaf = (LeafNode *)node;

    int found_index = -1;
    for (int i = 0; i < leaf->num_cells; i++) {
        if (leaf->keys[i] == key) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) {
        return DB_NOT_FOUND;
    }

    for (int i = found_index; i < leaf->num_cells - 1; i++) {
        leaf->keys[i] = leaf->keys[i + 1];
        leaf->values[i] = leaf->values[i + 1];
    }

    leaf->num_cells--;

    pager_flush_page(tree->pager, current_page);
    return DB_SUCCESS;
}

uint32_t btree_count_keys(BTree *tree) {
    if (!tree) return 0;
    uint32_t count = 0;

    void *root = pager_get_page(tree->pager, tree->root_page_num);
    NodeHeader *header = (NodeHeader *)root;

    if (header->type == NODE_LEAF) {
        count = ((LeafNode *)root)->num_cells;
    } else {
        void *node = root;
        while (((NodeHeader *)node)->type == NODE_INTERNAL) {
            InternalNode *internal = (InternalNode *)node;
            node = pager_get_page(tree->pager, internal->children[0]);
        }
        count = ((LeafNode *)node)->num_cells;
    }

    return count;
}

struct BTreeCursor {
    BTree *tree;
    page_num_t *path_pages;
    int *path_indices;
    int depth;
    int valid;
};

BTreeCursor *btree_cursor_create(BTree *tree) {
    if (!tree) return NULL;
    BTreeCursor *cursor = calloc(1, sizeof(BTreeCursor));
    if (!cursor) return NULL;
    cursor->tree = tree;
    cursor->path_pages = malloc(32 * sizeof(page_num_t));
    cursor->path_indices = malloc(32 * sizeof(int));
    cursor->depth = 0;
    cursor->valid = 0;
    return cursor;
}

static DB_Result cursor_goto_leaf(BTreeCursor *cursor, page_num_t page_num) {
    void *node = pager_get_page(cursor->tree->pager, page_num);
    if (!node) return DB_ERROR;
    NodeHeader *header = (NodeHeader *)node;

    cursor->depth = 0;

    while (header->type == NODE_INTERNAL) {
        InternalNode *internal = (InternalNode *)node;
        cursor->path_pages[cursor->depth] = page_num;
        cursor->path_indices[cursor->depth] = 0;
        cursor->depth++;

        page_num = internal->children[0];
        node = pager_get_page(cursor->tree->pager, page_num);
        if (!node) return DB_ERROR;
        header = (NodeHeader *)node;
    }

    cursor->path_pages[cursor->depth] = page_num;
    cursor->path_indices[cursor->depth] = 0;

    return DB_SUCCESS;
}

DB_Result btree_cursor_first(BTreeCursor *cursor) {
    if (!cursor) return DB_ERROR;
    DB_Result r = cursor_goto_leaf(cursor, cursor->tree->root_page_num);
    if (r != DB_SUCCESS) return r;

    LeafNode *leaf = get_leaf_node(cursor->tree->pager, cursor->path_pages[cursor->depth]);
    if (leaf->num_cells == 0) {
        cursor->valid = 0;
        return DB_NOT_FOUND;
    }

    cursor->path_indices[cursor->depth] = 0;
    cursor->valid = 1;
    return DB_SUCCESS;
}

DB_Result btree_cursor_last(BTreeCursor *cursor) {
    if (!cursor) return DB_ERROR;

    page_num_t page_num = cursor->tree->root_page_num;
    void *node = pager_get_page(cursor->tree->pager, page_num);
    NodeHeader *header = (NodeHeader *)node;

    cursor->depth = 0;

    while (header->type == NODE_INTERNAL) {
        InternalNode *internal = (InternalNode *)node;
        cursor->path_pages[cursor->depth] = page_num;
        cursor->path_indices[cursor->depth] = internal->num_keys;
        cursor->depth++;

        page_num = internal->children[internal->num_keys];
        node = pager_get_page(cursor->tree->pager, page_num);
        if (!node) return DB_ERROR;
        header = (NodeHeader *)node;
    }

    cursor->path_pages[cursor->depth] = page_num;
    LeafNode *leaf = (LeafNode *)node;

    if (leaf->num_cells == 0) {
        cursor->valid = 0;
        return DB_NOT_FOUND;
    }

    cursor->path_indices[cursor->depth] = leaf->num_cells - 1;
    cursor->valid = 1;
    return DB_SUCCESS;
}

DB_Result btree_cursor_next(BTreeCursor *cursor) {
    if (!cursor || !cursor->valid) return DB_NOT_FOUND;

    page_num_t leaf_page = cursor->path_pages[cursor->depth];
    int idx = cursor->path_indices[cursor->depth];

    LeafNode *leaf = get_leaf_node(cursor->tree->pager, leaf_page);
    if (!leaf) return DB_ERROR;

    if (idx + 1 < leaf->num_cells) {
        cursor->path_indices[cursor->depth] = idx + 1;
        return DB_SUCCESS;
    }

    if (cursor->depth == 0) {
        cursor->valid = 0;
        return DB_NOT_FOUND;
    }

    for (int d = cursor->depth - 1; d >= 0; d--) {
        page_num_t p = cursor->path_pages[d];
        int i = cursor->path_indices[d];

        InternalNode *internal = get_internal_node(cursor->tree->pager, p);
        if (!internal) return DB_ERROR;

        if (i + 1 <= internal->num_keys) {
            cursor->path_indices[d] = i + 1;
            page_num_t next_child = internal->children[i + 1];
            DB_Result r = cursor_goto_leaf(cursor, next_child);
            if (r != DB_SUCCESS) {
                cursor->valid = 0;
                return r;
            }

            leaf = get_leaf_node(cursor->tree->pager, cursor->path_pages[cursor->depth]);
            if (leaf && leaf->num_cells > 0) {
                cursor->path_indices[cursor->depth] = 0;
                cursor->valid = 1;
                return DB_SUCCESS;
            }
        }
    }

    cursor->valid = 0;
    return DB_NOT_FOUND;
}

DB_Result btree_cursor_prev(BTreeCursor *cursor) {
    if (!cursor || !cursor->valid) return DB_NOT_FOUND;

    page_num_t leaf_page = cursor->path_pages[cursor->depth];
    int idx = cursor->path_indices[cursor->depth];

    LeafNode *leaf = get_leaf_node(cursor->tree->pager, leaf_page);
    if (!leaf) return DB_ERROR;

    if (idx - 1 >= 0) {
        cursor->path_indices[cursor->depth] = idx - 1;
        return DB_SUCCESS;
    }

    if (cursor->depth == 0) {
        cursor->valid = 0;
        return DB_NOT_FOUND;
    }

    for (int d = cursor->depth - 1; d >= 0; d--) {
        page_num_t p = cursor->path_pages[d];
        int i = cursor->path_indices[d];

        InternalNode *internal = get_internal_node(cursor->tree->pager, p);
        if (!internal) return DB_ERROR;

        if (i - 1 >= 0) {
            cursor->path_indices[d] = i - 1;
            page_num_t prev_child = internal->children[i - 1];
            cursor->depth = d;

            page_num_t cp = prev_child;
            for (int dd = d + 1; ; dd++) {
                void *cn = pager_get_page(cursor->tree->pager, cp);
                if (!cn) return DB_ERROR;
                NodeHeader *ch = (NodeHeader *)cn;

                if (ch->type == NODE_LEAF) {
                    cursor->path_pages[dd] = cp;
                    leaf = get_leaf_node(cursor->tree->pager, cp);
                    cursor->path_indices[dd] = leaf->num_cells - 1;
                    cursor->depth = dd;
                    cursor->valid = 1;
                    return DB_SUCCESS;
                } else {
                    InternalNode *cin = (InternalNode *)cn;
                    cursor->path_pages[dd] = cp;
                    cursor->path_indices[dd] = cin->num_keys;
                    cp = cin->children[cin->num_keys];
                }
            }
        }
    }

    cursor->valid = 0;
    return DB_NOT_FOUND;
}

DB_Result btree_cursor_get(BTreeCursor *cursor, uint32_t *key, uint64_t *value) {
    if (!cursor || !cursor->valid) return DB_NOT_FOUND;
    if (!key || !value) return DB_ERROR;

    page_num_t leaf_page = cursor->path_pages[cursor->depth];
    int idx = cursor->path_indices[cursor->depth];

    LeafNode *leaf = get_leaf_node(cursor->tree->pager, leaf_page);
    if (!leaf || idx >= leaf->num_cells) return DB_ERROR;

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
