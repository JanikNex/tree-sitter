#ifndef TREE_SITTER_EDIT_H
#define TREE_SITTER_EDIT_H

#include "subtree.h"

typedef struct {
    void *child_id;
} ChildPrototype;

typedef Array(ChildPrototype) ChildPrototypeArray;

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

typedef enum {
    CORE_ATTACH, CORE_DETACH, CORE_UNLOAD, CORE_LOAD, CORE_UPDATE
} CoreEditTag;

typedef enum {
    ATTACH, DETACH, UNLOAD, LOAD, LOAD_ATTACH, DETACH_UNLOAD, UPDATE
} EditTag;

typedef struct {
    void *id;
    uint32_t link;
    void *parent_id;
    TSSymbol parent_tag;
} Attach;

typedef struct {
    Subtree *subtree;
    void *id;
    uint32_t link;
    void *parent_id;
    TSSymbol parent_tag;
} Detach;

typedef struct {
    TSSymbol tag;
    Subtree *subtree;
    void *id;
} Unload;

typedef struct {
    bool is_leaf: 1;
    TSSymbol tag;
    void *id;
    union {
        EditLeafData leaf;
        EditNodeData node;
    };
} Load;

typedef struct {
    Subtree *subtree;
    void *id;
    Length old_start;
    Length old_size;
    Length old_padding;
    Length new_start;
    Length new_size;
    Length new_padding;
} Update;

typedef struct {
    bool is_leaf: 1;
    void *id;
    uint32_t link;
    void *parent_id;
    TSSymbol parent_tag;
    TSSymbol tag;
    union {
        EditLeafData leaf;
        EditNodeData node;
    };
} LoadAttach;

typedef struct {
    Subtree *subtree;
    void *id;
    uint32_t link;
    void *parent_id;
    TSSymbol parent_tag;
    TSSymbol tag;
} DetachUnload;

typedef struct {
    CoreEditTag edit_tag;
    union {
        Attach attach;
        Detach detach;
        Unload unload;
        Load load;
        Update update;
    };
} CoreEdit;

typedef struct {
    EditTag edit_tag;
    union {
        Attach attach;
        Detach detach;
        Unload unload;
        Load load;
        Update update;
        LoadAttach load_attach;
        DetachUnload detach_unload;
    };
} Edit;

typedef Array(Edit) EditArray;
typedef Array(CoreEdit) CoreEditArray;

#endif //TREE_SITTER_EDIT_H
