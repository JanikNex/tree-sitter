#include "sha_digest/sha256.h"
#include "node_diff_heap.h"
#include "tree.h"
#include "tree_cursor.h"

bool ts_diff_heap_hash_eq(const unsigned char *hash1, const unsigned char *hash2) {
  return memcmp(hash1, hash2, SHA256_HASH_SIZE) == 0;
}

void ts_diff_heap_initialize(const TSTree *tree, const char *code, const TSLiteralMap *literal_map) {
  // Init cursor
  TSTreeCursor cursor = ts_diff_heap_cursor_create(tree);
  ts_diff_heap_initialize_subtree(&cursor, code, literal_map);
  ts_tree_cursor_delete(&cursor);
}

static TSNodeDiffHeap *
ts_diff_heap_initialize_subtree(TSTreeCursor *cursor, const char *code, const TSLiteralMap *literal_map) {
  TSNode node = ts_tree_cursor_current_node(cursor);
  Subtree *subtree = (Subtree *) node.id;
  MutableSubtree mut_subtree = ts_subtree_to_mut_unsafe(*subtree);
  TSNodeDiffHeap *node_diff_heap = ts_diff_heap_new();
  SHA256_Context structural_context;
  SHA256_Context literal_context;
  ts_diff_heap_hash_init(&structural_context, &literal_context, &node, literal_map, code);
  unsigned int tree_height = 0;
  unsigned int tree_size = 0;
  TSNodeDiffHeap *child_heap;
  if (ts_tree_cursor_goto_first_child(cursor)) {
    child_heap = ts_diff_heap_initialize_subtree(cursor, code, literal_map);
    tree_height = child_heap->treeheight > tree_height ? child_heap->treeheight : tree_height;
    tree_size += child_heap->treesize;
    ts_diff_heap_hash_child(&structural_context, &literal_context, child_heap);
    while (ts_tree_cursor_goto_next_sibling(cursor)) {
      child_heap = ts_diff_heap_initialize_subtree(cursor, code, literal_map);
      tree_height = child_heap->treeheight > tree_height ? child_heap->treeheight : tree_height;
      tree_size += child_heap->treesize;
      ts_diff_heap_hash_child(&structural_context, &literal_context, child_heap);
    }
    ts_tree_cursor_goto_parent(cursor);
  }
  node_diff_heap->treesize = 1 + tree_size;
  node_diff_heap->treeheight = 1 + tree_height;
  ts_diff_heap_hash_finalize(&structural_context, &literal_context, node_diff_heap);
  ts_subtree_assign_node_diff_heap(&mut_subtree, node_diff_heap);
  *subtree = ts_subtree_from_mut(mut_subtree);
  return node_diff_heap;
}


static void ts_diff_heap_delete_subtree(TSTreeCursor *cursor) {
  Subtree *subtree = ts_diff_heap_cursor_get_subtree(cursor);
  ts_diff_heap_free(ts_subtree_node_diff_heap(*subtree));
  MutableSubtree mut_subtree = ts_subtree_to_mut_unsafe(*subtree);
  if (ts_tree_cursor_goto_first_child(cursor)) {
    ts_diff_heap_delete_subtree(cursor);
    while (ts_tree_cursor_goto_next_sibling(cursor)) {
      ts_diff_heap_delete_subtree(cursor);
    }
    ts_tree_cursor_goto_parent(cursor);
  }
  ts_subtree_assign_node_diff_heap(&mut_subtree, NULL);
  *subtree = ts_subtree_from_mut(mut_subtree);
}

void ts_diff_heap_delete(const TSTree *tree) {
  TSTreeCursor cursor = ts_diff_heap_cursor_create(tree);
  ts_diff_heap_delete_subtree(&cursor);
  ts_tree_cursor_delete(&cursor);
}


static Subtree *ts_diff_heap_cursor_get_subtree(const TSTreeCursor *cursor) {
  const TreeCursor *self = (const TreeCursor *) cursor;
  TreeCursorEntry *last_entry = array_back(&self->stack);
  return (Subtree *) last_entry->subtree;
}
