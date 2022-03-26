#include "sha_digest/sha256.h"
#include "diff_heap.h"
#include "tree.h"
#include "subtree_share.h"
#include "pqueue.h"
#include "language.h"


/**
 * Compares two sha256 hashes
 * @param hash1 Pointer to the first hash
 * @param hash2 Pointer to the second hash
 * @return bool is equal
 */
bool ts_diff_heap_hash_eq(const unsigned char *hash1, const unsigned char *hash2) {
  return memcmp(hash1, hash2, SHA256_HASH_SIZE) == 0;
}

/**
 * Tests if the current subtree is relevant for the abstract edit script
 * @param sub Current subtree
 * @param lit_map Pointer to the TSLiteralMap
 * @return bool whether the subtree is relevant
 */
static inline bool is_relevant(Subtree sub, const TSLiteralMap *lit_map) {
  if (!ts_subtree_visible(sub)) {
    return false;
  }
  if (ts_subtree_named(sub)) {
    return true;
  }
  if (ts_literal_map_is_unnamed_token(lit_map, ts_subtree_symbol(sub))) {
    return true;
  } else {
  }
  return false;
}

/**
 * Generate a new ParentData struct based on the current subtree and its parent data
 * @param subtree Current subtree
 * @param pd ParenData of the current subtree
 * @param idx Current index
 * @param cpa Pointer to the ChildPrototypeArray
 * @param lang Pointer to the TSLanguage
 * @return new ParentData struct
 */
static inline ParentData
generate_new_pd(Subtree subtree, ParentData pd, uint32_t idx, ChildPrototypeArray *cpa, const TSLanguage *lang) {
  bool is_field = false;
  const TSFieldMapEntry *field_map;
  const TSFieldMapEntry *field_map_end;
  ts_language_field_map(
    lang,
    ts_subtree_production_id(subtree),
    &field_map, &field_map_end
  );
  TSFieldId field_id;
  // Look for the FieldID of the current index
  for (const TSFieldMapEntry *field = field_map; field < field_map_end; field++) {
    if (!field->inherited && field->child_index == idx) {
      is_field = true;
      field_id = field->field_id;
    }
  }
  if (!ts_subtree_visible(subtree)) {
    // Return the old ParentData unchanged if the current subtree is invisible
    return pd;
  } else {
    TSSymbol parent_symbol;
    uint16_t parent_production;
    if (ts_subtree_visible(subtree)) {
      parent_symbol = ts_subtree_symbol(subtree);
      parent_production = ts_subtree_production_id(subtree);
    } else {
      parent_symbol = pd.parent_symbol;
      parent_production = pd.production_id;
    }
    if (is_field) {
      return (ParentData) {
        .parent_symbol = parent_symbol,
        .production_id = parent_production,
        .parent_id = ts_subtree_node_diff_heap(subtree)->id,
        .is_field = true,
        .field_id = field_id,
        .cpa = cpa
      };
    } else {
      return (ParentData) {
        .parent_symbol = parent_symbol,
        .production_id = parent_production,
        .parent_id = ts_subtree_node_diff_heap(subtree)->id,
        .is_field = false,
        .link = idx,
        .cpa = cpa
      };
    }
  }
}

/**
 * Push a new abstract child prototype onto a ChildPrototypeArray
 * @param id ID of the subtree
 * @param pd Current ParentData
 * @param cpa Pointer to the ChildPrototypeArray
 */
static inline void push_abstract_child_prototype_(void *id, ParentData pd, ChildPrototypeArray *cpa) {
  ChildPrototype acp = {
    .child_id=id,
    .is_field=pd.is_field
  };
  if (pd.is_field) {
    acp.field_id = pd.field_id;
  } else {
    acp.link = pd.link;
  }
  array_push(cpa, acp);
}

static inline void push_abstract_child_prototype(void *id, ParentData pd) {
  push_abstract_child_prototype_(id, pd, pd.cpa);
}

/**
 * Unloading an irrelevant node ensures that all subsequent
 * relevant children are released. Traverse the subtree until you reach these children.
 * @param sub Current subtree
 * @param lit_map Pointer to the TSLiteralMap
 * @param pd Current ParentData
 * @param cleaned_link Current link
 * @param child_prototypes Pointer to the ChildPrototypeArray
 * @param lang Pointer to the TSLanguage
 */
static void unload_list(Subtree sub, const TSLiteralMap *lit_map, ParentData *pd, uint32_t cleaned_link,
                        ChildPrototypeArray *child_prototypes, const TSLanguage *lang) {
  for (uint32_t i = 0; i < ts_subtree_child_count(sub); i++) {
    Subtree child = ts_subtree_children(sub)[i];
    ParentData child_pd = child_pd = generate_new_pd(sub, *pd, i, child_prototypes, lang);
    if (is_relevant(child, lit_map)) {
      push_abstract_child_prototype_(ts_subtree_node_diff_heap(child)->id, child_pd, child_prototypes);
    } else {
      unload_list(child, lit_map, &child_pd, cleaned_link, child_prototypes, lang);
    }
  }
}

/**
 * Creates and inserts a new detach operation
 * @param sub Current subtree
 * @param buffer Pointer to the EditScriptBuffer
 * @param pd Current ParentData
 */
static inline void create_missing_detach(Subtree sub, EditScriptBuffer *buffer, ParentData pd) {
  Detach detach_data = {
    .id=ts_subtree_node_diff_heap(sub)->id,
    .tag=ts_subtree_symbol(sub),
    .is_field=pd.is_field,
    .parent_id=pd.parent_id,
    .parent_tag=pd.parent_symbol
  };
  if (pd.is_field) {
    detach_data.field_id = pd.field_id;
  } else {
    detach_data.link = pd.link;
  }
  ts_edit_script_buffer_add(buffer,
                            (SugaredEdit) {
                              .edit_tag=DETACH,
                              .detach=detach_data
                            });
}

/**
 * In case some irrelevant subtree root had been assigned but skipped, we need to push corresponding
 * detach edits for its following relevant children.
 * Therefore, recursively traverse the subtrees children until some relevant is found to push
 * the detach operation.
 *
 * @param sub Current subtree
 * @param lit_map Pointer to the TSLiteralMap
 * @param pd ParentData of the assigned subtree.
 * @param buffer Pointer to the EditScriptBuffer
 */
static void
detach_next_children(Subtree sub, const struct TSLiteralMap *lit_map, ParentData pd, EditScriptBuffer *buffer) {
  if (pd.needs_action && is_relevant(sub, lit_map)) {
    create_missing_detach(sub, buffer, pd);
  } else {
    // recursively traverse children reusing the same ParentData, since we have to create the
    // missing Detach-Operations with regards to the parent of the irrelevant root.
    for (uint32_t i = 0; i < ts_subtree_child_count(sub); i++) {
      Subtree child = ts_subtree_children(sub)[i];
      detach_next_children(child, lit_map, pd, buffer);
    }
  }
}

/**
 * If a subtree is reused, its relevant children must be added to the child list of the load operation.
 * @param reused_subtree Current subtree
 * @param pd Current ParentData
 * @param lit_map Pointer to the TSLiteralMap
 */
static inline void
load_reused(Subtree reused_subtree, ParentData pd, const TSLiteralMap *lit_map) {
  for (uint32_t i = 0; i < ts_subtree_child_count(reused_subtree); i++) {
    Subtree reused_child = ts_subtree_children(reused_subtree)[i];
    if (is_relevant(reused_child, lit_map)) {
      push_abstract_child_prototype(ts_subtree_node_diff_heap(reused_child)->id, pd);
    } else {
      load_reused(reused_child, pd, lit_map);
    }
  }
}

/**
 * If an attach should be performed on an irrelevant node, attach operations must be
 * created for its nearest relevant children instead.
 * @param sub Current subtree
 * @param reference Current subtree reference (in the target tree)
 * @param pd Current ParentData
 * @param buffer Pointer to the EditScriptBuffer
 * @param lit_map Pointer to the TSLiteralMap
 */
static inline void
attach_next_root(Subtree sub, Subtree reference, ParentData pd, EditScriptBuffer *buffer, const TSLiteralMap *lit_map) {
  for (uint32_t i = 0; i < ts_subtree_child_count(sub); i++) {
    Subtree child = ts_subtree_children(sub)[i];
    Subtree reference_child = ts_subtree_children(reference)[i];
    if (ts_subtree_node_diff_heap(reference_child)->assigned != NULL) {
      // If the reference subtree is assigned in the target tree, it was reused due to the
      // same signature. Therefore no attach is necessary and we can skip it.
      continue;
    }
    if (is_relevant(child, lit_map)) {
      Attach attach_data = {
        .id=ts_subtree_node_diff_heap(child)->id,
        .tag=ts_subtree_symbol(child),
        .is_field=pd.is_field,
        .parent_tag=pd.parent_symbol,
        .parent_id=pd.parent_id
      };
      if (pd.is_field) {
        attach_data.field_id = pd.field_id;
      } else {
        attach_data.link = pd.link;
      }
      ts_edit_script_buffer_add(buffer,
                                (SugaredEdit) {
                                  .edit_tag=ATTACH,
                                  .attach=attach_data
                                });
    } else {
      attach_next_root(child, reference_child, pd, buffer, lit_map);
    }
  }
}

/**
 * Initializes a subtree starting at the current cursor position
 * @param cursor Pointer to a TSTreeCursor
 * @param code Pointer to the corresponding source code
 * @param literal_map Pointer to the TSLiteralMap
 * @return Pointer to the newly created TSDiffHeap
 */
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
 * @param this_subtree Pointer to a subtree
 * @param that_subtree Pointer to a subtree
 * @return bool
 */
static bool is_signature_equal(Subtree *this_subtree, Subtree *that_subtree) {
  uint32_t this_child_count = ts_subtree_child_count(*this_subtree);
  uint32_t that_child_count = ts_subtree_child_count(*that_subtree);
  // Check node type
  if (ts_subtree_symbol(*this_subtree) != ts_subtree_symbol(*that_subtree)) return false;
  // Check number of children
  if (this_child_count != that_child_count) return false;
  // Check whether subtrees were created by the same production id -> same fields
  if (ts_subtree_production_id(*this_subtree) != ts_subtree_production_id(*that_subtree)) return false;
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
 * @param this_subtree Pointer to a subtree in the original tree
 * @param that_subtree Pointer to a subtree in the changed tree
 * @param registry SubtreeRegistry to keep track of all shares
 */
static void
assign_shares(Subtree *this_subtree, Subtree *that_subtree,
              SubtreeRegistry *registry) { // TODO: Possible without recursion?
  TSDiffHeap *this_diff_heap = ts_subtree_node_diff_heap(*this_subtree);
  TSDiffHeap *that_diff_heap = ts_subtree_node_diff_heap(*that_subtree);


  if (this_diff_heap->preemptive_assignment != NULL && this_diff_heap->preemptive_assignment == that_diff_heap) {
    // Both subtrees got the same share -> preemptive assignment
    assign_tree(this_subtree, that_subtree, this_diff_heap, that_diff_heap);
    return;
  }

  SubtreeShare *this_share = ts_subtree_registry_assign_share(registry, this_subtree);
  SubtreeShare *that_share = ts_subtree_registry_assign_share(registry, that_subtree);

  // Assign shares or look into the IncrementalRegistry and search for an assignment if preemptive_assigned
  if (this_share == that_share) {
    // Both subtrees got the same share -> preemptive assignment
    assign_tree(this_subtree, that_subtree, this_diff_heap, that_diff_heap);
  } else {
    // Subtrees got different shares
    if (is_signature_equal(this_subtree, that_subtree)) { // check signature
      // Signatures are equal -> recurse simultaneously
      ts_subtree_share_register_available_tree(this_share, this_subtree);
      for (uint32_t i = 0; i < ts_subtree_child_count(*this_subtree); i++) {
        Subtree *this_kid = &ts_subtree_children(*this_subtree)[i];
        Subtree *that_kid = &ts_subtree_children(*that_subtree)[i];
        assign_shares(this_kid, that_kid, registry);
      }
    } else {
      // Signatures are not equal -> recurse separately
      foreach_tree_assign_share_and_register_tree(this_subtree, registry);
      foreach_subtree_assign_share(that_subtree, registry);
    }
  }

}


/**
 * Iterates a list of subtrees look for an assignable subtree in the registry for every subtree that is
 * still unassigned. The preferred parameter indicated whether the literal or the structural hash should be used.
 * @param nodes NodeEntryArray of subtrees
 * @param preferred bool that indicated whether to use the structural or literal hash
 * @param registry SubtreeRegistry
 */
static inline void
select_available_tree(NodeEntryArray *nodes, const bool preferred, SubtreeRegistry *registry) {
  for (uint32_t i = 0; i < nodes->size; i++) { // iterate all array elements
    NodeEntry *entry = array_get(nodes, i);
    if (!entry->valid) { // TODO: sort by validity to speed up iterations
      continue; // skip if already processed
    }
    Subtree *subtree = entry->subtree;
    TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
    if (diff_heap->assigned != NULL) {
      entry->valid = false; // set invalid if assigned
    } else {
      SubtreeShare *node_share = diff_heap->share;
      assert(node_share != NULL);
      // Search for possible assignable subtree
      Subtree *available_tree = ts_subtree_share_take_available_tree(node_share, subtree, preferred, registry);
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
 * STEP 3 - Select reuse candidates
 *
 * Creates a priority queue and starts with the first node of the changed tree.
 * Pop a set of nodes with the same tree height from the queue and try to assign
 * a corresponding subtree from the registry.
 * The children of nodes that cannot be assigned are added to the queue.
 *
 * @param that_subtree Pointer to a subtree in the changed tree
 * @param registry Pointer to the SubtreeRegistry
 */
static void assign_subtrees(Subtree *that_subtree, SubtreeRegistry *registry) {
  PriorityQueue *queue = priority_queue_create(); // create queue
  priority_queue_insert(queue, that_subtree); // insert the subtree of the node
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
    select_available_tree(&next_nodes, true, registry); // try to assign using literal hash
    select_available_tree(&next_nodes, false, registry); // try to assign using structural hash
    while (next_nodes.size) {
      NodeEntry entry = array_pop(&next_nodes);
      if (entry.valid) { // Test if subtree got assigned
        // Add children of unassigned subtree to queue
        Subtree *next_node = entry.subtree;
        for (uint32_t i = 0; i < ts_subtree_child_count(*next_node); i++) {
          Subtree *child_subtree = &ts_subtree_children(*next_node)[i];
          priority_queue_insert(queue, child_subtree);
        }
      }
    }
  }
  array_delete((VoidArray *) &next_nodes);
  priority_queue_destroy(queue);
}


/**
 * Compares two nodes and performs an update of the original node if needed
 * @param self_subtree Pointer to a subtree in the original tree
 * @param other_subtree Pointer to a subtree in the changed tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param lang Pointer to the used TSLanguage
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 */
static inline void
update_literals(Subtree *self_subtree, Subtree *other_subtree, EditScriptBuffer *buffer, const TSLanguage *lang,
                const char *self_code, const char *other_code,
                const TSLiteralMap *literal_map) {
  TSSymbol self_psymbol = ts_language_public_symbol(lang, ts_subtree_symbol(*self_subtree));
  TSSymbol other_psymbol = ts_language_public_symbol(lang, ts_subtree_symbol(*other_subtree));
  assert(self_psymbol == other_psymbol);
  bool is_literal = ts_literal_map_is_literal(literal_map, self_psymbol);
  TSDiffHeap *self_diff_heap = ts_subtree_node_diff_heap(*self_subtree);
  TSDiffHeap *other_diff_heap = ts_subtree_node_diff_heap(*other_subtree);
  const Length old_size = self_diff_heap->size;
  const Length new_size = other_diff_heap->size;
  const Length self_padding = self_diff_heap->padding;
  const Length other_padding = other_diff_heap->padding;
  const Length self_position = self_diff_heap->position;
  const Length other_position = other_diff_heap->position;
  bool size_change = !length_equal(old_size, new_size);
  bool padding_change = !length_equal(self_padding, other_padding);
  bool subtree_has_changes = ts_subtree_has_changes(*self_subtree);
  if (is_literal) { // are those nodes literals
    // Perform update if the length or the content of the literal changed
    if (size_change || 0 != memcmp(((self_code) + self_position.bytes), ((other_code) + other_position.bytes),
                                   old_size.bytes)) {
      Update update_data = { // create update
        .id=self_diff_heap->id,
        .tag=ts_subtree_symbol(*self_subtree),
        .old_start=self_position,
        .old_size=old_size,
        .new_start=other_position,
        .new_size=new_size,
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
  if (is_literal || size_change || padding_change || subtree_has_changes) {
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
  // copy literal hash from the changed to reflect possible literal changes
  memcpy(self_diff_heap->literal_hash, other_diff_heap->literal_hash, SHA256_HASH_SIZE);
  self_diff_heap->position = other_position;
  self_diff_heap->padding = other_padding;
  self_diff_heap->size = new_size;
  if (self_diff_heap->preemptive_assignment !=
      NULL) { //TODO: Verhaeltniss zum entfernen von Assignments während AssignShares
    reset_preassignment(self_diff_heap);
  }
  // increment the DiffHeap reference counter since this node is reused in the constructed tree
  diff_heap_inc(self_diff_heap);
  self_diff_heap->share = NULL;
  other_diff_heap->share = NULL;
}

/**
 * Update literals for every node in the subtree
 * @param self Pointer to a subtree in the original tree
 * @param other Pointer to a subtree in the changed tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param lang Pointer to the used TSLanguage
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 */
static void update_literals_rec(Subtree *self, Subtree *other, EditScriptBuffer *buffer, const TSLanguage *lang,
                                const char *self_code, const char *other_code,
                                const TSLiteralMap *literal_map) {
  assert(ts_subtree_child_count(*self) == ts_subtree_child_count(*other));
  update_literals(self, other, buffer, lang, self_code, other_code, literal_map);
  for (uint32_t i = 0; i < ts_subtree_child_count(*self); i++) {
    Subtree *self_child = &ts_subtree_children(*self)[i];
    Subtree *other_child = &ts_subtree_children(*other)[i];
    update_literals_rec(self_child, other_child, buffer, lang, self_code, other_code, literal_map);
  }
}


/**
 * Check signature and compute the editscript for every child of the given TSNode
 * Creates a new node and assigns the constructed childnodes.
 * @param this_subtree Pointer to a subtree in the original tree
 * @param other_subtree Pointer to a subtree in the changed tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param pd ParentData of the current subtree
 * @param subtree_pool Pointer to the SubtreePool
 * @param lang Pointer to the used TSLanguage
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 * @return Constructed subtree or NULL_SUBTREE if signature does not match
 */
Subtree compute_edit_script_recurse(Subtree *this_subtree, Subtree *other_subtree, EditScriptBuffer *buffer,
                                    SubtreePool *subtree_pool, ParentData pd,
                                    const TSLanguage *lang,
                                    const char *self_code,
                                    const char *other_code,
                                    const TSLiteralMap *literal_map) {
  if (is_signature_equal(this_subtree, other_subtree)) { // check signature
    TSDiffHeap *this_diff_heap = ts_subtree_node_diff_heap(*this_subtree);
    TSDiffHeap *other_diff_heap = ts_subtree_node_diff_heap(*other_subtree);
    diff_heap_inc(this_diff_heap); // increment reference counter since we reuse a DiffHeap from the original tree
    SubtreeArray subtree_array = array_new();
    for (uint32_t i = 0; i < ts_subtree_child_count(*this_subtree); i++) {
      Subtree *this_kid = &ts_subtree_children(*this_subtree)[i];
      Subtree *other_kid = &ts_subtree_children(*other_subtree)[i];
      // compute editscript and constructed subtree of this child
      ParentData child_pd = generate_new_pd(*this_subtree, pd, i, NULL, lang);
      Subtree kid_subtree = compute_edit_script(this_kid, other_kid, buffer, subtree_pool, child_pd, lang, self_code,
                                                other_code, literal_map);
      array_push(&subtree_array, kid_subtree);
    }
    // Update DiffHeaps
    this_diff_heap->treeheight = other_diff_heap->treeheight;
    this_diff_heap->treesize = other_diff_heap->treesize;
    memcpy((void *) this_diff_heap->structural_hash, other_diff_heap->structural_hash, SHA256_HASH_SIZE);
    memcpy(this_diff_heap->literal_hash, other_diff_heap->literal_hash, SHA256_HASH_SIZE);
    this_diff_heap->position = other_diff_heap->position;
    this_diff_heap->size = other_diff_heap->size;
    this_diff_heap->padding = other_diff_heap->padding;
    this_diff_heap->assigned = NULL;
    this_diff_heap->share = NULL;
    other_diff_heap->assigned = NULL;
    other_diff_heap->share = NULL;
    if (this_diff_heap->preemptive_assignment != NULL) {
      reset_preassignment(this_diff_heap);
    }
    if (other_diff_heap->preemptive_assignment != NULL) {
      reset_preassignment(other_diff_heap);
    }
    // create new parent node
    MutableSubtree mut_node = ts_subtree_new_node(ts_subtree_symbol(*other_subtree), &subtree_array,
                                                  ts_subtree_production_id(*other_subtree), lang);
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
 * @param self_subtree Pointer to a subtree in the original tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param pd ParentData of the current subtree
 * @param lit_map Pointer to the TSLiteralMap
 * @param lang Pointer to the TSLanguage
 */
static void
unload_unassigned(Subtree *self_subtree, EditScriptBuffer *buffer, ParentData pd, const TSLiteralMap *lit_map,
                  const TSLanguage *lang) {
  TSDiffHeap *this_diff_heap = ts_subtree_node_diff_heap(*self_subtree);
  this_diff_heap->share = NULL; // reset share
  if (this_diff_heap->assigned != NULL) { // check if assigned
    this_diff_heap->assigned = NULL; // reset assignment
    if (pd.needs_action && is_relevant(*self_subtree, lit_map)) {
      // The parent node was irrelevant, which is why a detach operation to the first relevant parent
      // node is missing. If the current node is relevant, this detach is inserted.
      create_missing_detach(*self_subtree, buffer, pd);
    } else {
      detach_next_children(*self_subtree, lit_map, pd, buffer);
    }
  } else {
    // create basic unload
    ChildPrototypeArray child_prototypes = array_new(); // create array to hold the ids of all children
    if (pd.needs_action && is_relevant(*self_subtree, lit_map)) {
      // The parent node was irrelevant, which is why a detach operation to the first relevant parent
      // node is missing. If the current node is relevant, this detach is inserted.
      create_missing_detach(*self_subtree, buffer, pd);
    }
    unload_list(*self_subtree, lit_map, &pd, 0, &child_prototypes, lang);
    if (is_relevant(*self_subtree, lit_map)) {
      Unload abstract_unload = {
        .id=this_diff_heap->id,
        .tag=ts_subtree_symbol(*self_subtree),
        .kids=child_prototypes
      };
      ts_edit_script_buffer_add(buffer,
                                (SugaredEdit) {
                                  .edit_tag=UNLOAD,
                                  .unload=abstract_unload
                                }); // create unload
    }
    for (uint32_t i = 0; i < ts_subtree_child_count(*self_subtree); i++) { // recursive call
      Subtree *child = &ts_subtree_children(*self_subtree)[i];
      ParentData child_pd = generate_new_pd(*self_subtree, pd, i, &child_prototypes, lang);
      unload_unassigned(child, buffer, child_pd, lit_map, lang);
    }
  }
}

/**
 * Load all nodes that are part of the changed tree but do not exist in the original tree, recursively
 * @param other_subtree Pointer to a subtree in the changed tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 * @param self_tree Pointer to the original tree
 * @param subtree_pool Pointer to the SubtreePool
 * @param pd ParentData of the current subtree
 * @return Constructed Subtree
 */
static Subtree
load_unassigned(Subtree *other_subtree, EditScriptBuffer *buffer, const TSLanguage *lang, const char *self_code,
                const char *other_code,
                const TSLiteralMap *literal_map, SubtreePool *subtree_pool, ParentData pd) {
  TSDiffHeap *other_diff_heap = ts_subtree_node_diff_heap(*other_subtree);
  if (other_diff_heap->assigned != NULL) { // check if assigned
    // Assigned -> No LoadEdit needed -> try to update literals
    Subtree *assigned_subtree = other_diff_heap->assigned; // get the assigned Subtree in the original tree
    update_literals_rec(assigned_subtree, other_subtree, buffer, lang, self_code, other_code,
                        literal_map); // update literals
    ts_subtree_retain(*assigned_subtree); // increment reference counter of the subtree, since we reuse it
    if (pd.cpa != NULL) {
      if (is_relevant(*other_subtree, literal_map)) {
        push_abstract_child_prototype(ts_subtree_node_diff_heap(*assigned_subtree)->id, pd);
      } else {
        // Since the current subtree is not relevant, we have to create load operations for all subsequent subtrees.
        load_reused(*assigned_subtree, pd, literal_map);
      }
    }
    return *assigned_subtree;
  }
  other_diff_heap->share = NULL;
  void *new_id = generate_new_id(); // generate new ID for the new subtree
  Length node_position = other_diff_heap->position;
  Length node_padding = other_diff_heap->padding;
  Length node_size = other_diff_heap->size;
  TSDiffHeap *new_node_diff_heap = ts_diff_heap_new_with_id(node_position, node_padding, node_size,
                                                            new_id); // create new DiffHeap
  new_node_diff_heap->treeheight = other_diff_heap->treeheight;
  new_node_diff_heap->treesize = other_diff_heap->treesize;
  memcpy((void *) new_node_diff_heap->structural_hash, other_diff_heap->structural_hash, SHA256_HASH_SIZE);
  memcpy(new_node_diff_heap->literal_hash, other_diff_heap->literal_hash, SHA256_HASH_SIZE);
  Load load_data = {
    .id=new_id,
    .tag=ts_subtree_symbol(*other_subtree),
    .is_leaf=ts_subtree_child_count(*other_subtree) == 0
  };
  if (pd.cpa != NULL) {
    if (is_relevant(*other_subtree, literal_map)) {
      push_abstract_child_prototype(new_id, pd);
    }
  }
  if (ts_subtree_child_count(*other_subtree) > 0) { // test for children to decide if it's a node or a leaf
    // -> Node
    SubtreeArray kids = array_new();
    ChildPrototypeArray child_prototypes = array_new();
    ChildPrototypeArray *child_prototypes_pointer = &child_prototypes;
    for (uint32_t i = 0; i < ts_subtree_child_count(*other_subtree); i++) { // load children
      Subtree *other_kid = &ts_subtree_children(*other_subtree)[i];
      ParentData child_pd = generate_new_pd(*other_subtree, pd, i, child_prototypes_pointer, lang);
      Subtree kid_subtree = load_unassigned(other_kid, buffer, lang, self_code, other_code, literal_map, subtree_pool,
                                            child_pd);
      array_push(&kids, kid_subtree);
    }
    // Create new node
    MutableSubtree mut_node;
    if (ts_subtree_is_error(*other_subtree)) { // check for error node
      Subtree err_node = ts_subtree_new_error_node(&kids, ts_subtree_extra(*other_subtree), lang);
      mut_node = ts_subtree_to_mut_unsafe(err_node);
    } else {
      mut_node = ts_subtree_new_node(ts_subtree_symbol(*other_subtree), &kids,
                                     ts_subtree_production_id(*other_subtree), lang);
    }
    ts_subtree_assign_node_diff_heap(&mut_node, new_node_diff_heap); // assign DiffHeap to the new node
    Subtree new_node = ts_subtree_from_mut(mut_node);
    load_data.is_leaf = false;
    load_data.kids = child_prototypes;
    if (is_relevant(new_node, literal_map)) {
      ts_edit_script_buffer_add(buffer,
                                (SugaredEdit) {
                                  .edit_tag=LOAD,
                                  .load=load_data
                                });
    } else {
      array_delete(child_prototypes_pointer);
    }
    return new_node;
  } else {
    // -> Leaf
    load_data.is_leaf = true;

    // Create new leaf
    Subtree new_leaf;
    if (ts_subtree_is_error(*other_subtree)) { // check for error leaf
      int32_t lookahead_char = other_subtree->ptr->lookahead_char;
      new_leaf = ts_subtree_new_error(subtree_pool, lookahead_char, ts_subtree_padding(*other_subtree),
                                      ts_subtree_size(*other_subtree),
                                      ts_subtree_lookahead_bytes(*other_subtree),
                                      ts_subtree_parse_state(*other_subtree), lang);
    } else {
      new_leaf = ts_subtree_new_leaf(subtree_pool, ts_subtree_symbol(*other_subtree),
                                     ts_subtree_padding(*other_subtree),
                                     ts_subtree_size(*other_subtree),
                                     ts_subtree_lookahead_bytes(*other_subtree),
                                     ts_subtree_parse_state(*other_subtree),
                                     ts_subtree_has_external_tokens(*other_subtree),
                                     ts_subtree_depends_on_column(*other_subtree),
                                     ts_subtree_is_keyword(*other_subtree), lang);
    }
    MutableSubtree mut_leaf = ts_subtree_to_mut_unsafe(new_leaf);
    if (ts_subtree_has_external_tokens(*other_subtree)) {
      const ExternalScannerState *node_state = &other_subtree->ptr->external_scanner_state;
      mut_leaf.ptr->external_scanner_state = ts_external_scanner_state_copy(node_state);
    }
    ts_subtree_assign_node_diff_heap(&mut_leaf, new_node_diff_heap); // assign DiffHeap to the new leaf
    new_leaf = ts_subtree_from_mut(mut_leaf);
    // Create LoadEdit
    if (is_relevant(new_leaf, literal_map)) {
      ts_edit_script_buffer_add(buffer,
                                (SugaredEdit) {
                                  .edit_tag=LOAD,
                                  .load=load_data
                                });
    }

    return new_leaf;
  }

}

/**
 * Computes an EditScript and constructs a new tree, that consists out of all the assigned nodes of the
 * original tree and newly loaded nodes.
 * @param this_subtree Pointer to a subtree in the original tree
 * @param other_subtree Pointer to a subtree in the changed tree
 * @param buffer Pointer to the EditScriptBuffer
 * @param subtree_pool Pointer to the SubtreePool
 * @param pd ParentData of the current subtree
 * @param lang Pointer to the used TSLanguage
 * @param self_code Pointer to the original code
 * @param other_code Pointer to the changed code
 * @param literal_map Pointer to the literalmap
 * @return Constructed Subtree
 */
Subtree
compute_edit_script(Subtree *this_subtree, Subtree *other_subtree, EditScriptBuffer *buffer, SubtreePool *subtree_pool,
                    ParentData pd, const TSLanguage *lang, const char *self_code, const char *other_code,
                    const TSLiteralMap *literal_map) {
  TSDiffHeap *this_diff_heap = ts_subtree_node_diff_heap(*this_subtree);
  const TSDiffHeap *other_diff_heap = ts_subtree_node_diff_heap(*other_subtree);
  Subtree *assigned_to_this = this_diff_heap->assigned;
  if (this_diff_heap->assigned != NULL && ts_subtree_node_diff_heap(*assigned_to_this)->id == other_diff_heap->id) {
    // self == other
    update_literals_rec(this_subtree, other_subtree, buffer, lang, self_code, other_code, literal_map);
    this_diff_heap->assigned = NULL;
    ts_subtree_retain(*this_subtree); // increment subtree reference counter since we reuse this subtree
    return *this_subtree;
  } else if (this_diff_heap->assigned == NULL && other_diff_heap->assigned == NULL) {
    // No match -> recurse into
    Subtree rec_gen_subtree = compute_edit_script_recurse(this_subtree, other_subtree, buffer, subtree_pool, pd, lang,
                                                          self_code, other_code, literal_map);
    if (rec_gen_subtree.ptr != NULL) {
      return rec_gen_subtree;
    }
  }
  // This subtree does not match with the changed subtree at the same position -> create DetachEdit
  if (is_relevant(*this_subtree, literal_map)) {
    // We create a detach operation because the current subtree is relevant
    create_missing_detach(*this_subtree, buffer, pd);
  } else {
    // Since the current subtree is not relevant, we mark it in the ParentData. As a result, the
    // following subtrees create a detach operation while it unloads.
    pd.needs_action = true;
  }
  unload_unassigned(this_subtree, buffer, pd, literal_map,
                    lang);  // unload all unassigned subtrees (in the original tree)
  Subtree new_subtree = load_unassigned(other_subtree, buffer, lang, self_code, other_code, literal_map,
                                        subtree_pool, pd); // load all unassigned subtrees (in the changed tree)
  TSDiffHeap *new_subtree_diff_heap = ts_subtree_node_diff_heap(new_subtree);
  // Attach new subtree
  if (is_relevant(new_subtree, literal_map)) {
    // Create an attach operation for the current subtree
    Attach attach_data = {
      .id=new_subtree_diff_heap->id,
      .tag=ts_subtree_symbol(new_subtree),
      .is_field=pd.is_field,
      .parent_tag=pd.parent_symbol,
      .parent_id=pd.parent_id
    };
    if (pd.is_field) {
      attach_data.field_id = pd.field_id;
    } else {
      attach_data.link = pd.link;
    }
    ts_edit_script_buffer_add(buffer,
                              (SugaredEdit) {
                                .edit_tag=ATTACH,
                                .attach=attach_data
                              });
  } else {
    // Since the current subtree is irrelevant, we have to create attach operations for its named children
    attach_next_root(new_subtree, *other_subtree, pd, buffer, literal_map);
  }
  return new_subtree;
}

/**
 * Diffs two TSTrees, computes an EditScript and constructs a new TSTree
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
  Subtree *self_subtree = (Subtree *) self.id;
  Subtree *other_subtree = (Subtree *) other.id;
  SubtreeRegistry *registry = ts_subtree_registry_create(); // create new SubtreeRegistry
  assign_shares(self_subtree, other_subtree, registry); // STEP 2: Assign shares
  assign_subtrees(other_subtree, registry); // STEP 3: Assign subtrees
  EditScriptBuffer edit_script_buffer = ts_edit_script_buffer_create(); // create new EditScriptBuffer
  SubtreePool subtree_pool = ts_subtree_pool_new(32); // create new SubtreePool
  // STEP 4: Compute EditScript and construct new Subtree
  ParentData root_data = {.parent_id = NULL, .parent_symbol = UINT16_MAX, .link = 0, .cpa=NULL};
  Subtree computed_subtree = compute_edit_script(self_subtree, other_subtree, &edit_script_buffer,
                                                 &subtree_pool, root_data, this_tree->language, self_code, other_code,
                                                 literal_map);
  EditScript *edit_script = ts_edit_script_buffer_finalize(
    &edit_script_buffer); // Convert EditScriptBuffer to EditScript
  // Construct new tree
  TSTree *result = ts_tree_new(
    computed_subtree,
    this_tree->language,
    that_tree->included_ranges, // TODO: Calculate values
    that_tree->included_range_count
  );
  bool success = ts_subtree_eq(*other_subtree, computed_subtree) == 0; // test equality
  // Cleanup
  ts_subtree_registry_clean_delete(registry);
  ts_subtree_pool_delete(&subtree_pool);
  return (TSDiffResult) {.constructed_tree=result, .edit_script=edit_script, .success=success};
}

/**
 * Diffs two TSTrees, computes an EditScript and constructs a new TSTree
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
  Subtree *self_subtree = (Subtree *) self.id;
  Subtree *other_subtree = (Subtree *) other.id;
  SubtreeRegistry *registry = ts_subtree_registry_create(); // create new SubtreeRegistry
  assign_shares(self_subtree, other_subtree, registry); // STEP 2: Assign shares
  assign_subtrees(other_subtree, registry); // STEP 3: Assign subtrees
  ts_tree_diff_graph(self, other, this_tree->language, graph_file); // generate AssignmentGraph
  EditScriptBuffer edit_script_buffer = ts_edit_script_buffer_create(); // create new EditScriptBuffer
  SubtreePool subtree_pool = ts_subtree_pool_new(32); // create new SubtreePool
  // STEP 4: Compute EditScript and construct new Subtree
  ParentData root_data = {.parent_id = NULL, .parent_symbol = UINT16_MAX, .link = 0, .cpa=NULL};
  Subtree computed_subtree = compute_edit_script(self_subtree, other_subtree, &edit_script_buffer,
                                                 &subtree_pool, root_data, this_tree->language, self_code, other_code,
                                                 literal_map);
  EditScript *edit_script = ts_edit_script_buffer_finalize(
    &edit_script_buffer); // Convert EditScriptBuffer to EditScript
  // Construct new tree
  TSTree *result = ts_tree_new(
    computed_subtree,
    this_tree->language,
    that_tree->included_ranges, // TODO: Calculate values
    that_tree->included_range_count
  );
  bool success = ts_subtree_eq(*other_subtree, computed_subtree) == 0; // test equality
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
  if (ts_subtree_child_count(*s1) != ts_subtree_child_count(*s2)) {
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
    printf("[%p | %p] SubtreeProductionID mismatch %d != %d\n", d1->id, d2->id, ts_subtree_production_id(*s1),
           ts_subtree_production_id(*s2));
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
  if (d2->share != NULL) {
    printf("[%p] Share not reset\n", d2->id);
    error = true;
  }
  /*if (d1->preemptive_assignment != NULL) {
    printf("[%p] Preemptive Assignment not reset\n", d1->id);
    error = true;
  }*/
  if (d2->preemptive_assignment != NULL) {
    printf("[%p] Preemptive Assignment not reset\n", d2->id);
    error = true;
  }
  for (uint32_t i = 0; i < ts_real_node_child_count(n1); i++) {
    TSNode kid1 = ts_real_node_child(n1, i);
    TSNode kid2 = ts_real_node_child(n2, i);
    error = ts_reconstruction_test(kid1, kid2) || error;
  }
  return error;
}

/**
 * Checks every important attribute of a non-incremental and an incremental subtree to confirm
 * a correct incremental parse.
 *
 * @param n1 Node in the non-incremental parsed tree
 * @param n2 Node in the incremental parsed tree
 * @return true if something is wrong, false otherwise
 */
bool ts_incremental_parse_test(const TSNode n1, const TSNode n2) {
  const TSDiffHeap *d1 = n1.diff_heap;
  const TSDiffHeap *d2 = n2.diff_heap;
  const Subtree *s1 = (Subtree *) n1.id;
  const Subtree *s2 = (Subtree *) n2.id;
  bool error = false;
  if (ts_subtree_child_count(*s1) != ts_subtree_child_count(*s2)) {
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
    printf("[%p | %p] SubtreeProductionID mismatch %d != %d\n", d1->id, d2->id, ts_subtree_production_id(*s1),
           ts_subtree_production_id(*s2));
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
  if (error) {
    printf("Subtree %p has error with DiffHeap %p | %p\n", s2, d2->id, d2);
  }
  for (uint32_t i = 0; i < ts_real_node_child_count(n1); i++) {
    TSNode kid1 = ts_real_node_child(n1, i);
    TSNode kid2 = ts_real_node_child(n2, i);
    error = ts_incremental_parse_test(kid1, kid2) || error;
  }
  return error;
}
