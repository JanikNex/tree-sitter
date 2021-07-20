#include "subtree_share.h"
#include "diff_heap.h"

typedef struct {
    Subtree *subtree;
} HashmapResult;

/**
 * Traverse a subtree and apply a callback function to every node
 * @param node TSNode root
 * @param registry Pointer to the SubtreeRegistry
 * @param f Callback function that expects a TSNode and a Pointer to the SubtreeRegistry
 */
static void foreach_subtree(TSNode node, SubtreeRegistry *registry, void (*f)(TSNode, SubtreeRegistry *)) {
  TSTreeCursor cursor = ts_tree_cursor_new(node);
  int lvl = 0;
  TSNode curr;
  do {
    curr = ts_tree_cursor_current_node(&cursor);
    f(curr, registry);
    while (ts_diff_tree_cursor_goto_first_child(&cursor)) {
      lvl++;
      curr = ts_tree_cursor_current_node(&cursor);
      f(curr, registry);
    }
    while (!(ts_diff_tree_cursor_goto_next_sibling(&cursor)) && lvl > 0) {
      lvl--;
      ts_diff_tree_cursor_goto_parent(&cursor);
    }
  } while (lvl > 0);
  ts_tree_cursor_delete(&cursor);
}

/**
 * HashmapIterate callback function that inserts an available tree into the radix trie of preferred trees.
 *
 * @param context Context given from the iterator call (Pointer to the preferred trees rax struct)
 * @param value Current iteration value (Pointer to a stored subtree)
 * @return 1 to keep the iteration going
 */
static int iterator_preferred_trees_init(void *const context, void *const value) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*(Subtree *) value);
  rax *pref_trees = (rax *) context;
  raxInsert(pref_trees, (unsigned char *) &diff_heap->literal_hash, sizeof(diff_heap->literal_hash), value, NULL);
  return 1;
}

/**
 * HashmapIterate callback function that returns the first element in the hashmap
 *
 * @param context Context given from the iterator call (Pointer to a HashmapResult struct)
 * @param value Current iteration value (Pointer to a stored subtree)
 * @return 0 to stop the iteration
 */
static int iterator_first_element(void *const context, void *const value) {
  *(HashmapResult *) context = ((HashmapResult) {.subtree=value});
  return 0;
}

/**
 * Traverses all childs of the given node and deregisters them as available trees from their shares.
 *
 * @param node TSNode
 * @param registry Pointer to the SubtreeRegistry
 */
static void deregister_foreach_subtree(TSNode node, SubtreeRegistry *registry) {
  TSTreeCursor cursor = ts_tree_cursor_new(node);
  if (ts_diff_tree_cursor_goto_first_child(&cursor)) {
    do {
      TSNode child = ts_tree_cursor_current_node(&cursor);
      ts_subtree_share_deregister_available_tree(child, registry);
    } while (ts_diff_tree_cursor_goto_next_sibling(&cursor));
  }
  ts_tree_cursor_delete(&cursor);
}

/**
 * Looks for the literal hash of the given TSDiffHeap in the shares preferred_trees and removes the entry
 * if the entries value matches the given subtree.
 * @param share Pointer to a SubtreeShare
 * @param diff_heap Pointer to the current TSDiffHeap
 * @param subtree Pointer to the current Subtree
 */
static inline void remove_preferred_tree(SubtreeShare *share, TSDiffHeap *diff_heap, Subtree *subtree) {
  if (share->_preferred_trees != NULL) {
    Subtree *res = raxFind(share->_preferred_trees, (unsigned char *) &diff_heap->literal_hash,
                           sizeof(diff_heap->literal_hash));
    if (res != raxNotFound && res == subtree) {
      raxRemove(share->_preferred_trees, (unsigned char *) &diff_heap->literal_hash, sizeof(diff_heap->literal_hash),
                NULL);
    }
  }
}

/**
 * Foreach callback function
 * @param node TSNode
 * @param registry Pointer to the SubtreeRegistry
 */
static void take_tree_assign_foreach(TSNode node, SubtreeRegistry *registry) {
  TSDiffHeap *diff_heap = (TSDiffHeap *) node.diff_heap;
  if (diff_heap->assigned != NULL) {
    Subtree *that_subtree = diff_heap->assigned;
    ts_subtree_registry_assign_share_and_register_tree(registry, that_subtree);
  }
}

/**
 * Take an available tree and make it (and all subtrees) unavailable
 * @param self Pointer to the SubtreeShare
 * @param tree Target TSNode in the original tree
 * @param that Corresponding TSNode in the changed tree
 * @param registry Pointer to the SubtreeRegistry
 * @return Pointer to the original Subtree
 */
static Subtree *take_tree(const SubtreeShare *self, TSNode tree, TSNode that, SubtreeRegistry *registry) {
  TSDiffHeap *diff_heap = (TSDiffHeap *) tree.diff_heap;
  SubtreeShare *share = diff_heap->share;
  assert(share != NULL);
  // Remove the original tree from the available_tree hashmap and the preferred_trees radix trie
  // Thereby the subtree is no longer available
  hashmap_remove(self->available_trees, (char *) &diff_heap->id, sizeof(void *));
  remove_preferred_tree(share, diff_heap, (Subtree *) tree.id);
  diff_heap->share = NULL;

  // The subtrees of this tree are also no longer available -> deregister
  deregister_foreach_subtree(tree, registry);

  foreach_subtree(that, registry, take_tree_assign_foreach);
  return (Subtree *) tree.id;
}

/**
 * Create a new SubtreeShare
 * Function allocates memory!
 *
 * @return Pointer to the new SubtreeShare or NULL if failed
 */
SubtreeShare *ts_subtree_share_create() {
  SubtreeShare *share = ts_malloc(sizeof(SubtreeShare));
  share->available_trees = ts_malloc(sizeof(struct hashmap_s));
  if (0 != hashmap_create(2, share->available_trees)) {
    return NULL;
  }
  share->_preferred_trees = NULL;
  return share;
}

/**
 * Deletes a SubtreeShare and frees the allocated memory!
 *
 * @param self Pointer to a SubtreeShare
 */
void ts_subtree_share_delete(SubtreeShare *self) {
  hashmap_destroy(self->available_trees);
  ts_free(self->available_trees);
  if (self->_preferred_trees != NULL) {
    raxFree(self->_preferred_trees);
  }
  ts_free(self);
}

/**
 * Register a subtree as an available tree and (if needed) as a preferred tree in a SubtreeShare.
 *
 * @param self Pointer to the SubtreeShare
 * @param subtree Pointer to the Subtree
 */
void ts_subtree_share_register_available_tree(const SubtreeShare *self, Subtree *subtree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (!diff_heap->skip_node) {
    hashmap_put(self->available_trees, (char *) &diff_heap->id, sizeof(void *), subtree);
    if (self->_preferred_trees != NULL) {
      raxInsert(self->_preferred_trees, (unsigned char *) &diff_heap->literal_hash, sizeof(diff_heap->literal_hash),
                subtree, NULL);
    }
  }
}

/**
 * Get the preferred trees. Returns the shares radix trie or generates it if nonexistent.
 * @param self Pointer to the SubtreeShare
 * @return Pointer to the preferred trees rax struct
 */
rax *ts_subtree_share_preferred_trees(SubtreeShare *self) {
  if (self->_preferred_trees == NULL) {
    self->_preferred_trees = raxNew();
    if (hashmap_iterate(self->available_trees, iterator_preferred_trees_init, self->_preferred_trees) != 0) {
      printf("Error on init of preferred_trees");
    }
  }
  return self->_preferred_trees;
}

/**
 * Look for a fitting available tree in the SubtreeShare.
 *
 * @param self Pointer to the SubtreeShare
 * @param node TSNode in the changed tree
 * @param preferred Should it use the literal hash - use structural hash otherwise
 * @param registry Pointer to the SubtreeRegistry
 * @return Pointer to an available Subtree or NULL if none found
 */
Subtree *
ts_subtree_share_take_available_tree(SubtreeShare *self, TSNode node, bool preferred,
                                     SubtreeRegistry *registry) {
  Subtree *subtree = (Subtree *) node.id;
  Subtree *res;
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (preferred) {
    res = raxFind(ts_subtree_share_preferred_trees(self), (unsigned char *) &diff_heap->literal_hash,
                  sizeof(diff_heap->literal_hash));
    if (res == raxNotFound) {
      res = NULL;
    }
  } else {
    HashmapResult iter_res = (HashmapResult) {.subtree=NULL};
    int iter_stat = hashmap_iterate(self->available_trees, iterator_first_element, &iter_res);
    res = iter_stat == 1 ? iter_res.subtree : NULL;
  }
  if (res == NULL) {
    return res;
  }
  TSNode res_node = ts_diff_heap_node(res, node.tree);
  return take_tree(self, res_node, node, registry);
}

/**
 * Deregister an available tree
 * @param node TSNode
 * @param registry Pointer to the SubtreeRegistry
 */
void ts_subtree_share_deregister_available_tree(TSNode node, SubtreeRegistry *registry) {
  TSDiffHeap *diff_heap = (TSDiffHeap *) node.diff_heap;
  if (diff_heap->share != NULL) {
    // Subtree has not been taken previously
    SubtreeShare *share = diff_heap->share;
    // Remove subtree from share, remove share from subtree and deregister all subtrees
    hashmap_remove(share->available_trees, (char *) &diff_heap->id, sizeof(void *));
    remove_preferred_tree(share, diff_heap, (Subtree *) node.id);
    share = NULL;
    deregister_foreach_subtree(node, registry);
  } else if (diff_heap->assigned != NULL) {
    assert(!diff_heap->is_preemptive_assigned);
    // Subtree has been taken previously but was part of a larger subtree
    Subtree *assigned_subtree = diff_heap->assigned;
    TSDiffHeap *assigned_diff_heap = ts_subtree_node_diff_heap(*assigned_subtree);
    // Reset all assignments and reassign shares
    diff_heap->assigned = NULL;
    assigned_diff_heap->assigned = NULL;
    TSNode assigned_node = ts_diff_heap_node(assigned_subtree, node.tree);
    foreach_tree_assign_share(assigned_node, registry);
  }
}