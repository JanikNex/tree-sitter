#ifndef TREE_SITTER_SUBTREE_REGISTRY_H
#define TREE_SITTER_SUBTREE_REGISTRY_H

#include "rax/rax.h"
#include "tree_sitter/api.h"
#include "subtree.h"

typedef struct {
    rax *subtrees;
} SubtreeRegistry;

typedef struct {
    struct hashmap_s *available_trees;
    rax *_preferred_trees;
} SubtreeShare;

SubtreeRegistry *ts_subtree_registry_create();

void ts_subtree_registry_delete(SubtreeRegistry *self);

SubtreeShare *ts_subtree_registry_assign_share(const SubtreeRegistry *self, Subtree *subtree);

SubtreeShare *ts_subtree_registry_assign_share_and_register_tree(const SubtreeRegistry *self, Subtree *subtree);

#endif //TREE_SITTER_SUBTREE_REGISTRY_H