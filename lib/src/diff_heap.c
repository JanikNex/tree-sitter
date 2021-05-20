#include "sha_digest/sha256.h"
#include "diff_heap.h"
#include "tree.h"
#include "subtree_share.h"
#include "pqueue.h"

bool ts_diff_heap_hash_eq(const unsigned char *hash1, const unsigned char *hash2) {
  return memcmp(hash1, hash2, SHA256_HASH_SIZE) == 0;
}

static TSDiffHeap *
ts_diff_heap_initialize_subtree(TSTreeCursor *cursor, const char *code,
                                const TSLiteralMap *literal_map) { // TODO: Possible without recursion?
  TSNode node = ts_tree_cursor_current_node(cursor);
  Subtree *subtree = (Subtree *) node.id;
  MutableSubtree mut_subtree = ts_subtree_to_mut_unsafe(*subtree);
  Length node_position = {.bytes=node.context[0], .extent={.row=node.context[1], .column=node.context[2]}};
  TSDiffHeap *node_diff_heap = ts_diff_heap_new(node_position);
  SHA256_Context structural_context;
  SHA256_Context literal_context;
  ts_diff_heap_hash_init(&structural_context, &literal_context, &node, literal_map, code);
  unsigned int tree_height = 0;
  unsigned int tree_size = 0;
  TSDiffHeap *child_heap;
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

void ts_diff_heap_initialize(const TSTree *tree, const char *code, const TSLiteralMap *literal_map) {
  // Init cursor
  TSTreeCursor cursor = ts_diff_heap_cursor_create(tree);
  ts_diff_heap_initialize_subtree(&cursor, code, literal_map);
  ts_tree_cursor_delete(&cursor);
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

void assign_shares(TSNode this_node, TSNode that_node, SubtreeRegistry *registry) { // TODO: Possible without recursion?
  Subtree *this_subtree = (Subtree *) this_node.id;
  Subtree *that_subtree = (Subtree *) that_node.id;
  TSDiffHeap *this_diff_heap = ts_subtree_node_diff_heap(*this_subtree);
  TSDiffHeap *that_diff_heap = ts_subtree_node_diff_heap(*that_subtree);
  if (this_diff_heap->skip_node) {
    foreach_tree_assign_share(that_node, registry);
    return;
  }
  if (that_diff_heap->skip_node) {
    foreach_tree_assign_share_and_register_tree(this_node, registry);
    return;
  }
  SubtreeShare *this_share = ts_subtree_registry_assign_share(registry, this_subtree);
  SubtreeShare *that_share = ts_subtree_registry_assign_share(registry, that_subtree);
  if (this_share == that_share) {
    assign_tree(this_subtree, that_subtree, this_diff_heap, that_diff_heap);
  } else {
    uint32_t this_child_count = ts_node_child_count(this_node);
    uint32_t that_child_count = ts_node_child_count(that_node);
    // TODO: Do we have to check the fields?
    if (ts_node_symbol(this_node) == ts_node_symbol(that_node) && this_child_count == that_child_count) {
      ts_subtree_share_register_available_tree(this_share, this_subtree);
      for (uint32_t i = 0; i < this_child_count; i++) {
        TSNode this_child = ts_node_child(this_node, i);
        TSNode that_child = ts_node_child(that_node, i);
        assign_shares(this_child, that_child, registry);
      }
    } else {
      foreach_tree_assign_share_and_register_tree(this_node, registry);
      foreach_subtree_assign_share(that_node, registry);
    }
  }

}

TSNode ts_diff_heap_node(const Subtree *subtree, const TSTree *tree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  return (TSNode) {
    {diff_heap->position.bytes, diff_heap->position.extent.row, diff_heap->position.extent.column,
     ts_subtree_symbol(*subtree)},
    subtree,
    tree,
    diff_heap
  };
}

void assign_subtrees(TSNode that_node, SubtreeRegistry *registry) {
  PriorityQueue *queue = priority_queue_create();
  priority_queue_insert(queue, (Subtree *) that_node.id);
  while (!priority_queue_is_empty(queue)) {
    unsigned lvl = priority_queue_head_value(queue);
    NodeEntryArray next_nodes = array_new(); // TODO: Can we move this out of the loop?
    while (!priority_queue_is_empty(queue) && priority_queue_head_value(queue) == lvl) {
      Subtree *next = priority_queue_pop(queue);
      TSDiffHeap *next_diff_heap = ts_subtree_node_diff_heap(*next);
      if (next_diff_heap->assigned == NULL) {
        array_push(&next_nodes, ((NodeEntry) {.subtree=next, .valid=true}));
      }
    }
    select_available_tree(&next_nodes, that_node.tree, true, registry);
    select_available_tree(&next_nodes, that_node.tree, false, registry);
    while (next_nodes.size) {
      NodeEntry entry = array_pop(&next_nodes);
      if (!entry.valid) {
        continue;
      }
      TSNode next_node = ts_diff_heap_node(entry.subtree, that_node.tree);
      for (uint32_t i = 0; i < ts_node_child_count(next_node); i++) {
        TSNode child_node = ts_node_child(next_node, i);
        Subtree *child_subtree = (Subtree *) child_node.id;
        priority_queue_insert(queue, child_subtree);
      }
    }
    array_delete((VoidArray *) &next_nodes);
  }
}

void
select_available_tree(NodeEntryArray *nodes, const TSTree *tree, const bool preferred, SubtreeRegistry *registry) {
  for (uint32_t i = 0; i < nodes->size; i++) {
    NodeEntry *entry = array_get(nodes, i);
    if (!entry->valid) { // TODO: sort by validity to speed up iterations
      continue;
    }
    Subtree *subtree = entry->subtree;
    TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
    if (diff_heap->skip_node) {
      continue;
    } else if (diff_heap->assigned != NULL) {
      entry->valid = false;
    } else {
      SubtreeShare *node_share = diff_heap->share;
      TSNode subtree_node = ts_diff_heap_node(subtree, tree);
      Subtree *available_tree = ts_subtree_share_take_available_tree(node_share, subtree_node, preferred, registry);
      if (available_tree != NULL) {
        TSDiffHeap *available_diff_heap = ts_subtree_node_diff_heap(*available_tree);
        assign_tree(available_tree, subtree, available_diff_heap, diff_heap);
        entry->valid = false;
      }
    }
  }
}


void ts_compare_to(TSNode self, TSNode other) { //TODO: Cleanup -> free used memory
  printf("Create SubtreeRegistry\n");
  SubtreeRegistry *registry = ts_subtree_registry_create();
  printf("AssignShares\n");
  assign_shares(self, other, registry);
  printf("AssignSubtrees\n");
  assign_subtrees(other, registry);
}
