#include "alloc.h"
#include "assert.h"
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
  hashmap_create(16, &reg->inc_registry);
  return reg;
}

/**
 * Deletes a SubtreeRegistry
 * @param self Pointer to a SubtreeRegistry
 */
void ts_subtree_registry_delete(SubtreeRegistry *self) {
  raxFree(self->subtrees);
  hashmap_destroy(&self->inc_registry);
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
  hashmap_destroy(&self->inc_registry);
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
  SubtreeShare *share = ts_subtree_registry_assign_share(self, subtree);
  ts_subtree_share_register_available_tree(share, subtree);
  return share;
}

/**
 * Search for a preemptive assignment made in the IncrementalRegistry. If there is
 * an entry for the counterpart of the current node, the corresponding subtree is
 * returned. If there is no entry, a new one is created for the current node and
 * NULL is returned.
 * @param self Pointer to the SubtreeRegistry
 * @param subtree Pointer to the current Subtree
 * @return Pointer to the preemptively assigned Subtree or NULL if none was found
 */
Subtree *ts_subtree_registry_find_incremental_assignment(SubtreeRegistry *self, Subtree *subtree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  assert(diff_heap->preemptive_assignment != NULL);
  TSDiffHeap *target_diff_heap = (TSDiffHeap *) diff_heap->preemptive_assignment;
  Subtree *found_subtree = (Subtree *) hashmap_get(&self->inc_registry, (char *) &target_diff_heap->id, sizeof(void *));
  if (found_subtree == NULL) {
    hashmap_put(&self->inc_registry, (char *) &diff_heap->id, sizeof(void *), subtree);
    return NULL;
  } else {
    hashmap_remove(&self->inc_registry, (char *) &target_diff_heap->id, sizeof(void *));
  }
  return found_subtree;
}