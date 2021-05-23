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
            bool is_leaf: 1;
            TSSymbol tag;
            union {
                struct {
                    Length padding;
                    Length size;
                    uint32_t lookahead_bytes;
                    TSStateId parse_state;
                    bool has_external_tokens: 1;
                    bool depends_on_column: 1;
                    bool is_keyword: 1;
                } leaf;
                struct {
                    ChildPrototypeArray kids;
                    uint16_t production_id;
                } node;
            };
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
