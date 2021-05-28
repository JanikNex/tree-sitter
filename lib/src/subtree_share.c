#include "subtree_share.h"
#include "diff_heap.h"

typedef struct {
    Subtree *subtree;
} HashmapResult;

static void foreach_subtree(TSNode node, SubtreeRegistry *registry, void (*f)(TSNode, SubtreeRegistry *)) {
  TSTreeCursor cursor = ts_tree_cursor_new(node);
  int lvl = 0;
  if (ts_diff_tree_cursor_goto_first_child(&cursor)) {
    lvl++;
    f(node, registry);
    while (lvl > 0) {
      while (ts_diff_tree_cursor_goto_first_child(&cursor)) {
        lvl++;
        f(node, registry);
      }
      while (!ts_diff_tree_cursor_goto_next_sibling(&cursor) && lvl > 0) {
        lvl--;
        ts_diff_tree_cursor_goto_parent(&cursor);
      }
      if (lvl > 0) {
        f(node, registry);
      }
    }
  }
  ts_tree_cursor_delete(&cursor);
}

static int iterator_preferred_trees_init(void *const context, void *const value) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*(Subtree *) value);
  rax *pref_trees = (rax *) context;
  raxInsert(pref_trees, (unsigned char *) diff_heap->literal_hash, sizeof(diff_heap->literal_hash), value, NULL);
  return 1;
}

static int iterator_first_element(void *const context, void *const value) {
  *(HashmapResult *) context = ((HashmapResult) {.subtree=value});
  return 0;
}

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

static void take_tree_assign_foreach(TSNode node, SubtreeRegistry *registry) {
  TSDiffHeap *diff_heap = (TSDiffHeap *) node.diff_heap;
  if (diff_heap->assigned != NULL) {
    Subtree *that_subtree = diff_heap->assigned;
    ts_subtree_registry_assign_share_and_register_tree(registry, that_subtree);
  }
}

static Subtree *take_tree(const SubtreeShare *self, TSNode tree, TSNode that, SubtreeRegistry *registry) {
  TSDiffHeap *diff_heap = (TSDiffHeap *) tree.diff_heap;
  SubtreeShare *share = diff_heap->share;
  hashmap_remove(self->available_trees, diff_heap->id, sizeof(diff_heap->id));
  if (share->_preferred_trees != NULL) {
    raxRemove(share->_preferred_trees, (unsigned char *) diff_heap->literal_hash, sizeof(diff_heap->literal_hash),
              NULL);
  }
  diff_heap->share = NULL;
  deregister_foreach_subtree(tree, registry);

  foreach_subtree(that, registry, take_tree_assign_foreach);
  return (Subtree *) tree.id;
}


SubtreeShare *ts_subtree_share_create() {
  SubtreeShare *share = ts_malloc(sizeof(SubtreeShare));
  share->available_trees = ts_malloc(sizeof(struct hashmap_s));
  if (0 != hashmap_create(2, share->available_trees)) {
    return NULL;
  }
  share->_preferred_trees = NULL;
  return share;
}

void ts_subtree_share_delete(SubtreeShare *self) {
  hashmap_destroy(self->available_trees);
  ts_free(self->available_trees);
  if (self->_preferred_trees != NULL) {
    raxFree(self->_preferred_trees);
  }
  ts_free(self);
}

void ts_subtree_share_register_available_tree(const SubtreeShare *self, Subtree *subtree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (!diff_heap->skip_node) {
    hashmap_put(self->available_trees, diff_heap->id, sizeof(diff_heap->id), subtree);
    if (self->_preferred_trees != NULL) {
      raxInsert(self->_preferred_trees, (unsigned char *) diff_heap->literal_hash, sizeof(diff_heap->literal_hash),
                subtree, NULL);
    }
  }
}


rax *ts_subtree_share_preferred_trees(SubtreeShare *self) {
  if (self->_preferred_trees == NULL) {
    self->_preferred_trees = raxNew();
    if (hashmap_iterate(self->available_trees, iterator_preferred_trees_init, self->_preferred_trees) != 0) {
      printf("Error on init of preferred_trees");
    }
  }
  return self->_preferred_trees;
}

Subtree *
ts_subtree_share_take_available_tree(SubtreeShare *self, TSNode node, bool preferred,
                                     SubtreeRegistry *registry) {
  Subtree *subtree = (Subtree *) node.id;
  Subtree *res;
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (preferred) {
    res = raxFind(ts_subtree_share_preferred_trees(self), (unsigned char *) diff_heap->literal_hash,
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

void ts_subtree_share_deregister_available_tree(TSNode node, SubtreeRegistry *registry) {
  TSDiffHeap *diff_heap = (TSDiffHeap *) node.diff_heap;
  if (diff_heap->share != NULL) {
    SubtreeShare *share = diff_heap->share;
    hashmap_remove(share->available_trees, diff_heap->id, sizeof(diff_heap->id));
    share = NULL;
    deregister_foreach_subtree(node, registry);
  } else if (diff_heap->assigned != NULL) {
    Subtree *assigned_subtree = diff_heap->assigned;
    TSDiffHeap *assigned_diff_heap = ts_subtree_node_diff_heap(*assigned_subtree);
    diff_heap->assigned = NULL;
    assigned_diff_heap->assigned = NULL;
    TSNode assigned_node = ts_diff_heap_node(assigned_subtree, node.tree);
    foreach_tree_assign_share(assigned_node, registry);
  }
}