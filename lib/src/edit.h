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
    union {
        ExternalScannerState external_scanner_state;
        int32_t lookahead_char;
    };
} EditLeafData;

typedef struct {
    ChildPrototypeArray kids;
    uint16_t production_id;
} EditNodeData;

typedef enum {
    CORE_ATTACH, CORE_DETACH, CORE_UNLOAD, CORE_LOAD, CORE_UPDATE, CORE_UPDATE_PADDING
} CoreEditTag;

typedef enum {
    ATTACH, DETACH, UNLOAD, LOAD, LOAD_ATTACH, DETACH_UNLOAD, UPDATE, UPDATE_PADDING
} EditTag;

typedef struct {
    void *id;
    TSSymbol tag;
    uint32_t link;
    void *parent_id;
    TSSymbol parent_tag;
} Attach;

typedef struct {
    void *id;
    TSSymbol tag;
    uint32_t link;
    void *parent_id;
    TSSymbol parent_tag;
} Detach;

typedef struct {
    TSSymbol tag;
    void *id;
    ChildPrototypeArray kids;
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
    void *id;
    TSSymbol tag;
    Length old_start;
    Length old_size;
    Length new_start;
    Length new_size;
} Update;

typedef struct{
  void *id;
  TSSymbol tag;
  Length old_padding;
  Length new_padding;
}UpdatePadding;

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
    void *id;
    uint32_t link;
    void *parent_id;
    TSSymbol parent_tag;
    TSSymbol tag;
    ChildPrototypeArray kids;
} DetachUnload;

typedef struct {
    CoreEditTag edit_tag;
    union {
        Attach attach;
        Detach detach;
        Unload unload;
        Load load;
        Update update;
        UpdatePadding update_padding;
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
        UpdatePadding update_padding;
        LoadAttach load_attach;
        DetachUnload detach_unload;
    };
} SugaredEdit;

typedef Array(SugaredEdit) EditArray;
typedef Array(CoreEdit) CoreEditArray;

#endif //TREE_SITTER_EDIT_H
