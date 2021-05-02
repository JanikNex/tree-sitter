#include "sha256.h"
#include "node_diff_heap.h"
#include "tree.h"
#include "tree_cursor.h"
#include "literal_map.h"

void ts_diff_heap_calculate_structural_hash(TSNode node) {
  SHA256_Context ctxt;
  if (sha256_initialize(&ctxt) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at initialize\n");
    return;
  }
  const char *tag = ts_node_type(node);
  if (sha256_add_bytes(&ctxt, tag, strlen(tag)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at add_bytes of tag\n");
    return;
  }
  for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
    TSNode child = ts_node_child(node, i);
    if (!ts_node_is_null(child)) {
      if (sha256_add_bytes(&ctxt, child.diff_heap->structural_hash, 32) != SHA_DIGEST_OK) {
        fprintf(stderr, "SHA_digest library failure at add_bytes of child\n");
        return;
      }
    }
  }
  if (sha256_calculate(&ctxt, (unsigned char *) &(node.diff_heap->structural_hash)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at calculate\n");
    return;
  }
}

void ts_diff_heap_calculate_literal_hash(TSNode node, const char *code, const TSLiteralMap *literal_map) {
  SHA256_Context ctxt;
  if (sha256_initialize(&ctxt) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at initialize\n");
    return;
  }
  TSSymbol symbol = ts_node_symbol(node);
  if (ts_literal_map_is_literal(literal_map, symbol)) {
    size_t literal_len = ts_node_end_byte(node) - ts_node_start_byte(node);
    //printf("Found literal @%p (+%d) of size %d\n", ((const void *) code) + ts_node_start_byte(node),
    //ts_node_start_byte(node), literal_len);
    if (sha256_add_bytes(&ctxt, ((const void *) code) + ts_node_start_byte(node), literal_len) != SHA_DIGEST_OK) {
      fprintf(stderr, "SHA_digest library failure at add_bytes of tag\n");
      return;
    }
  }
  for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
    TSNode child = ts_node_child(node, i);
    if (!ts_node_is_null(child)) {
      if (sha256_add_bytes(&ctxt, child.diff_heap->literal_hash, 32) != SHA_DIGEST_OK) {
        fprintf(stderr, "SHA_digest library failure at add_bytes of child\n");
        return;
      }
    }
  }
  if (sha256_calculate(&ctxt, (unsigned char *) &(node.diff_heap->literal_hash)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at calculate\n");
    return;
  }
}

bool ts_diff_heap_hash_eq(const unsigned char *hash1, const unsigned char *hash2) {
  return memcmp(hash1, hash2, SHA256_HASH_SIZE);
}

void ts_diff_heap_initialize(TSTree *tree, const char *code, const TSLiteralMap *literal_map) {
  printf("Init Tree %p with %p\n", tree, code);
  // Init cursor
  TSTreeCursor cursor = ts_diff_heap_cursor_create(tree);
  ts_diff_heap_initialize_subtree(&cursor, code, literal_map);
}

TSNodeDiffHeap *
ts_diff_heap_initialize_subtree(TSTreeCursor *cursor, const char *code, const TSLiteralMap *literal_map) {
  Subtree *subtree = ts_diff_heap_cursor_get_subtree(cursor);
  MutableSubtree mut_subtree = ts_subtree_to_mut_unsafe(*subtree);
  TSNodeDiffHeap *node_diff_heap = ts_diff_heap_new();
  int tree_height = 0;
  int tree_size = 0;
  TSNodeDiffHeap *child_heap;
  if (ts_tree_cursor_goto_first_child(cursor)) {
    child_heap = ts_diff_heap_initialize_subtree(cursor, code, literal_map);
    tree_height = child_heap->treeheight > tree_height ? child_heap->treeheight : tree_height;
    tree_size += child_heap->treesize;
    while (ts_tree_cursor_goto_next_sibling(cursor)) {
      child_heap = ts_diff_heap_initialize_subtree(cursor, code, literal_map);
      tree_height = child_heap->treeheight > tree_height ? child_heap->treeheight : tree_height;
      tree_size += child_heap->treesize;
    }
    ts_tree_cursor_goto_parent(cursor);
  }
  node_diff_heap->treesize = 1 + tree_size;
  node_diff_heap->treeheight = 1 + tree_height;
  ts_subtree_assign_node_diff_heap(&mut_subtree, node_diff_heap);
  *subtree = ts_subtree_from_mut(mut_subtree);
  TSNode current_node = ts_tree_cursor_current_node(cursor);
  ts_diff_heap_calculate_structural_hash(current_node);
  ts_diff_heap_calculate_literal_hash(current_node, code, literal_map);
  return node_diff_heap;
}


TSNodeDiffHeap *ts_diff_heap_new() {
  TSNodeDiffHeap *node_diff_heap = ts_malloc(sizeof(TSNodeDiffHeap));
  node_diff_heap->id = ts_malloc(1);
  node_diff_heap->assigned = NULL;
  node_diff_heap->share = NULL;
  node_diff_heap->skip_node = 0;
  return node_diff_heap;
}

void ts_diff_heap_destroy(TSNodeDiffHeap *self) {
  ts_free(self->id);
  ts_free(self);
}


TSTreeCursor ts_diff_heap_cursor_create(const TSTree *tree) {
  TSTreeCursor ts_cursor = {NULL, NULL, {0, 0}};
  TreeCursor *cursor = (TreeCursor *) &ts_cursor;
  cursor->tree = tree;
  array_clear(&cursor->stack);
  array_push(&cursor->stack, ((TreeCursorEntry) {
    .subtree = &tree->root,
    .position = ts_subtree_padding(tree->root),
    .child_index = 0,
    .structural_child_index = 0,
  }));
  return ts_cursor;
}

Subtree *ts_diff_heap_cursor_get_subtree(const TSTreeCursor *cursor) {
  const TreeCursor *self = (const TreeCursor *) cursor;
  TreeCursorEntry *last_entry = array_back(&self->stack);
  return (Subtree *) last_entry->subtree;
}
