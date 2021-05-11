#include "alloc.h"
#include "subtree_registry.h"
#include "diff_heap.h"
#include "subtree_share.h"

SubtreeRegistry *ts_subtree_registry_create() {
  SubtreeRegistry *reg = ts_malloc(sizeof(SubtreeRegistry));
  reg->subtrees = raxNew();
  return reg;
}

void ts_subtree_registry_delete(SubtreeRegistry *self) {
  raxFree(self->subtrees);
  ts_free(self);
}

SubtreeShare *ts_subtree_registry_assign_share(const SubtreeRegistry *self, Subtree *subtree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (diff_heap->skip_node) {
    return NULL;
  }

  diff_heap->assigned = NULL;
  SubtreeShare *result = raxFind(self->subtrees, (unsigned char *) &diff_heap->structural_hash,
                                 sizeof(diff_heap->structural_hash));
  if (result == raxNotFound) {
    result = ts_subtree_share_create();
    void *old = NULL;
    raxInsert(self->subtrees, (unsigned char *) &diff_heap->structural_hash, sizeof(diff_heap->structural_hash), result,
              old);
  }
  diff_heap->share = result;
  return result;
}

SubtreeShare *ts_subtree_registry_assign_share_and_register_tree(const SubtreeRegistry *self, Subtree *subtree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (diff_heap->skip_node) {
    return NULL;
  }
  SubtreeShare *share = ts_subtree_registry_assign_share(self, subtree);
  ts_subtree_share_register_available_tree(share, subtree);
  return share;
}