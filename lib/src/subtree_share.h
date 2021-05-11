#ifndef TREE_SITTER_SUBTREE_SHARE_H
#define TREE_SITTER_SUBTREE_SHARE_H

#include "rax/rax.h"
#include "subtree_registry.h"
#include "subtree.h"
#include "hashmap/hashmap.h"
#include "tree_sitter/api.h"

SubtreeShare *ts_subtree_share_create();

void ts_subtree_share_delete(SubtreeShare *self);

void ts_subtree_share_register_available_tree(const SubtreeShare *self, Subtree *subtree);

rax *ts_subtree_share_preferred_trees(SubtreeShare *self);

Subtree *
ts_subtree_share_take_available_tree(SubtreeShare *self, TSNode node, bool preferred, SubtreeRegistry *registry);

void ts_subtree_share_deregister_available_tree(TSNode node, SubtreeRegistry *registry);


#endif //TREE_SITTER_SUBTREE_SHARE_H
