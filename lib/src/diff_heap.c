#include "sha_digest/sha256.h"
#include "diff_heap.h"
#include "tree.h"
#include "subtree_share.h"
#include "pqueue.h"


/**
 * Compares two sha256 hashes
 * @param hash1 Pointer to the first hash
 * @param hash2 Pointer to the second hash
 * @return bool is equal
 */
bool ts_diff_heap_hash_eq(const unsigned char *hash1, const unsigned char *hash2) {
  return memcmp(hash1, hash2, SHA256_HASH_SIZE) == 0;
}

static TSDiffHeap *
ts_diff_heap_initialize_subtree(TSTreeCursor *cursor, const char *code,
                                const TSLiteralMap *literal_map) { // TODO: Possible without recursion?
  TSNode node = ts_tree_cursor_current_node(cursor);
  Subtree *subtree = (Subtree *) node.id;
  Length node_position = {.bytes=node.context[0], .extent={.row=node.context[1], .column=node.context[2]}};
  Length node_size = ts_subtree_size(*subtree);
  Length node_padding = ts_subtree_padding(*subtree);

  // Check if there is already an assigned DiffHeap -> reuse and update
  if (node.diff_heap != NULL) {
    TSDiffHeap *existing_diff_heap = ts_subtree_node_diff_heap(*subtree);
    existing_diff_heap->position = node_position;
    existing_diff_heap->padding = node_padding;
    existing_diff_heap->size = node_size;
    if (ts_diff_tree_cursor_goto_first_child(cursor)) {
      ts_diff_heap_initialize_subtree(cursor, code, literal_map);
      while (ts_diff_tree_cursor_goto_next_sibling(cursor)) {
        ts_diff_heap_initialize_subtree(cursor, code, literal_map);
      }
      ts_diff_tree_cursor_goto_parent(cursor);
    }
    return existing_diff_heap;
  }

  // Create new TSDiffHeap
  TSDiffHeap *node_diff_heap = ts_diff_heap_new(node_position, node_padding, node_size);

  //Prepare for hashing
  SHA256_Context structural_context;
  SHA256_Context literal_context;
  ts_diff_heap_hash_init(&structural_context, &literal_context, &node, literal_map, code);

  unsigned int tree_height = 0;
  unsigned int tree_size = 0;
  TSDiffHeap *child_heap;

  // Traverse tree (depth-first)
  // - Initialize children
  // - Whenever the cursor returns a parent node, add children hashes to parent hash
  // - Update treeheight and treesize of the parent node
  if (ts_diff_tree_cursor_goto_first_child(cursor)) {
    child_heap = ts_diff_heap_initialize_subtree(cursor, code, literal_map);
    tree_height = child_heap->treeheight > tree_height ? child_heap->treeheight : tree_height;
    tree_size += child_heap->treesize;
    ts_diff_heap_hash_child(&structural_context, &literal_context, child_heap);
    while (ts_diff_tree_cursor_goto_next_sibling(cursor)) {
      child_heap = ts_diff_heap_initialize_subtree(cursor, code, literal_map);
      tree_height = child_heap->treeheight > tree_height ? child_heap->treeheight : tree_height;
      tree_size += child_heap->treesize;
      ts_diff_heap_hash_child(&structural_context, &literal_context, child_heap);
    }
    ts_diff_tree_cursor_goto_parent(cursor);
  }
  // Update treesize and treeheight of the current node
  node_diff_heap->treesize = 1 + tree_size;
  node_diff_heap->treeheight = 1 + tree_height;

  // Finalize hashes of the current node
  ts_diff_heap_hash_finalize(&structural_context, &literal_context, node_diff_heap);

  // Assign generated DiffHeap to the current node
  MutableSubtree mut_subtree = ts_subtree_to_mut_unsafe(*subtree);
  ts_subtree_assign_node_diff_heap(&mut_subtree, node_diff_heap);
  *subtree = ts_subtree_from_mut(mut_subtree);
  return node_diff_heap;
}

/**
 * Initialize a tree and generate TSDiffHeaps
 * @param tree Tree to be initialized
 * @param code Pointer to the sourcecode of the given tree
 * @param literal_map Pointer to the literalmap of the current language
 */
void ts_diff_heap_initialize(const TSTree *tree, const char *code, const TSLiteralMap *literal_map) {
  // Init cursor
  TSTreeCursor cursor = ts_diff_heap_cursor_create(tree);
  ts_diff_heap_initialize_subtree(&cursor, code, literal_map);
  ts_tree_cursor_delete(&cursor);
}

/**
 * Removes the TSDiffHeap at the current position of the given TreeCursor
 *
 * @param cursor Positioned TSTreeCursor
 */
static void ts_diff_heap_delete_subtree(TSTreeCursor *cursor) {
  Subtree *subtree = ts_diff_heap_cursor_get_subtree(cursor);
  *subtree = ts_diff_heap_del(*subtree);
  // Call function recursively for every child
  if (ts_diff_tree_cursor_goto_first_child(cursor)) {
    ts_diff_heap_delete_subtree(cursor);
    while (ts_diff_tree_cursor_goto_next_sibling(cursor)) {
      ts_diff_heap_delete_subtree(cursor);
    }
    ts_diff_tree_cursor_goto_parent(cursor);
  }
}

/**
 * Removes all TSDiffHeaps of a tree and frees its memory
 *
 * @param tree
 */
void ts_diff_heap_delete(const TSTree *tree) {
  TSTreeCursor cursor = ts_diff_heap_cursor_create(tree);
  ts_diff_heap_delete_subtree(&cursor);
  ts_tree_cursor_delete(&cursor);
}

/**
 * Compares the signatures of two nodes
 *
 * Equality requires:
 * 1. Same symbol
 * 2. Same number of children
 * 3. Every pair of kids is either unnamed or appended to the same field
 *
 * @param this_node Node
 * @param that_node Node
 * @return bool
 */
static bool is_signature_equal(TSNode this_node, TSNode that_node) {
  uint32_t this_child_count = ts_real_node_child_count(this_node);
  uint32_t that_child_count = ts_real_node_child_count(that_node);
  // Check node type
  if (ts_node_symbol(this_node) != ts_node_symbol(that_node)) return false;
  // Check number of children
  if (this_child_count != that_child_count) return false;
  // Check whether children are equally unnamed or appended to the same field
  if (this_child_count > 0) {
    bool field_eq = true;
    TSTreeCursor this_cursor = ts_tree_cursor_new(this_node);
    ts_diff_tree_cursor_goto_first_child(&this_cursor);
    TSTreeCursor that_cursor = ts_tree_cursor_new(that_node);
    ts_diff_tree_cursor_goto_first_child(&that_cursor);
    do {
      TSNode this_kid = ts_tree_cursor_current_node(&this_cursor);
      TSNode that_kid = ts_tree_cursor_current_node(&that_cursor);
      if (ts_node_is_named(this_kid) != ts_node_is_named(that_kid) || ts_tree_cursor_current_field_id(&this_cursor) !=
                                                                      ts_tree_cursor_current_field_id(&that_cursor)) {
        field_eq = false;
        break;
      }
    } while (ts_diff_tree_cursor_goto_next_sibling(&this_cursor) &&
             ts_diff_tree_cursor_goto_next_sibling(&that_cursor));
    ts_tree_cursor_delete(&this_cursor);
    ts_tree_cursor_delete(&that_cursor);
    return field_eq;
  }
  return true;
}

/**
 * STEP 2 - Find reuse candidates
 *
 * Assigns shares to each subtree recursively. The current node of the original and the current node of
 * the changes tree are both considered at the same time.
 *
 * Various cases can arise after the share assignment:
 * 1. Both subtrees are assigned the same share -> assign subtree preemptively
 * 2. Both subtrees are assigned different shares but they have the same type -> recurse simultaneously
 * 3. Both subtrees are assigned different shares and have different types -> recurse separately
 *
 * @param this_node Node in the original tree
 * @param that_node Node in the changed tree
 * @param registry SubtreeRegistry to keep track of all shares
 */
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
  // Assign shares or look into the IncrementalRegistry and search for an assignment if preemptive_assigned
  SubtreeShare *this_share = ts_subtree_registry_assign_share(registry, this_subtree);
  SubtreeShare *that_share = ts_subtree_registry_assign_share(registry, that_subtree);
  if (this_share == that_share) {
    // Both subtrees got the same share -> preemptive assignment
    assign_tree(this_subtree, that_subtree, this_diff_heap, that_diff_heap);
  } else {
    // Subtrees got different shares
    if (is_signature_equal(this_node, that_node)) { // check signature
      // Signatures are equal -> recurse simultaneously
      uint32_t this_child_count = ts_real_node_child_count(this_node);
      ts_subtree_share_register_available_tree(this_share, this_subtree);
      for (uint32_t i = 0; i < this_child_count; i++) {
        TSNode this_child = ts_real_node_child(this_node, i);
        TSNode that_child = ts_real_node_child(that_node, i);
        assign_shares(this_child, that_child, registry);
      }
    } else {
      // Signatures are not equal -> recurse separately
      foreach_tree_assign_share_and_register_tree(this_node, registry);
      foreach_subtree_assign_share(that_subtree, registry);
    }
  }

}

/**
 * Constructs a TSNode with the information of the subtree and the DiffHeap
 * @param subtree Subtree whose node is required
 * @param tree The tree of the subtree
 * @return TSNode of the subtree
 */
TSNode ts_diff_heap_node(const Subtree *subtree, const TSTree *tree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  return (TSNode) {
    {
      diff_heap->position.bytes,
      diff_heap->position.extent.row,
      diff_heap->position.extent.column,
      ts_subtree_symbol(*subtree)
    },
    subtree,
    tree,
    diff_heap
  };
}

/**
 * STEP 3 - Select reuse candidates
 *
 * Creates a priority queue and starts with the first node of the changed tree.
 * Pop a set of nodes with the same tree height from the queue and try to assign
 * a corresponding subtree from the registry.
 * The children of nodes that cannot be assigned are added to the queue.
 *
 * @param that_node Root-Node of the changed tree
 * @param registry SubtreeRegistry
 */
void assign_subtrees(TSNode that_node, SubtreeRegistry *registry) {
  PriorityQueue *queue = priority_queue_create(); // create queue
  priority_queue_insert(queue, (Subtree *) that_node.id); // insert the subtree of the node
  NodeEntryArray next_nodes = array_new(); // create array as working list
  while (!priority_queue_is_empty(queue)) {
    unsigned int lvl = priority_queue_head_value(queue); // store tree height of first element
    while (!priority_queue_is_empty(queue)) {
      if (priority_queue_head_value(queue) != lvl) { // break if the next element has a lower tree height
        break;
      }
      Subtree *next = priority_queue_pop(queue); // pop element from queue
      TSDiffHeap *next_diff_heap = ts_subtree_node_diff_heap(*next);
      if (next_diff_heap->assigned == NULL) { // check if subtree is already assigned
        array_push(&next_nodes, ((NodeEntry) {.subtree=next, .valid=true})); // add not to working list
      }
    }
    select_available_tree(&next_nodes, that_node.tree, true, registry); // try to assign using literal hash
    select_available_tree(&next_nodes, that_node.tree, false, registry); // try to assign using structural hash
    while (next_nodes.size) {
      NodeEntry entry = array_pop(&next_nodes);
      if (entry.valid) { // Test if subtree got assigned
        // Add children of unassigned subtree to queue
        TSNode next_node = ts_diff_heap_node(entry.subtree, that_node.tree);
        for (uint32_t i = 0; i < ts_real_node_child_count(next_node); i++) {
          TSNode child_node = ts_real_node_child(next_node, i);
          Subtree *child_subtree = (Subtree *) child_node.id;
          priority_queue_insert(queue, child_subtree);
        }
      }
    }
  }
  array_delete((VoidArray *) &next_nodes);
  priority_queue_destroy(queue);
}

/**
 * Iterates a list of subtrees look for an assignable subtree in the registry for every subtree that is
 * still unassigned. The preferred parameter indicated whether the literal or the structural hash should be used.
 * @param nodes NodeEntryArray of subtrees
 * @param tree TSTree of the changed tree
 * @param preferred bool that indicated whether to use the structural or literal hash
 * @param registry SubtreeRegistry
 */
void
select_available_tree(NodeEntryArray *nodes, const TSTree *tree, const bool preferred, SubtreeRegistry *registry) {
  for (uint32_t i = 0; i < nodes->size; i++) { // iterate all array elements
    NodeEntry *entry = array_get(nodes, i);
    if (!entry->valid) { // TODO: sort by validity to speed up iterations
      continue; // skip if already processed
    }
    Subtree *subtree = entry->subtree;
    TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
    if (diff_heap->skip_node) {
      continue;
    } else if (diff_heap->assigned != NULL) {
      entry->valid = false; // set invalid if assigned
    } else {
      SubtreeShare *node_share = diff_heap->share;
      assert(node_share != NULL);
      TSNode subtree_node = ts_diff_heap_node(subtree, tree);
      // Search for possible assignable subtree
      Subtree *available_tree = ts_subtree_share_take_available_tree(node_share, subtree_node, preferred, registry);
      if (available_tree != NULL) {
        // assign tree, if an assignable subtree was found
        TSDiffHeap *available_diff_heap = ts_subtree_node_diff_heap(*available_tree);
        assign_tree(available_tree, subtree, available_diff_heap, diff_heap);
        entry->valid = false;
      }
    }
  }
}

/**
 * Compares two nodes and performs an update of the original node if needed
 * @param self TSNode in the original tree
 * @param other TSNode in the changed tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 */
static inline void
update_literals(TSNode self, TSNode other, EditScriptBuffer *buffer, const char *self_code, const char *other_code,
                const TSLiteralMap *literal_map) {
  bool is_literal = ts_literal_map_is_literal(literal_map, ts_node_symbol(self)) &&
                    ts_literal_map_is_literal(literal_map, ts_node_symbol(other));
  Subtree *self_subtree = (Subtree *) self.id;
  Subtree *other_subtree = (Subtree *) other.id;
  TSDiffHeap *self_diff_heap = ts_subtree_node_diff_heap(*self_subtree);
  TSDiffHeap *other_diff_heap = ts_subtree_node_diff_heap(*other_subtree);
  const Length old_size = self_diff_heap->size;
  const Length new_size = other_diff_heap->size;
  const Length self_padding = self_diff_heap->padding;
  const Length other_padding = other_diff_heap->padding;
  const Length self_position = self_diff_heap->position;
  const Length other_position = other_diff_heap->position;
  if (is_literal) { // are those nodes literals
    // Perform update if the length or the content of the literal changed
    if (!length_equal(old_size, new_size) ||
        0 != memcmp(((self_code) + self_position.bytes),
                    ((other_code) + other_position.bytes),
                    old_size.bytes)) {
      Update update_data = { // create update
        .id=self.diff_heap->id,
        .tag=ts_subtree_symbol(*self_subtree),
        .old_start=self_position,
        .old_size=old_size,
        .old_padding=self_padding,
        .new_start=other_position,
        .new_size=new_size,
        .new_padding=other_padding
      };
      // add update to editscript buffer
      ts_edit_script_buffer_add(buffer,
                                (SugaredEdit) {
                                  .edit_tag=UPDATE,
                                  .update=update_data
                                });
    }
  }
  // update node in the original tree if needed
  if (is_literal || !length_equal(old_size, new_size) || !length_equal(self_padding, other_padding) ||
      ts_subtree_has_changes(*self_subtree)) {
    MutableSubtree mut_subtree = ts_subtree_to_mut_unsafe(*self_subtree);
    if (self_subtree->data.is_inline) {
      mut_subtree.data.padding_bytes = other_padding.bytes;
      mut_subtree.data.padding_rows = other_padding.extent.row;
      mut_subtree.data.padding_columns = other_padding.extent.column;
      mut_subtree.data.size_bytes = new_size.bytes;
      mut_subtree.data.has_changes = false;
    } else {
      mut_subtree.ptr->padding = other_padding;
      mut_subtree.ptr->size = new_size;
      mut_subtree.ptr->has_changes = false;
    }
    *self_subtree = ts_subtree_from_mut(mut_subtree);
  }
  // copy literal hash from the changed to to reflect possible literal changes
  memcpy(self_diff_heap->literal_hash, other_diff_heap->literal_hash, SHA256_HASH_SIZE);
  self_diff_heap->position = other_position;
  self_diff_heap->padding = other_padding;
  self_diff_heap->size = new_size;
  if (self_diff_heap->is_preemptive_assigned) { //TODO: Verhaeltniss zum entfernen von Assignments während AssignShares
    reset_preassignment(self_diff_heap);
  }
  // increment the DiffHeap reference counter since this node is reused in the constructed tree
  diff_heap_inc(self_diff_heap);
  self_diff_heap->share = NULL;
  other_diff_heap->share = NULL;
}

/**
 * Update literals for every node in the subtree
 * @param self TSNode in the original tree
 * @param other TSNode in the changed tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 */
static void
update_literals_iter(TSNode self, TSNode other, EditScriptBuffer *buffer, const char *self_code, const char *other_code,
                     const TSLiteralMap *literal_map) {
  TSTreeCursor self_cursor = ts_tree_cursor_new(self);
  TSTreeCursor other_cursor = ts_tree_cursor_new(other);
  int lvl = 0;
  TSNode self_child;
  TSNode other_child;
  do {
    self_child = ts_tree_cursor_current_node(&self_cursor);
    other_child = ts_tree_cursor_current_node(&other_cursor);
    update_literals(self_child, other_child, buffer, self_code, other_code, literal_map);
    while (ts_diff_tree_cursor_goto_first_child(&self_cursor) && ts_diff_tree_cursor_goto_first_child(&other_cursor)) {
      lvl++;
      self_child = ts_tree_cursor_current_node(&self_cursor);
      other_child = ts_tree_cursor_current_node(&other_cursor);
      update_literals(self_child, other_child, buffer, self_code, other_code, literal_map);
    }
    while (
      !(ts_diff_tree_cursor_goto_next_sibling(&self_cursor) &&
        ts_diff_tree_cursor_goto_next_sibling(&other_cursor)) &&
      lvl > 0) {
      lvl--;
      ts_diff_tree_cursor_goto_parent(&self_cursor);
      ts_diff_tree_cursor_goto_parent(&other_cursor);
    }
  } while (lvl > 0);
  ts_tree_cursor_delete(&self_cursor);
  ts_tree_cursor_delete(&other_cursor);

}

/**
 * Check signature and compute the editscript for every child of the given TSNode
 * Creates a new node and assigns the constructed childnodes.
 * @param self TSNode in the original tree
 * @param other TSNode in the changed tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param subtree_pool Pointer to the SubtreePool
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 * @return Constructed subtree or NULL_SUBTREE if signature does not match
 */
Subtree compute_edit_script_recurse(TSNode self, TSNode other, EditScriptBuffer *buffer, SubtreePool *subtree_pool,
                                    const char *self_code,
                                    const char *other_code,
                                    const TSLiteralMap *literal_map) {
  if (is_signature_equal(self, other)) { // check signature
    Subtree *self_subtree = (Subtree *) self.id;
    Subtree *other_subtree = (Subtree *) other.id;
    TSDiffHeap *this_diff_heap = ts_subtree_node_diff_heap(*self_subtree);
    TSDiffHeap *other_diff_heap = ts_subtree_node_diff_heap(*other_subtree);
    diff_heap_inc(this_diff_heap); // increment reference counter since we reuse a DiffHeap from the original tree
    SubtreeArray subtree_array = array_new();
    // Prepare for hashing
    SHA256_Context structural_context;
    SHA256_Context literal_context;
    ts_diff_heap_hash_init(&structural_context, &literal_context, &other, literal_map, other_code); // Init new hash
    unsigned int new_treesize = 0;
    unsigned int new_treeheight = 0;
    for (uint32_t i = 0; i < ts_real_node_child_count(self); i++) {
      TSNode this_kid = ts_real_node_child(self, i);
      TSNode that_kid = ts_real_node_child(other, i);
      // compute editscript and constructed subtree of this child
      Subtree kid_subtree = compute_edit_script(this_kid, that_kid, this_diff_heap->id, ts_node_symbol(self), i,
                                                buffer, subtree_pool, self_code, other_code, literal_map);
      TSDiffHeap *kid_diff_heap = ts_subtree_node_diff_heap(kid_subtree);
      ts_diff_heap_hash_child(&structural_context, &literal_context, kid_diff_heap);
      new_treesize += kid_diff_heap->treesize;
      new_treeheight = kid_diff_heap->treeheight > new_treeheight ? kid_diff_heap->treeheight : new_treeheight;
      array_push(&subtree_array, kid_subtree);
    }
    // Finalize hashes and update DiffHeaps
    ts_diff_heap_hash_finalize(&structural_context, &literal_context, this_diff_heap);
    this_diff_heap->treeheight = 1 + new_treeheight;
    this_diff_heap->treesize = 1 + new_treesize;
    this_diff_heap->position = other_diff_heap->position;
    this_diff_heap->size = other_diff_heap->size;
    this_diff_heap->padding = other_diff_heap->padding;
    this_diff_heap->assigned = NULL;
    this_diff_heap->share = NULL;
    other_diff_heap->assigned = NULL;
    other_diff_heap->share = NULL;
    // create new parent node
    MutableSubtree mut_node = ts_subtree_new_node(ts_node_symbol(other), &subtree_array,
                                                  ts_subtree_production_id(*other_subtree), self.tree->language);
    ts_subtree_assign_node_diff_heap(&mut_node, this_diff_heap); // assign DiffHeap to the new parent node
    Subtree new_node = ts_subtree_from_mut(mut_node);
    return new_node;
  } else {
    return NULL_SUBTREE;
  }
}

/**
 * Traverses all subtrees of the given node of the original tree recursively and creates UnloadEdits
 * for all unassigned nodes.
 * @param self TSNode in the original tree
 * @param buffer Pointer to the EditScriptBuffer
 */
void unload_unassigned(TSNode self, EditScriptBuffer *buffer) {
  Subtree *self_subtree = (Subtree *) self.id;
  TSDiffHeap *this_diff_heap = ts_subtree_node_diff_heap(*self_subtree);
  this_diff_heap->share = NULL; // reset share
  if (this_diff_heap->assigned != NULL) { // check if assigned
    this_diff_heap->assigned = NULL; // reset assignment
  } else {
    // create basic unload
    Unload unload_data = {
      .id=this_diff_heap->id,
      .tag=ts_node_symbol(self)
    };
    ChildPrototypeArray child_prototypes = array_new(); // create array to hold the ids of all children
    for (uint32_t i = 0; i < ts_real_node_child_count(self); i++) {
      TSNode child = ts_real_node_child(self, i);
      const TSDiffHeap *child_diff_heap = child.diff_heap;
      array_push(&child_prototypes, (ChildPrototype) {.child_id=child_diff_heap->id});
    }
    unload_data.kids = child_prototypes; // add the ChildPrototypeArray to the unload
    ts_edit_script_buffer_add(buffer,
                              (SugaredEdit) {
                                .edit_tag=UNLOAD,
                                .unload=unload_data
                              }); // create unload
    for (uint32_t i = 0; i < ts_real_node_child_count(self); i++) { // recursive call
      TSNode child = ts_real_node_child(self, i);
      unload_unassigned(child, buffer);
    }
  }
}

/**
 * Load all nodes that are part of the changed tree but do not exist in the original tree, recursively
 * @param other TSNode in the changed tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 * @param self_tree Pointer to the original tree
 * @param subtree_pool Pointer to the SubtreePool
 * @return Constructed Subtree
 */
static Subtree load_unassigned(TSNode other, EditScriptBuffer *buffer, const char *self_code, const char *other_code,
                               const TSLiteralMap *literal_map, const TSTree *self_tree, SubtreePool *subtree_pool) {
  Subtree *other_subtree = (Subtree *) other.id;
  TSDiffHeap *other_diff_heap = ts_subtree_node_diff_heap(*other_subtree);
  if (other_diff_heap->assigned != NULL) { // check if assigned
    // Assigned -> No LoadEdit needed -> try to update literals
    const Subtree *assigned_subtree = other_diff_heap->assigned; // get the assigned Subtree in the original tree
    TSNode assigned_node = ts_diff_heap_node(assigned_subtree, self_tree); // get the assigned node
    update_literals_iter(assigned_node, other, buffer, self_code, other_code, literal_map); // update literals
    ts_subtree_retain(*assigned_subtree); // increment reference counter of the subtree, since we reuse it
    return *assigned_subtree;
  }
  other_diff_heap->share = NULL;
  void *new_id = generate_new_id(); // generate new ID for the new subtree
  Length node_position = {.bytes=other.context[0], .extent={.row=other.context[1], .column=other.context[2]}};
  Length node_padding = ts_subtree_padding(*other_subtree);
  Length node_size = ts_subtree_size(*other_subtree);
  TSDiffHeap *new_node_diff_heap = ts_diff_heap_new_with_id(node_position, node_padding, node_size,
                                                            new_id); // create new DiffHeap
  SHA256_Context structural_context;
  SHA256_Context literal_context;
  ts_diff_heap_hash_init(&structural_context, &literal_context, &other, literal_map, other_code); // init hash
  Load load_data = {
    .id=new_id,
    .tag=ts_node_symbol(other),
  };
  if (ts_real_node_child_count(other) > 0) { // test for children to decide if it's a node or a leaf
    // -> Node
    SubtreeArray kids = array_new();
    ChildPrototypeArray child_prototypes = array_new();
    unsigned int tree_size = 0;
    unsigned int tree_height = 0;
    for (uint32_t i = 0; i < ts_real_node_child_count(other); i++) { // load children
      TSNode other_kid = ts_real_node_child(other, i);
      Subtree kid_subtree = load_unassigned(other_kid, buffer, self_code, other_code, literal_map, self_tree,
                                            subtree_pool);
      TSDiffHeap *kid_diff_heap = ts_subtree_node_diff_heap(kid_subtree); // get the child's DiffHeap
      array_push(&child_prototypes, (ChildPrototype) {.child_id=kid_diff_heap->id});
      tree_height = kid_diff_heap->treeheight > tree_height ? kid_diff_heap->treeheight : tree_height;
      tree_size += kid_diff_heap->treesize;
      ts_diff_heap_hash_child(&structural_context, &literal_context, kid_diff_heap); // hash child
      array_push(&kids, kid_subtree);
    }
    ts_diff_heap_hash_finalize(&structural_context, &literal_context, new_node_diff_heap);
    new_node_diff_heap->treeheight = 1 + tree_height;
    new_node_diff_heap->treesize = 1 + tree_size;
    // Create new node
    MutableSubtree mut_node;
    if (ts_subtree_is_error(*other_subtree)) { // check for error node
      Subtree err_node = ts_subtree_new_error_node(&kids, ts_subtree_extra(*other_subtree), self_tree->language);
      mut_node = ts_subtree_to_mut_unsafe(err_node);
    } else {
      mut_node = ts_subtree_new_node(ts_node_symbol(other), &kids,
                                     ts_subtree_production_id(*other_subtree), self_tree->language);
    }
    ts_subtree_assign_node_diff_heap(&mut_node, new_node_diff_heap); // assign DiffHeap to the new node
    Subtree new_node = ts_subtree_from_mut(mut_node);
    load_data.is_leaf = false;
    EditNodeData node_data = {.kids=child_prototypes, .production_id=ts_subtree_production_id(*other_subtree)};
    load_data.node = node_data;
    // Create LoadEdit
    ts_edit_script_buffer_add(buffer,
                              (SugaredEdit) {
                                .edit_tag=LOAD,
                                .load=load_data
                              });
    return new_node;
  } else {
    // -> Leaf
    load_data.is_leaf = true;
    EditLeafData leaf_data = {
      .padding=ts_subtree_padding(*other_subtree),
      .size=ts_subtree_size(*other_subtree),
      .lookahead_bytes=ts_subtree_lookahead_bytes(*other_subtree),
      .parse_state=ts_subtree_parse_state(*other_subtree),
      .has_external_tokens=ts_subtree_has_external_tokens(*other_subtree),
      .depends_on_column=ts_subtree_depends_on_column(*other_subtree),
      .is_keyword=ts_subtree_is_keyword(*other_subtree)
    };
    if (ts_subtree_has_external_tokens(*other_subtree)) {
      const ExternalScannerState *node_state = &other_subtree->ptr->external_scanner_state;
      leaf_data.external_scanner_state = ts_external_scanner_state_copy(node_state);
    } else if (ts_subtree_is_error(*other_subtree)) {
      leaf_data.lookahead_char = other_subtree->ptr->lookahead_char;
    }
    load_data.leaf = leaf_data;
    // Create LoadEdit
    ts_edit_script_buffer_add(buffer,
                              (SugaredEdit) {
                                .edit_tag=LOAD,
                                .load = load_data
                              });
    // Create new leaf
    Subtree new_leaf;
    if (ts_subtree_is_error(*other_subtree)) { // check for error leaf
      int32_t lookahead_char = other_subtree->ptr->lookahead_char;
      new_leaf = ts_subtree_new_error(subtree_pool, lookahead_char, ts_subtree_padding(*other_subtree),
                                      ts_subtree_size(*other_subtree),
                                      ts_subtree_lookahead_bytes(*other_subtree),
                                      ts_subtree_parse_state(*other_subtree), self_tree->language);
    } else {
      new_leaf = ts_subtree_new_leaf(subtree_pool, ts_subtree_symbol(*other_subtree),
                                     ts_subtree_padding(*other_subtree),
                                     ts_subtree_size(*other_subtree),
                                     ts_subtree_lookahead_bytes(*other_subtree),
                                     ts_subtree_parse_state(*other_subtree),
                                     ts_subtree_has_external_tokens(*other_subtree),
                                     ts_subtree_depends_on_column(*other_subtree),
                                     ts_subtree_is_keyword(*other_subtree), self_tree->language);
    }
    ts_diff_heap_hash_finalize(&structural_context, &literal_context, new_node_diff_heap);
    new_node_diff_heap->treeheight = 1;
    new_node_diff_heap->treesize = 1;
    MutableSubtree mut_leaf = ts_subtree_to_mut_unsafe(new_leaf);
    if (ts_subtree_has_external_tokens(*other_subtree)) {
      const ExternalScannerState *node_state = &other_subtree->ptr->external_scanner_state;
      mut_leaf.ptr->external_scanner_state = ts_external_scanner_state_copy(node_state);
    }
    ts_subtree_assign_node_diff_heap(&mut_leaf, new_node_diff_heap); // assign DiffHeap to the new leaf
    new_leaf = ts_subtree_from_mut(mut_leaf);
    return new_leaf;
  }

}

/**
 * Computes an EditScript and constructs a new tree, that consists out of all the assigned nodes of the
 * original tree and newly loaded nodes.
 * @param self TSNode in the original tree
 * @param other TSNode in the changed tree
 * @param parent_id ID of the parent node (or NULL if ROOT)
 * @param parent_type TSSymbol of the parent node (or UINT16_MAX if ROOT)
 * @param link Childindex of the parent node
 * @param buffer Pointer to the EditScriptBuffer
 * @param subtree_pool Pointer to the SubtreePool
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 * @return Constructed Subtree
 */
Subtree compute_edit_script(TSNode self, TSNode other, void *parent_id, TSSymbol parent_type, uint32_t link,
                            EditScriptBuffer *buffer, SubtreePool *subtree_pool, const char *self_code,
                            const char *other_code,
                            const TSLiteralMap *literal_map) {
  Subtree *this_subtree = (Subtree *) self.id;
  TSDiffHeap *this_diff_heap = ts_subtree_node_diff_heap(*this_subtree);
  const TSDiffHeap *other_diff_heap = other.diff_heap;
  Subtree *assigned_to_this = this_diff_heap->assigned;
  if (this_diff_heap->assigned != NULL && ts_subtree_node_diff_heap(*assigned_to_this)->id == other_diff_heap->id) {
    // self == other
    update_literals_iter(self, other, buffer, self_code, other_code, literal_map);
    this_diff_heap->assigned = NULL;
    ts_subtree_retain(*this_subtree); // increment subtree reference counter since we reuse this subtree
    return *this_subtree;
  } else if (this_diff_heap->assigned == NULL && other_diff_heap->assigned == NULL) {
    // No match -> recurse into
    Subtree rec_gen_subtree = compute_edit_script_recurse(self, other, buffer, subtree_pool, self_code, other_code,
                                                          literal_map);
    if (rec_gen_subtree.ptr != NULL) {
      return rec_gen_subtree;
    }
  }
  // This subtree does not match with the changed subtree at the same position -> create DetachEdit
  Detach detach_data = {
    .id=this_diff_heap->id,
    .tag=ts_subtree_symbol(*this_subtree),
    .link=link,
    .parent_id=parent_id,
    .parent_tag=parent_type
  };
  ts_edit_script_buffer_add(buffer,
                            (SugaredEdit) {
                              .edit_tag=DETACH,
                              .detach=detach_data
                            });
  unload_unassigned(self, buffer);  // unload all unassigned subtrees (in the original tree)
  Subtree new_subtree = load_unassigned(other, buffer, self_code, other_code, literal_map, self.tree,
                                        subtree_pool); // load all unassigned subtrees (in the changed tree)
  TSDiffHeap *new_subtree_diff_heap = ts_subtree_node_diff_heap(new_subtree);
  // Attach new subtree
  Attach attach_data = {
    .id=new_subtree_diff_heap->id,
    .tag=ts_subtree_symbol(new_subtree),
    .link=link,
    .parent_tag=parent_type,
    .parent_id=parent_id
  };
  ts_edit_script_buffer_add(buffer,
                            (SugaredEdit) {
                              .edit_tag=ATTACH,
                              .attach=attach_data
                            });
  return new_subtree;
}

/**
 * Diffs two TSTrees, computed an EditScript and constructs a new TSTree
 * @param this_tree Pointer to the original TSTree
 * @param that_tree Pointer to the changed TSTree
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 * @return TSDiffResult
 */
TSDiffResult
ts_compare_to(const TSTree *this_tree, const TSTree *that_tree, const char *self_code, const char *other_code,
              const TSLiteralMap *literal_map) {
  TSNode self = ts_tree_root_node(this_tree);
  TSNode other = ts_tree_root_node(that_tree);
  SubtreeRegistry *registry = ts_subtree_registry_create(); // create new SubtreeRegistry
  assign_shares(self, other, registry); // STEP 2: Assign shares
  assign_subtrees(other, registry); // STEP 3: Assign subtrees
  EditScriptBuffer edit_script_buffer = ts_edit_script_buffer_create(); // create new EditScriptBuffer
  SubtreePool subtree_pool = ts_subtree_pool_new(32); // create new SubtreePool
  // STEP 4: Compute EditScript and construct new Subtree
  Subtree computed_subtree = compute_edit_script(self, other, NULL, UINT16_MAX, 0, &edit_script_buffer, &subtree_pool,
                                                 self_code, other_code, literal_map);
  EditScript *edit_script = ts_edit_script_buffer_finalize(
    &edit_script_buffer); // Convert EditScriptBuffer to EditScript
  // Construct new tree
  TSTree *result = ts_tree_new(
    computed_subtree,
    this_tree->language,
    that_tree->included_ranges, // TODO: Calculate values
    that_tree->included_range_count
  );
  bool success = ts_subtree_eq(*(Subtree *) other.id, computed_subtree) == 0; // test equality
  // Cleanup
  ts_subtree_registry_clean_delete(registry);
  ts_subtree_pool_delete(&subtree_pool);
  return (TSDiffResult) {.constructed_tree=result, .edit_script=edit_script, .success=success};
}

/**
 * Diffs two TSTrees, computed an EditScript and constructs a new TSTree
 * @param this_tree Pointer to the original TSTree
 * @param that_tree Pointer to the changed TSTree
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 * @param graph_file Filepointer to the desired output file
 * @return TSDiffResult
 */
TSDiffResult ts_compare_to_print_graph(const TSTree *this_tree, const TSTree *that_tree, const char *self_code,
                                       const char *other_code,
                                       const TSLiteralMap *literal_map, FILE *graph_file) {
  TSNode self = ts_tree_root_node(this_tree);
  TSNode other = ts_tree_root_node(that_tree);
  SubtreeRegistry *registry = ts_subtree_registry_create(); // create new SubtreeRegistry
  assign_shares(self, other, registry); // STEP 2: Assign shares
  assign_subtrees(other, registry); // STEP 3: Assign subtrees
  ts_tree_diff_graph(self, other, this_tree->language, graph_file); // generate AssignmentGraph
  EditScriptBuffer edit_script_buffer = ts_edit_script_buffer_create(); // create new EditScriptBuffer
  SubtreePool subtree_pool = ts_subtree_pool_new(32); // create new SubtreePool
  // STEP 4: Compute EditScript and construct new Subtree
  Subtree computed_subtree = compute_edit_script(self, other, NULL, UINT16_MAX, 0, &edit_script_buffer, &subtree_pool,
                                                 self_code, other_code, literal_map);
  EditScript *edit_script = ts_edit_script_buffer_finalize(
    &edit_script_buffer); // Convert EditScriptBuffer to EditScript
  // Construct new tree
  TSTree *result = ts_tree_new(
    computed_subtree,
    this_tree->language,
    that_tree->included_ranges, // TODO: Calculate values
    that_tree->included_range_count
  );
  bool success = ts_subtree_eq(*(Subtree *) other.id, computed_subtree) == 0; // test equality
  // Cleanup
  ts_subtree_registry_clean_delete(registry);
  ts_subtree_pool_delete(&subtree_pool);
  return (TSDiffResult) {.constructed_tree=result, .edit_script=edit_script, .success=success};
}

/**
 * Checks every important attribute of two subtrees starting at the given nodes.
 *
 * @param n1 Node in the original edited tree
 * @param n2 Node in the reconstructed tree
 * @return true if something is wrong, false otherwise
 */
bool ts_reconstruction_test(const TSNode n1, const TSNode n2) {
  const TSDiffHeap *d1 = n1.diff_heap;
  const TSDiffHeap *d2 = n2.diff_heap;
  const Subtree *s1 = (Subtree *) n1.id;
  const Subtree *s2 = (Subtree *) n2.id;
  bool error = false;
  if (ts_real_node_child_count(n1) != ts_real_node_child_count(n2)) {
    printf("[%p | %p] Real node child count mismatch\n", d1->id, d2->id);
    error = true;
  }
  if (!length_equal(ts_subtree_padding(*s1), ts_subtree_padding(*s2))) {
    printf("[%p | %p] Padding mismatch %d != %d\n", d1->id, d2->id, ts_subtree_padding(*s1).bytes,
           ts_subtree_padding(*s2).bytes);
    error = true;
  }
  if (!length_equal(ts_subtree_size(*s1), ts_subtree_size(*s2))) {
    printf("[%p | %p] Size mismatch %d != %d\n", d1->id, d2->id, ts_subtree_size(*s1).bytes,
           ts_subtree_size(*s2).bytes);
    error = true;
  }
  if (!length_equal(ts_subtree_total_size(*s1), ts_subtree_total_size(*s2))) {
    printf("[%p | %p] Total size mismatch %d != %d\n", d1->id, d2->id, ts_subtree_total_size(*s1).bytes,
           ts_subtree_total_size(*s2).bytes);
    error = true;
  }
  if (ts_subtree_symbol(*s1) != ts_subtree_symbol(*s2)) {
    printf("[%p | %p] Symbol mismatch %d != %d\n", d1->id, d2->id, ts_subtree_symbol(*s1), ts_subtree_symbol(*s2));
    error = true;
  }
  if (ts_subtree_production_id(*s1) != ts_subtree_production_id(*s2)) {
    printf("[%p | %p] SubtreeProductionID mismatch\n", d1->id, d2->id);
    error = true;
  }
  if (!length_equal(d1->position, d2->position)) {
    printf("[%p | %p] DiffHeap Position mismatch\n", d1->id, d2->id);
    error = true;
  }
  if (!length_equal(d1->size, d2->size)) {
    printf("[%p | %p] DiffHeap Size mismatch\n", d1->id, d2->id);
    error = true;
  }
  if (!length_equal(d1->padding, d2->padding)) {
    printf("[%p | %p] DiffHeap Padding mismatch %d != %d\n", d1->id, d2->id, d1->padding.bytes, d2->padding.bytes);
    error = true;
  }
  if (d1->treeheight != d2->treeheight) {
    printf("[%p | %p] Treeheight mismatch %d != %d\n", d1->id, d2->id, d1->treeheight, d2->treeheight);
    error = true;
  }
  if (d1->treesize != d2->treesize) {
    printf("[%p | %p] Treesize mismatch %d != %d\n", d1->id, d2->id, d1->treesize, d2->treesize);
    error = true;
  }
  if (!ts_diff_heap_hash_eq(d1->structural_hash, d2->structural_hash)) {
    printf("[%p | %p] Structural hash mismatch\n", d1->id, d2->id);
    error = true;
  }
  if (!ts_diff_heap_hash_eq(d1->literal_hash, d2->literal_hash)) {
    printf("[%p | %p] Literal hash mismatch\n", d1->id, d2->id);
    error = true;
  }
  /*if (d1->assigned != NULL) {
    printf("[%p] Assigned not reset\n", d1->id);
    error = true;
  }*/
  if (d2->assigned != NULL) {
    printf("[%p] Assigned not reset\n", d2->id);
    error = true;
  }
  if (d1->share != NULL) {
    printf("[%p] Share not reset\n", d1->id);
    error = true;
  }
  if (d1->share != NULL) {
    printf("[%p] Share not reset\n", d2->id);
    error = true;
  }
  for (uint32_t i = 0; i < ts_real_node_child_count(n1); i++) {
    TSNode kid1 = ts_real_node_child(n1, i);
    TSNode kid2 = ts_real_node_child(n2, i);
    error = ts_reconstruction_test(kid1, kid2) || error;
  }
  return error;
}
