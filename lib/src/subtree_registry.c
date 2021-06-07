#include "alloc.h"
#include "subtree_registry.h"
#include "diff_heap.h"
#include "subtree_share.h"

/**
 * Create a new SubtreeRegistry
 * @return Pointer to new SubtreeRegistry
 */
SubtreeRegistry *ts_subtree_registry_create() {
  SubtreeRegistry *reg = ts_malloc(sizeof(SubtreeRegistry));
  reg->subtrees = raxNew();
  return reg;
}

/**
 * Deletes a SubtreeRegistry
 * @param self Pointer to a SubtreeRegistry
 */
void ts_subtree_registry_delete(SubtreeRegistry *self) {
  raxFree(self->subtrees);
  ts_free(self);
}

/**
 * Deletes a SubtreeRegistry and all contained shares
 * @param self Pointer to a SubtreeRegistry
 */
void ts_subtree_registry_clean_delete(SubtreeRegistry *self) {
  raxIterator iter;
  raxStart(&iter, self->subtrees);
  raxSeek(&iter, "^", NULL, 0);
  while (raxNext(&iter)) {
    SubtreeShare *share = (SubtreeShare *) iter.data;
    ts_subtree_share_delete(share);
  }
  raxStop(&iter);
  raxFree(self->subtrees);
  ts_free(self);
}

/**
 * Assigns a share to a given subtree. Searches the registry by structural hash. If no
 * matching share is found, a new one is created.
 * @param self Pointer to the SubtreeRegistry
 * @param subtree Pointer to a subtree
 * @return Assigned SubtreeShare
 */
SubtreeShare *ts_subtree_registry_assign_share(const SubtreeRegistry *self, Subtree *subtree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (diff_heap->skip_node) {
    return NULL;
  }
  // Reset assigned
  diff_heap->assigned = NULL;
  // Search for a share by structural hash in the registry
  SubtreeShare *result = raxFind(self->subtrees, (unsigned char *) &diff_heap->structural_hash,
                                 sizeof(diff_heap->structural_hash));
  if (result == raxNotFound) {
    // No share found -> create new share and insert into registry
    result = ts_subtree_share_create();
    void *old = NULL;
    raxInsert(self->subtrees, (unsigned char *) &diff_heap->structural_hash, sizeof(diff_heap->structural_hash), result,
              old);
  }
  diff_heap->share = result; // assign share
  return result;
}

/**
 * Assign a share to the given subtree and register the subtree in the share
 * @param self Pointer to SubtreeRegistry
 * @param subtree Pointer to Subtree
 * @return Assigned SubtreeShare
 */
SubtreeShare *ts_subtree_registry_assign_share_and_register_tree(const SubtreeRegistry *self, Subtree *subtree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (diff_heap->skip_node) {
    return NULL;
  }
  SubtreeShare *share = ts_subtree_registry_assign_share(self, subtree);
  ts_subtree_share_register_available_tree(share, subtree);
  return share;
}