#include "sha_digest/sha256.h"
#include "diff_heap.h"
#include "tree.h"
#include "subtree_share.h"
#include "pqueue.h"
#include "edit_script_buffer.h"

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

static bool is_signature_equal(TSNode this_node, TSNode that_node) {
  uint32_t this_child_count = ts_node_child_count(this_node);
  uint32_t that_child_count = ts_node_child_count(that_node);
  if (ts_node_symbol(this_node) != ts_node_symbol(that_node)) return false;
  if (this_child_count != that_child_count) return false;
  if (this_child_count > 0) {
    bool field_eq = true;
    TSTreeCursor this_cursor = ts_tree_cursor_new(this_node);
    ts_tree_cursor_goto_first_child(&this_cursor);
    TSTreeCursor that_cursor = ts_tree_cursor_new(that_node);
    ts_tree_cursor_goto_first_child(&that_cursor);
    do {
      TSNode this_kid = ts_tree_cursor_current_node(&this_cursor);
      TSNode that_kid = ts_tree_cursor_current_node(&that_cursor);
      if (ts_node_is_named(this_kid) != ts_node_is_named(that_kid) || ts_tree_cursor_current_field_id(&this_cursor) !=
                                                                      ts_tree_cursor_current_field_id(&that_cursor)) {
        field_eq = false;
        break;
      }
    } while (ts_tree_cursor_goto_next_sibling(&this_cursor) && ts_tree_cursor_goto_next_sibling(&that_cursor));
    ts_tree_cursor_delete(&this_cursor);
    ts_tree_cursor_delete(&that_cursor);
    return field_eq;
  }
  return true;
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
    if (is_signature_equal(this_node, that_node)) {
      uint32_t this_child_count = ts_node_child_count(this_node);
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

static inline LiteralEdit
update_literals(TSNode self, TSNode other, EditScriptBuffer *buffer, const char *self_code, const char *other_code,
                const TSLiteralMap *literal_map, const LiteralEdit prev_edit) {
  if (ts_literal_map_is_literal(literal_map, ts_node_symbol(self)) &&
      ts_literal_map_is_literal(literal_map, ts_node_symbol(other))) {
    size_t self_literal_len = ts_node_end_byte(self) - ts_node_start_byte(self);
    size_t other_literal_len = ts_node_end_byte(other) - ts_node_start_byte(other);
    if (self_literal_len != other_literal_len || (0 != memcmp(((self_code) + ts_node_start_byte(self)),
                                                              ((other_code) + ts_node_start_byte(other)),
                                                              self_literal_len))) {
      Subtree *self_subtree = (Subtree *) self.id;
      Subtree *other_subtree = (Subtree *) other.id;
      Length old_start = {.bytes=ts_node_start_byte(self), .extent=ts_node_start_point(self)};
      Length old_size = ts_subtree_size(*self_subtree);
      Length new_start = {.bytes=ts_node_start_byte(other), .extent=ts_node_start_point(other)};
      Length new_size = ts_subtree_size(*other_subtree);
      ts_edit_script_buffer_add(buffer,
                                (Edit) {
                                  .type=UPDATE,
                                  .subtree=(Subtree *) self.id,
                                  .id=self.diff_heap->id,
                                  .update={
                                    .old_start=old_start,
                                    .old_size=old_size,
                                    .new_start=new_start,
                                    .new_size=new_size
                                  }
                                });
      Length padding_change = length_add(prev_edit.padding, length_sub(ts_subtree_padding(*other_subtree),
                                                                       ts_subtree_padding(*self_subtree)));
      Length size_change = length_add(prev_edit.size,
                                      length_sub(ts_subtree_size(*other_subtree), ts_subtree_size(*self_subtree)));
      MutableSubtree mut_subtree = ts_subtree_to_mut_unsafe(*self_subtree);
      if (self_subtree->data.is_inline) {
        mut_subtree.data.padding_bytes += padding_change.bytes;
        mut_subtree.data.padding_rows += padding_change.extent.row;
        mut_subtree.data.padding_columns += padding_change.extent.column;
        mut_subtree.data.size_bytes += size_change.bytes;
      } else {
        mut_subtree.ptr->padding = length_add(ts_subtree_padding(*self_subtree), padding_change);
        mut_subtree.ptr->size = length_add(ts_subtree_size(*self_subtree), size_change);
      }
      *self_subtree = ts_subtree_from_mut(mut_subtree);
    }
  }
  return (LiteralEdit) {.size=length_zero(), .padding=length_zero()}; //TODO: Return correct change
}

static void
update_literals_iter(TSNode self, TSNode other, EditScriptBuffer *buffer, const char *self_code, const char *other_code,
                     const TSLiteralMap *literal_map) {
  LiteralEdit prev_edits = (LiteralEdit) {.size=length_zero(), .padding=length_zero()};
  TSTreeCursor self_cursor = ts_tree_cursor_new(self);
  TSTreeCursor other_cursor = ts_tree_cursor_new(other);
  int lvl = 0;
  TSNode self_child;
  TSNode other_child;
  do { //TODO: Use correct changed offsets
    self_child = ts_tree_cursor_current_node(&self_cursor);
    other_child = ts_tree_cursor_current_node(&other_cursor);
    update_literals(self_child, other_child, buffer, self_code, other_code, literal_map, prev_edits);
    while (ts_tree_cursor_goto_first_child(&self_cursor) && ts_tree_cursor_goto_first_child(&other_cursor)) {
      lvl++;
      self_child = ts_tree_cursor_current_node(&self_cursor);
      other_child = ts_tree_cursor_current_node(&other_cursor);
      update_literals(self_child, other_child, buffer, self_code, other_code, literal_map, prev_edits);
    }
    while (!(ts_tree_cursor_goto_next_sibling(&self_cursor) && ts_tree_cursor_goto_next_sibling(&other_cursor)) &&
           lvl > 0) {
      lvl--;
      ts_tree_cursor_goto_parent(&self_cursor);
      ts_tree_cursor_goto_parent(&other_cursor);
    }
  } while (lvl > 0);
  ts_tree_cursor_delete(&self_cursor);
  ts_tree_cursor_delete(&other_cursor);

}

Subtree compute_edit_script_recurse(TSNode self, TSNode other, EditScriptBuffer *buffer, SubtreePool *subtree_pool,
                                    const char *self_code,
                                    const char *other_code,
                                    const TSLiteralMap *literal_map) {
  if (is_signature_equal(self, other)) {
    Subtree *other_subtree = (Subtree *) other.id;
    const TSDiffHeap *this_diff_heap = self.diff_heap;
    SubtreeArray subtree_array = array_new();
    Length node_position = {.bytes=other.context[0], .extent={.row=other.context[1], .column=other.context[2]}};
    TSDiffHeap *new_node_diff_heap = ts_diff_heap_new_with_id(node_position, this_diff_heap->id);
    SHA256_Context structural_context;
    SHA256_Context literal_context;
    ts_diff_heap_hash_init(&structural_context, &literal_context, &other, literal_map, other_code);
    for (uint32_t i = 0; i < ts_node_child_count(self); i++) {
      TSNode this_kid = ts_node_child(self, i);
      TSNode that_kid = ts_node_child(other, i);
      Subtree kid_subtree = compute_edit_script(this_kid, that_kid, this_diff_heap->id, ts_node_symbol(self), i, buffer,
                                                subtree_pool,
                                                self_code, other_code, literal_map);
      ts_diff_heap_hash_child(&structural_context, &literal_context, ts_subtree_node_diff_heap(kid_subtree));
      array_push(&subtree_array, kid_subtree);
    }
    ts_diff_heap_hash_finalize(&structural_context, &literal_context, new_node_diff_heap);
    MutableSubtree mut_node = ts_subtree_new_node(ts_node_symbol(other), &subtree_array,
                                                  ts_subtree_production_id(*other_subtree), self.tree->language);
    ts_subtree_assign_node_diff_heap(&mut_node, new_node_diff_heap);
    Subtree new_node = ts_subtree_from_mut(mut_node);
    return new_node;
  } else {
    return NULL_SUBTREE;
  }
}

void unload_unassigned(TSNode self, EditScriptBuffer *buffer) {
  Subtree *self_subtree = (Subtree *) self.id;
  TSDiffHeap *this_diff_heap = ts_subtree_node_diff_heap(*self_subtree);
  if (this_diff_heap->assigned != NULL) {
    //this_diff_heap->assigned = NULL; // TODO: Why should this be set to NULL?
  } else {
    ts_edit_script_buffer_add(buffer,
                              (Edit) {
                                .type=UNLOAD,
                                .subtree=self_subtree,
                                .id=this_diff_heap->id,
                                .loading={
                                  .tag=ts_node_symbol(self)
                                }
                              }); //TODO: Insert correct Subtree
    for (uint32_t i = 0; i < ts_node_child_count(self); i++) {
      TSNode child = ts_node_child(self, i);
      unload_unassigned(child, buffer);
    }
  }
}

static Subtree load_unassigned(TSNode other, EditScriptBuffer *buffer, const char *self_code, const char *other_code,
                               const TSLiteralMap *literal_map, const TSTree *self_tree, SubtreePool *subtree_pool) {
  const TSDiffHeap *other_diff_heap = other.diff_heap;
  Subtree *other_subtree = (Subtree *) other.id;
  if (other_diff_heap->assigned != NULL) {
    const Subtree *assigned_subtree = other_diff_heap->assigned;
    TSNode assigned_node = ts_diff_heap_node(assigned_subtree, self_tree);
    update_literals_iter(assigned_node, other, buffer, self_code, other_code, literal_map);
    return *assigned_subtree;
  }
  void *new_id = generate_new_id();
  Length node_position = {.bytes=other.context[0], .extent={.row=other.context[1], .column=other.context[2]}};
  TSDiffHeap *new_node_diff_heap = ts_diff_heap_new_with_id(node_position, new_id);
  SHA256_Context structural_context;
  SHA256_Context literal_context;
  ts_diff_heap_hash_init(&structural_context, &literal_context, &other, literal_map, other_code);
  if (ts_node_child_count(other) > 0) {
    SubtreeArray kids = array_new();
    ChildPrototypeArray child_prototypes = array_new();
    for (uint32_t i = 0; i < ts_node_child_count(other); i++) {
      TSNode other_kid = ts_node_child(other, i);
      Subtree kid_subtree = load_unassigned(other_kid, buffer, self_code, other_code, literal_map, self_tree,
                                            subtree_pool);
      TSDiffHeap *kid_diff_heap = ts_subtree_node_diff_heap(kid_subtree);
      array_push(&child_prototypes, (ChildPrototype) {.child_id=kid_diff_heap->id});
      ts_diff_heap_hash_child(&structural_context, &literal_context, kid_diff_heap);
      array_push(&kids, kid_subtree);
    }
    ts_diff_heap_hash_finalize(&structural_context, &literal_context, new_node_diff_heap);
    MutableSubtree mut_node = ts_subtree_new_node(ts_node_symbol(other), &kids,
                                                  ts_subtree_production_id(*other_subtree), self_tree->language);
    ts_subtree_assign_node_diff_heap(&mut_node, new_node_diff_heap);
    Subtree new_node = ts_subtree_from_mut(mut_node);
    ts_edit_script_buffer_add(buffer,
                              (Edit) {.type=LOAD, .subtree=NULL, .id=new_id,
                                .loading={
                                  .is_leaf=false,
                                  .tag=ts_node_symbol(other),
                                  .node={
                                    .kids=child_prototypes,
                                    .production_id = ts_subtree_production_id(*other_subtree)
                                  }
                                }
                              }); //TODO: Do we have a subtree?
    return new_node;
  } else {
    ts_edit_script_buffer_add(buffer,
                              (Edit) {.type=LOAD, .subtree=NULL, .id=new_id,
                                .loading={
                                  .is_leaf=true,
                                  .tag=ts_node_symbol(other),
                                  .leaf={
                                    .padding=ts_subtree_padding(*other_subtree),
                                    .size=ts_subtree_size(*other_subtree),
                                    .lookahead_bytes=ts_subtree_lookahead_bytes(*other_subtree),
                                    .parse_state=ts_subtree_parse_state(*other_subtree),
                                    .has_external_tokens=ts_subtree_has_external_tokens(*other_subtree),
                                    .depends_on_column=ts_subtree_depends_on_column(*other_subtree),
                                    .is_keyword=ts_subtree_is_keyword(*other_subtree)
                                  }
                                }
                              });
    Subtree new_leaf = ts_subtree_new_leaf(subtree_pool, ts_node_symbol(other), ts_subtree_padding(*other_subtree),
                                           ts_subtree_size(*other_subtree),
                                           ts_subtree_lookahead_bytes(*other_subtree),
                                           ts_subtree_parse_state(*other_subtree),
                                           ts_subtree_has_external_tokens(*other_subtree),
                                           ts_subtree_depends_on_column(*other_subtree),
                                           ts_subtree_is_keyword(*other_subtree), self_tree->language);
    ts_diff_heap_hash_finalize(&structural_context, &literal_context, new_node_diff_heap);
    MutableSubtree mut_leaf = ts_subtree_to_mut_unsafe(new_leaf);
    ts_subtree_assign_node_diff_heap(&mut_leaf, new_node_diff_heap);
    new_leaf = ts_subtree_from_mut(mut_leaf);
    return new_leaf;
  }
}

Subtree compute_edit_script(TSNode self, TSNode other, void *parent_id, TSSymbol parent_type, uint32_t link,
                            EditScriptBuffer *buffer, SubtreePool *subtree_pool, const char *self_code,
                            const char *other_code,
                            const TSLiteralMap *literal_map) {
  const TSDiffHeap *this_diff_heap = self.diff_heap;
  const TSDiffHeap *other_diff_heap = other.diff_heap;
  Subtree *this_subtree = (Subtree *) self.id;
  Subtree *assigned_to_this = this_diff_heap->assigned;
  if (this_diff_heap->assigned != NULL && ts_subtree_node_diff_heap(*assigned_to_this)->id == other_diff_heap->id) {
    // self == other
    update_literals_iter(self, other, buffer, self_code, other_code, literal_map);
    return *this_subtree; //TODO: Insert updated subtree
  } else if (this_diff_heap->assigned == NULL && other_diff_heap->assigned == NULL) {
    // No match -> recurse into
    Subtree rec_gen_subtree = compute_edit_script_recurse(self, other, buffer, subtree_pool, self_code, other_code,
                                                          literal_map);
    if (rec_gen_subtree.ptr != NULL) {
      return rec_gen_subtree;
    }
  }
  ts_edit_script_buffer_add(buffer,
                            (Edit) {
                              .type=DETACH,
                              .subtree=(Subtree *) self.id,
                              .id=this_diff_heap->id,
                              .basic={
                                .link=link,
                                .parent_id=parent_id,
                                .parent_tag=parent_type
                              }
                            });
  unload_unassigned(self, buffer);
  Subtree new_subtree = load_unassigned(other, buffer, self_code, other_code, literal_map, self.tree, subtree_pool);
  TSDiffHeap *new_subtree_diff_heap = ts_subtree_node_diff_heap(new_subtree);
  ts_edit_script_buffer_add(buffer,
                            (Edit) {
                              .type=ATTACH,
                              .subtree=NULL,
                              .id=new_subtree_diff_heap->id,
                              .basic={
                                .link=link,
                                .parent_id=parent_id,
                                .parent_tag=parent_type
                              }
                            }); //TODO: Insert correct Subtree
  return new_subtree;
}


TSTree *ts_compare_to(const TSTree *this_tree, const TSTree *that_tree, const char *self_code, const char *other_code,
                      const TSLiteralMap *literal_map) { //TODO: Cleanup -> free used memory
  TSNode self = ts_tree_root_node(this_tree);
  TSNode other = ts_tree_root_node(that_tree);
  printf("Create SubtreeRegistry\n");
  SubtreeRegistry *registry = ts_subtree_registry_create();
  printf("AssignShares\n");
  assign_shares(self, other, registry);
  printf("AssignSubtrees\n");
  assign_subtrees(other, registry);
  printf("Create EditScriptBuffer\n");
  EditScriptBuffer edit_script_buffer = ts_edit_script_buffer_create();
  printf("Create SubtreePool\n");
  SubtreePool subtree_pool = ts_subtree_pool_new(32);
  printf("Fill EditScriptBuffer\n");
  Subtree computed_subtree = compute_edit_script(self, other, NULL, 0, -1, &edit_script_buffer, &subtree_pool,
                                                 self_code, other_code,
                                                 literal_map); // TODO: Look for correct initial parameters!
  printf("Finalize EditScriptBuffer\n");
  EditScript edit_script = ts_edit_script_buffer_finalize(&edit_script_buffer);
  printf("==== EDIT SCRIPT ====\n");
  print_edit_script(this_tree->language, &edit_script);
  printf("Balance Subtree\n");
  ts_subtree_balance(computed_subtree, &subtree_pool, this_tree->language);
  printf("Constructing Tree\n");
  TSTree *result = ts_tree_new(
    computed_subtree,
    this_tree->language,
    that_tree->included_ranges, // TODO: Calculate values
    that_tree->included_range_count
  );
  printf("EQUALITY CHECK: %d\n", ts_subtree_compare(*(Subtree *) other.id, computed_subtree));
  return result;
}
