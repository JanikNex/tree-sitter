#ifndef TREE_SITTER_DIFF_HEAP_H
#define TREE_SITTER_DIFF_HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tree_sitter/api.h"
#include "sha_digest/sha256.h"
#include "subtree.h"
#include "literal_map.h"
#include "tree_cursor.h"
#include "subtree_registry.h"
#include "edit_script_buffer.h"
#include "atomic.h"

// A heap-allocated structure to hold additional attributes for the truediff algorithm
//
// When using the truediff algorithm, every node is assigned an additional DiffHeap,
// that holds any additional data that is only needed by truediff. Thereby the size
// of a node is increased by just one byte, that can hold a pointer to a DiffHeap.
struct TSDiffHeap {
    void *id;
    bool skip_node;
    bool is_preemptive_assigned;
    volatile uint32_t ref_count;
    const unsigned char structural_hash[SHA256_HASH_SIZE];
    unsigned char literal_hash[SHA256_HASH_SIZE];
    unsigned int treeheight;
    unsigned int treesize;
    SubtreeShare *share;
    union {
        void *preemptive_assignment;
        Subtree *assigned;
    };
    Length position;
    Length padding;
    Length size;
};

typedef struct {
    Subtree *subtree;
    bool valid;
} NodeEntry;

typedef Array(NodeEntry) NodeEntryArray;

TSNode ts_diff_heap_node(const Subtree *, const TSTree *);

void assign_shares(TSNode, TSNode, SubtreeRegistry *);

void assign_subtrees(TSNode, SubtreeRegistry *);

Subtree
compute_edit_script_recurse(TSNode, TSNode, EditScriptBuffer *, SubtreePool *, const char *, const char *,
                            const TSLiteralMap *);

Subtree
compute_edit_script(TSNode, TSNode, void *, TSSymbol, uint32_t, EditScriptBuffer *, SubtreePool *, const char *,
                    const char *,
                    const TSLiteralMap *);

void unload_unassigned(TSNode, EditScriptBuffer *);

void
select_available_tree(NodeEntryArray *, const TSTree *, bool, SubtreeRegistry *);

uint32_t ts_real_node_child_count(TSNode);

TSNode ts_real_node_child(TSNode, uint32_t);

/**
 * Increments the reference counter of the given diffHeap
 * @param diff_heap Pointer to the DiffHeap
 */
static inline void diff_heap_inc(TSDiffHeap *diff_heap) {
  assert(diff_heap->ref_count > 0);
  atomic_inc((volatile uint32_t *) &diff_heap->ref_count);
  assert(diff_heap->ref_count != 0);
}

/**
 * Decrements the reference counter of the given DiffHeap
 * @param diff_heap Pointer to the DiffHeap
 * @return uint32_t New reference count
 */
static inline uint32_t diff_heap_dec(TSDiffHeap *diff_heap) {
  assert(diff_heap->ref_count > 0);
  return atomic_dec((volatile uint32_t *) &diff_heap->ref_count);
}

static inline void *generate_new_id() { // TODO: Is there a better way to generate URIs?
  return ts_malloc(1);
}

/**
 * Creates a new TSDiffHeap with a random id
 * Allocates memory!
 * @param pos Length - Absolute position in the Sourcecode
 * @param padding Length - Padding of the node
 * @param size Length - Size of the node
 * @return Pointer to the new TSDiffHeap
 */
static inline TSDiffHeap *ts_diff_heap_new(Length pos, Length padding, Length size) {
  TSDiffHeap *node_diff_heap = ts_malloc(sizeof(TSDiffHeap));
  node_diff_heap->id = generate_new_id();
  node_diff_heap->is_preemptive_assigned = false;
  node_diff_heap->skip_node = false;
  node_diff_heap->assigned = NULL;
  node_diff_heap->share = NULL;
  node_diff_heap->position = pos;
  node_diff_heap->ref_count = 1;
  node_diff_heap->padding = padding;
  node_diff_heap->size = size;
  return node_diff_heap;
}

/**
 * Creates a new TSDiffHeap with a specific id
 * Allocates memory!
 * @param pos Length - Absolute position in the Sourcecode
 * @param padding Length - Padding of the node
 * @param size Length - Size of the node
 * @param id Specific id (void *)
 * @return Pointer to the new TSDiffHeap
 */
static inline TSDiffHeap *ts_diff_heap_new_with_id(Length pos, Length padding, Length size, void *id) {
  TSDiffHeap *node_diff_heap = ts_malloc(sizeof(TSDiffHeap));
  node_diff_heap->id = id;
  node_diff_heap->skip_node = false;
  node_diff_heap->is_preemptive_assigned = false;
  node_diff_heap->assigned = NULL;
  node_diff_heap->share = NULL;
  node_diff_heap->position = pos;
  node_diff_heap->ref_count = 1;
  node_diff_heap->padding = padding;
  node_diff_heap->size = size;
  return node_diff_heap;
}

/**
 * Creates a new TSDiffHeap with a new ID but reuses the remaining values of the passed TSDiffHeap.
 * This is used to create analogous TSDiffHeaps during incremental parsing that keep all calculated values
 * of the original TSDiffHeap.
 * @param diff_heap Pointer to the original TSDiffHeap
 * @return Pointer to the newly created TSDiffHeap
 */
static inline TSDiffHeap *ts_diff_heap_reuse(TSDiffHeap *diff_heap) {
  TSDiffHeap *new_diff_heap = ts_diff_heap_new(diff_heap->position, LENGTH_UNDEFINED, LENGTH_UNDEFINED);
  new_diff_heap->skip_node = diff_heap->skip_node;
  new_diff_heap->treeheight = diff_heap->treeheight;
  new_diff_heap->treesize = diff_heap->treesize;
  memcpy((void *) &new_diff_heap->structural_hash[0], diff_heap->structural_hash, SHA256_HASH_SIZE);
  memcpy(new_diff_heap->literal_hash, diff_heap->literal_hash, SHA256_HASH_SIZE);
  return new_diff_heap;
}

/**
 * Remove an assigned TSDiffHeap from the passed Subtree.
 * @param subtree Subtree with TSDiffHeap
 * @return Subtree without TSDiffHeap
 */
static inline Subtree ts_diff_heap_del(Subtree subtree) {
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(subtree);
  // Check if subtree owns DiffHeap and decrement reference counter
  if (diff_heap != NULL && diff_heap_dec(diff_heap) == 0) {
    // Subtree has DiffHeap and was the last reference
    ts_free(diff_heap->id); // free id
    ts_free(diff_heap); // free DiffHeap
    // Remove reference from subtree
    MutableSubtree mut_subtree = ts_subtree_to_mut_unsafe(subtree);
    ts_subtree_assign_node_diff_heap(&mut_subtree, NULL);
    return ts_subtree_from_mut(mut_subtree);
  }
  return subtree;
}

/**
 * Check the incremental registry of the SubtreeRegistry to see whether a counterpart
 * for preemptive assignment is already known. If so, the preemptive assignment is
 * converted into an actual assignment.
 * @param registry Pointer to the SubtreeRegistry
 * @param this_subtree Pointer to the current Subtree
 * @param this_diff_heap Pointer to the TSDiffHeap of the current Subtree
 */
static inline void
try_preemptive_assignment(SubtreeRegistry *registry, Subtree *this_subtree, TSDiffHeap *this_diff_heap) {
  Subtree *assigned_subtree = ts_subtree_registry_find_incremental_assignment(registry, this_subtree);
  if (assigned_subtree != NULL) {
    TSDiffHeap *assigned_diff_heap = ts_subtree_node_diff_heap(*assigned_subtree);
    this_diff_heap->is_preemptive_assigned = false;
    this_diff_heap->assigned = assigned_subtree;
    assigned_diff_heap->is_preemptive_assigned = false;
    assigned_diff_heap->assigned = this_subtree;
  }
}

/**
 * Prepares the contexts for structural and literal hashing. For this purpose, the contexts are first
 * initialized and then the node type for the structural and (if literal) the value of the literal
 * for the literal hash is added.
 * @param structural_context Pointer to the structural context
 * @param literal_context Pointer to the literal context
 * @param node Pointer to the current node
 * @param literal_map Pointer to the literal map
 * @param code Pointer to the beginning of the sourcecode
 */
static inline void
ts_diff_heap_hash_init(SHA256_Context *structural_context, SHA256_Context *literal_context, const TSNode *node,
                       const TSLiteralMap *literal_map, const char *code) {
  // Init contexts
  if (sha256_initialize(structural_context) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at initialize\n");
    return;
  }
  if (sha256_initialize(literal_context) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at initialize\n");
    return;
  }
  const TSSymbol symbol = ts_node_symbol(*node);
  // Hash node type in structural hash
  if (sha256_add_bytes(structural_context, &symbol, sizeof(symbol)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at add_bytes of tag\n");
    return;
  }
  // Hash literal in literal hash if necessary
  if (ts_literal_map_is_literal(literal_map, symbol)) {
    size_t literal_len = ts_node_end_byte(*node) - ts_node_start_byte(*node);
    if (sha256_add_bytes(literal_context, ((const void *) code) + ts_node_start_byte(*node), literal_len) !=
        SHA_DIGEST_OK) {
      fprintf(stderr, "SHA_digest library failure at add_bytes of tag\n");
      return;
    }
  }
}

/**
 * If the TSNode has children, their hashes must also be added to their own hash.
 * @param structural_context Pointer to the structural context
 * @param literal_context Pointer to the literal hash
 * @param child_heap Pointer to the TSDiffHeap of a child
 */
static inline void
ts_diff_heap_hash_child(SHA256_Context *structural_context, SHA256_Context *literal_context,
                        const TSDiffHeap *child_heap) {
  if (sha256_add_bytes(structural_context, child_heap->structural_hash, 32) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at add_bytes of child\n");
    return;
  }
  if (sha256_add_bytes(literal_context, child_heap->literal_hash, 32) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at add_bytes of child\n");
    return;
  }
}

/**
 * Finalize the hashing by calculating the SHA256 hashes and storing them in the TSDiffHeap.
 * @param structural_context Pointer to the structural context
 * @param literal_context Pointer to the literal context
 * @param diff_heap Pointer to the TSDiffHeap of the current node
 */
static inline void
ts_diff_heap_hash_finalize(SHA256_Context *structural_context, SHA256_Context *literal_context,
                           const TSDiffHeap *diff_heap) {
  if (sha256_calculate(structural_context, (unsigned char *) &(diff_heap->structural_hash)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at calculate\n");
    return;
  }
  if (sha256_calculate(literal_context, (unsigned char *) &(diff_heap->literal_hash)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at calculate\n");
    return;
  }
}

/**
 * Returns the subtree at the current cursor position
 * @param cursor Pointer to the TSTreeCursor
 * @return Pointer to the Subtree
 */
static inline Subtree *ts_diff_heap_cursor_get_subtree(const TSTreeCursor *cursor) {
  const TreeCursor *self = (const TreeCursor *) cursor;
  TreeCursorEntry *last_entry = array_back(&self->stack);
  return (Subtree *) last_entry->subtree;
}

/**
 * Assigns a share to each subtree (excluding the root)
 * @param node TSNode
 * @param registry Pointer to the SubtreeRegistry
 */
static inline void
foreach_subtree_assign_share(Subtree *subtree, SubtreeRegistry *registry) {
  for (uint32_t i = 0; i < ts_subtree_child_count(*subtree); i++) {
    Subtree *child = &ts_subtree_children(*subtree)[i];
    TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*child);
    if (diff_heap->is_preemptive_assigned) {
      try_preemptive_assignment(registry, child, diff_heap);
    } else {
      ts_subtree_registry_assign_share(registry, child);
      foreach_subtree_assign_share(child, registry);
    }
  }
}

/**
 * Assigns a share to each subtree (including the root)
 * @param node
 * @param registry
 */
static inline void
foreach_tree_assign_share(TSNode node, SubtreeRegistry *registry) {
  Subtree *subtree = (Subtree *) node.id;
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (diff_heap->is_preemptive_assigned) {
    try_preemptive_assignment(registry, subtree, diff_heap);
  } else {
    if (!diff_heap->skip_node) {
      ts_subtree_registry_assign_share(registry, subtree);
    }
    foreach_subtree_assign_share(subtree, registry);
  }
}

/**
 * Assigns a share to each sub-tree and registers it as an available tree (excluding the root)
 * @param node TSNode
 * @param registry Pointer to the SubtreeRegistry
 */
static inline void foreach_subtree_assign_share_and_register_tree(Subtree *subtree, SubtreeRegistry *registry) {
  for (uint32_t i = 0; i < ts_subtree_child_count(*subtree); i++) {
    Subtree *child = &ts_subtree_children(*subtree)[i];
    TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*child);
    if (diff_heap->is_preemptive_assigned) {
      try_preemptive_assignment(registry, child, diff_heap);
    } else {
      ts_subtree_registry_assign_share_and_register_tree(registry, child);
      foreach_subtree_assign_share_and_register_tree(child, registry);
    }
  }
}

/**
 * Assigns a share to each sub-tree and registers it as an available tree (including the root)
 * @param node TSNode
 * @param registry Pointer to the SubtreeRegistry
 */
static inline void
foreach_tree_assign_share_and_register_tree(TSNode node, SubtreeRegistry *registry) {
  Subtree *subtree = (Subtree *) node.id;
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  if (diff_heap->is_preemptive_assigned) {
    try_preemptive_assignment(registry, subtree, diff_heap);
  } else {
    if (!diff_heap->skip_node) {
      ts_subtree_registry_assign_share_and_register_tree(registry, subtree);
    }
    foreach_subtree_assign_share_and_register_tree(subtree, registry);
  }
}

/**
 * Creates a new TSTreeCursor starting at the root of the given tree
 * @param tree Pointer to the TSTree
 * @return New TSTreeCursor
 */
static inline TSTreeCursor ts_diff_heap_cursor_create(const TSTree *tree) {
  return ts_tree_cursor_new(ts_tree_root_node(tree));
}

/**
 * Assigns two trees to each other
 * @param this_subtree Pointer a subtree
 * @param that_subtree Pointer to another subtree
 * @param this_diff_heap TSDiffHeap of the first subtree
 * @param that_diff_heap TSDiffHeap of the second subtree
 */
static inline void
assign_tree(Subtree *this_subtree, Subtree *that_subtree, TSDiffHeap *this_diff_heap, TSDiffHeap *that_diff_heap) {
  this_diff_heap->assigned = that_subtree;
  that_diff_heap->assigned = this_subtree;
  this_diff_heap->share = NULL;
}

#ifdef __cplusplus
}
#endif

#endif //TREE_SITTER_DIFF_HEAP_H
