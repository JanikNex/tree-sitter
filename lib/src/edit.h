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
    union {
        struct {
            void *id;
            uint32_t link;
            void *parent_id;
            TSSymbol parent_tag;
        } basic;
        struct {
            Length old_start;
            Length old_size;
            Length new_start;
            Length new_size;
        } update;
        struct {
            void *id;
            TSSymbol tag;
            ChildPrototypeArray kids;
        } loading;
        struct {
            void *id;
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
