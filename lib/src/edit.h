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

typedef struct { // Attach and Detach
    uint32_t link;
    void *parent_id;
    TSSymbol parent_tag;
} EditDataBasic;

typedef struct { // Update
    Length old_start;
    Length old_size;
    Length new_start;
    Length new_size;
} EditDataUpdate;

typedef struct {
    Length padding;
    Length size;
    uint32_t lookahead_bytes;
    TSStateId parse_state;
    bool has_external_tokens: 1;
    bool depends_on_column: 1;
    bool is_keyword: 1;
} EditLeafData;

typedef struct {
    ChildPrototypeArray kids;
    uint16_t production_id;
} EditNodeData;

typedef struct { // Load
    bool is_leaf: 1;
    TSSymbol tag;
    union {
        EditLeafData leaf;
        EditNodeData node;
    };
} EditDataLoading;

typedef struct { // Load_Attach
    bool is_leaf: 1;
    uint32_t link;
    void *parent_id;
    TSSymbol parent_tag;
    TSSymbol tag;
    union {
        EditLeafData leaf;
        EditNodeData node;
    };
} EditDataAdvanced;

typedef struct {
    EditType type;
    Subtree *subtree;
    void *id;
    union {
        EditDataBasic basic;
        EditDataUpdate update;
        EditDataLoading loading;
        EditDataAdvanced advanced;
    };
} Edit;

typedef Array(Edit) EditArray;

#endif //TREE_SITTER_EDIT_H
