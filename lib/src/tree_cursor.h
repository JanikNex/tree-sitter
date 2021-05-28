#ifndef TREE_SITTER_TREE_CURSOR_H_
#define TREE_SITTER_TREE_CURSOR_H_

#include "./subtree.h"

typedef struct {
  const Subtree *subtree;
  Length position;
  uint32_t child_index;
  uint32_t structural_child_index;
} TreeCursorEntry;

typedef struct {
  const TSTree *tree;
  Array(TreeCursorEntry) stack;
} TreeCursor;

void ts_tree_cursor_init(TreeCursor *, TSNode);
void ts_tree_cursor_current_status(
  const TSTreeCursor *,
  TSFieldId *,
  bool *,
  bool *,
  bool *,
  TSSymbol *,
  unsigned *
);

TSNode ts_tree_cursor_parent_node(const TSTreeCursor *);

bool ts_diff_tree_cursor_goto_parent(TSTreeCursor *);

bool ts_diff_tree_cursor_goto_next_sibling(TSTreeCursor *);

bool ts_diff_tree_cursor_goto_first_child(TSTreeCursor *);

#endif  // TREE_SITTER_TREE_CURSOR_H_
