#ifndef TREE_SITTER_EDIT_H
#define TREE_SITTER_EDIT_H

#include "subtree.h"

typedef struct {
    void *child_id;
} ChildPrototype;

typedef Array(ChildPrototype) ChildPrototypeArray;

typedef enum {
    ATTACH, DETACH, UNLOAD, LOAD, LOAD_ATTACH, DETACH_UNLOAD, UPDATE
} EditType;

typedef struct {
    EditType type;
    Subtree *subtree;
    void *id;
    union {
        struct { // Attach and Detach
            uint32_t link;
            void *parent_id;
            TSSymbol parent_tag;
        } basic;
        struct { // Update
            Length old_start;
            Length old_size;
            Length new_start;
            Length new_size;
        } update;
        struct { // Load
            bool is_leaf;
            TSSymbol tag;
            ChildPrototypeArray kids;
        } loading;
        struct { // Load_Attach
            uint32_t link;
            void *parent_id;
            TSSymbol parent_tag;
            TSSymbol tag;
            ChildPrototypeArray kids;
        } advanced;
    };
} Edit;

typedef Array(Edit) EditArray;

#endif //TREE_SITTER_EDIT_H
