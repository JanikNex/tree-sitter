#ifndef TREE_SITTER_EDIT_H
#define TREE_SITTER_EDIT_H

#include "subtree.h"

typedef enum {
    ATTACH, DETACH, UNLOAD, LOAD, LOAD_ATTACH, DETACH_UNLOAD, UPDATE
} EditType;

typedef struct {
    EditType type;
    Subtree *subtree;
    union {
        struct {
            uint32_t link;
            void *parent;
            TSSymbol parent_tag;
        } basic;
        struct {
            uint32_t old_start;
            size_t old_length;
            uint32_t new_start;
            size_t new_length;
        } update;
    };
} Edit;

typedef Array(Edit) EditArray;

#endif //TREE_SITTER_EDIT_H
