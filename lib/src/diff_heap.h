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

// A heap-allocated structure to hold additional attributes for the truediff algorithm
//
// When using the truediff algorithm, every node is assigned an additional DiffHeap,
// that holds any additional data that is only needed by truediff. Thereby the size
// of a node is increased by just one byte, that can hold a pointer to a DiffHeap.
struct TSDiffHeap {
    void *id;
    const unsigned char structural_hash[SHA256_HASH_SIZE];
    const unsigned char literal_hash[SHA256_HASH_SIZE];
    unsigned int treeheight;
    unsigned int treesize;
    SubtreeShare *share;
    unsigned int skip_node;
    Subtree *assigned;
    Length position;
};

typedef struct {
    Subtree *subtree;
    bool valid;
} NodeEntry;

typedef struct {
    Subtree parent;
    const TSTree *tree;
    Length position;
    uint32_t child_index;
    uint32_t structural_child_index;
    const TSSymbol *alias_sequence;
} NodeChildIterator;

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

static inline void ts_diff_heap_free(TSDiffHeap *self) {
  if (self == NULL) {
    return;
  }
  ts_free(self->id);
  ts_free(self);
}

static inline void *generate_new_id() { // TODO: Is there a better way to generate URIs?
  return ts_malloc(1);
}

static inline TSDiffHeap *ts_diff_heap_new(Length pos) {
  TSDiffHeap *node_diff_heap = ts_malloc(sizeof(TSDiffHeap));
  node_diff_heap->id = generate_new_id();
  node_diff_heap->assigned = NULL;
  node_diff_heap->share = NULL;
  node_diff_heap->skip_node = 0;
  node_diff_heap->position = pos;
  return node_diff_heap;
}

static inline TSDiffHeap *ts_diff_heap_new_with_id(Length pos, void *id) {
  TSDiffHeap *node_diff_heap = ts_malloc(sizeof(TSDiffHeap));
  node_diff_heap->id = id;
  node_diff_heap->assigned = NULL;
  node_diff_heap->share = NULL;
  node_diff_heap->skip_node = 0;
  node_diff_heap->position = pos;
  return node_diff_heap;
}

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
  // Hash node type in structural hash
  const char *tag = ts_node_type(*node);
  if (sha256_add_bytes(structural_context, tag, strlen(tag)) != SHA_DIGEST_OK) {
    fprintf(stderr, "SHA_digest library failure at add_bytes of tag\n");
    return;
  }
  // Hash literal in literal hash if necessary
  TSSymbol symbol = ts_node_symbol(*node);
  if (ts_literal_map_is_literal(literal_map, symbol)) {
    size_t literal_len = ts_node_end_byte(*node) - ts_node_start_byte(*node);
    if (sha256_add_bytes(literal_context, ((const void *) code) + ts_node_start_byte(*node), literal_len) !=
        SHA_DIGEST_OK) {
      fprintf(stderr, "SHA_digest library failure at add_bytes of tag\n");
      return;
    }
  }
}

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

static inline Subtree *ts_diff_heap_cursor_get_subtree(const TSTreeCursor *cursor) {
  const TreeCursor *self = (const TreeCursor *) cursor;
  TreeCursorEntry *last_entry = array_back(&self->stack);
  return (Subtree *) last_entry->subtree;
}

static inline void foreach_subtree_assign_share(TSNode node, SubtreeRegistry *registry) {
  TSTreeCursor cursor = ts_tree_cursor_new(node);
  int lvl = 0;
  Subtree *subtree;
  do {
    subtree = ts_diff_heap_cursor_get_subtree(&cursor);
    ts_subtree_registry_assign_share(registry, subtree);
    while (ts_diff_tree_cursor_goto_first_child(&cursor)) {
      lvl++;
      subtree = ts_diff_heap_cursor_get_subtree(&cursor);
      ts_subtree_registry_assign_share(registry, subtree);
    }
    while (!(ts_diff_tree_cursor_goto_next_sibling(&cursor)) && lvl > 0) {
      lvl--;
      ts_diff_tree_cursor_goto_parent(&cursor);
    }
  } while (lvl > 0);
  ts_tree_cursor_delete(&cursor);
}

static inline void foreach_tree_assign_share(TSNode node, SubtreeRegistry *registry) {
  const TSDiffHeap *diff_heap = node.diff_heap;
  if (!diff_heap->skip_node) {
    Subtree *subtree = (Subtree *) node.id;
    ts_subtree_registry_assign_share(registry, subtree);
  }
  foreach_subtree_assign_share(node, registry);
};

static inline void foreach_subtree_assign_share_and_register_tree(TSNode node, SubtreeRegistry *registry) {
  TSTreeCursor cursor = ts_tree_cursor_new(node);
  int lvl = 0;
  Subtree *subtree;
  do {
    subtree = ts_diff_heap_cursor_get_subtree(&cursor);
    ts_subtree_registry_assign_share_and_register_tree(registry, subtree);
    while (ts_diff_tree_cursor_goto_first_child(&cursor)) {
      lvl++;
      subtree = ts_diff_heap_cursor_get_subtree(&cursor);
      ts_subtree_registry_assign_share_and_register_tree(registry, subtree);
    }
    while (!(ts_diff_tree_cursor_goto_next_sibling(&cursor)) && lvl > 0) {
      lvl--;
      ts_diff_tree_cursor_goto_parent(&cursor);
    }
  } while (lvl > 0);
  ts_tree_cursor_delete(&cursor);
}

static inline void foreach_tree_assign_share_and_register_tree(TSNode node, SubtreeRegistry *registry) {
  const TSDiffHeap *diff_heap = node.diff_heap;
  if (!diff_heap->skip_node) {
    Subtree *subtree = (Subtree *) node.id;
    ts_subtree_registry_assign_share_and_register_tree(registry, subtree);
  }
  foreach_subtree_assign_share_and_register_tree(node, registry);
};

static inline TSTreeCursor ts_diff_heap_cursor_create(const TSTree *tree) {
  return ts_tree_cursor_new(ts_tree_root_node(tree));
}

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
