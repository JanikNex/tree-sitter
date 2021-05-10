#ifndef TREE_SITTER_DIFF_HEAP_H
#define TREE_SITTER_DIFF_HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tree_sitter/api.h"
#include "subtree.h"
#include "literal_map.h"

// A heap-allocated structure to hold additional attributes for the truediff algorithm
//
// When using the truediff algorithm, every node is assigned an additional DiffHeap,
// that holds any additional data that is only needed by truediff. Thereby the size
// of a node is increased by just one byte, that can hold a pointer to a DiffHeap.
struct TSDiffHeap {
    void *id;
    const unsigned char structural_hash[SHA256_HASH_SIZE];
    const unsigned char literal_hash[SHA256_HASH_SIZE];
    unsigned int treeheight;
    unsigned int treesize;
    void *share; //TODO: Change type to share
    unsigned int skip_node;
    TSNode *assigned;
};

static inline void ts_diff_heap_free(TSDiffHeap *self) {
  if (self == NULL) {
    return;
  }
  ts_free(self->id);
  ts_free(self);
}

static inline TSDiffHeap *ts_diff_heap_new() {
  TSDiffHeap *node_diff_heap = ts_malloc(sizeof(TSDiffHeap));
  node_diff_heap->id = ts_malloc(1); // TODO: Is there a better way to generate URIs?
  node_diff_heap->assigned = NULL;
  node_diff_heap->share = NULL;
  node_diff_heap->skip_node = 0;
  return node_diff_heap;
}

static inline void
ts_diff_heap_hash_init(SHA256_Context *structural_context, SHA256_Context *literal_context, const TSNode *node,
                       const TSLiteralMap *literal_map, const char *code) {
  // Init contexts
  if (sha256_initialize(structural_context) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at initialize\n");
    return;
  }
  if (sha256_initialize(literal_context) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at initialize\n");
    return;
  }
  // Hash node type in structural hash
  const char *tag = ts_node_type(*node);
  if (sha256_add_bytes(structural_context, tag, strlen(tag)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at add_bytes of tag\n");
    return;
  }
  // Hash literal in literal hash if necessary
  TSSymbol symbol = ts_node_symbol(*node);
  if (ts_literal_map_is_literal(literal_map, symbol)) {
    size_t literal_len = ts_node_end_byte(*node) - ts_node_start_byte(*node);
    if (sha256_add_bytes(literal_context, ((const void *) code) + ts_node_start_byte(*node), literal_len) !=
        SHA_DIGEST_OK) {
      fprintf(stderr, "SHA_digest library failure at add_bytes of tag\n");
      return;
    }
  }
}

static inline void
ts_diff_heap_hash_child(SHA256_Context *structural_context, SHA256_Context *literal_context,
                        const TSDiffHeap *child_heap) {
  if (sha256_add_bytes(structural_context, child_heap->structural_hash, 32) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at add_bytes of child\n");
    return;
  }
  if (sha256_add_bytes(literal_context, child_heap->literal_hash, 32) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at add_bytes of child\n");
    return;
  }
}

static inline void
ts_diff_heap_hash_finalize(SHA256_Context *structural_context, SHA256_Context *literal_context,
                           const TSDiffHeap *diff_heap) {
  if (sha256_calculate(structural_context, (unsigned char *) &(diff_heap->structural_hash)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at calculate\n");
    return;
  }
  if (sha256_calculate(literal_context, (unsigned char *) &(diff_heap->literal_hash)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at calculate\n");
    return;
  }
}

static inline TSTreeCursor ts_diff_heap_cursor_create(const TSTree *tree) {
  return ts_tree_cursor_new(ts_tree_root_node(tree));
}

static void ts_diff_heap_delete_subtree(TSTreeCursor *cursor);

static Subtree *ts_diff_heap_cursor_get_subtree(const TSTreeCursor *cursor);

static TSDiffHeap *
ts_diff_heap_initialize_subtree(TSTreeCursor *cursor, const char *code, const TSLiteralMap *literal_map);

#ifdef __cplusplus
}
#endif

#endif //TREE_SITTER_DIFF_HEAP_H
