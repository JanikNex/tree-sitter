#ifndef TREE_SITTER_NODE_DIFF_HEAP_H
#define TREE_SITTER_NODE_DIFF_HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tree_sitter/api.h"
#include "subtree.h"

// A heap-allocated structure to hold additional attributes for the truediff algorithm
//
// When using the truediff algorithm, every node is assigned an additional DiffHeap,
// that holds any additional data that is only needed by truediff. Thereby the size
// of a node is increased by just one byte, that can hold a pointer to a DiffHeap.
struct TSNodeDiffHeap {
    void *id;
    const unsigned char structural_hash[SHA256_HASH_SIZE];
    const unsigned char literal_hash[SHA256_HASH_SIZE];
    unsigned int treeheight;
    unsigned int treesize;
    void *share; //TODO: Change type to share
    unsigned int skip_node;
    TSNode *assigned;
};

static inline void ts_diff_heap_free(TSNodeDiffHeap *self) {
  if (self == NULL) {
    return;
  }
  ts_free(self->id);
  ts_free(self);
}

static inline TSNodeDiffHeap *ts_diff_heap_new() {
  TSNodeDiffHeap *node_diff_heap = ts_malloc(sizeof(TSNodeDiffHeap));
  node_diff_heap->id = ts_malloc(1);
  node_diff_heap->assigned = NULL;
  node_diff_heap->share = NULL;
  node_diff_heap->skip_node = 0;
  return node_diff_heap;
}

static inline TSTreeCursor ts_diff_heap_cursor_create(const TSTree *tree) {
  return ts_tree_cursor_new(ts_tree_root_node(tree));
}

static void ts_diff_heap_calculate_structural_hash(const TSNode *node, const TSLiteralMap *literal_map);

static void ts_diff_heap_calculate_literal_hash(const TSNode *node, const char *code,
                                                const TSLiteralMap *literal_map);

static void ts_diff_heap_delete_subtree(TSTreeCursor *cursor);

static Subtree *ts_diff_heap_cursor_get_subtree(const TSTreeCursor *cursor);

static TSNodeDiffHeap *
ts_diff_heap_initialize_subtree(TSTreeCursor *cursor, const char *code, const TSLiteralMap *literal_map);

#ifdef __cplusplus
}
#endif

#endif //TREE_SITTER_NODE_DIFF_HEAP_H
