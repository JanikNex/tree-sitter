#ifndef TREE_SITTER_EDIT_H
#define TREE_SITTER_EDIT_H

#include "subtree.h"

typedef struct {
  void *child_id;
  bool is_field;
  union {
    TSFieldId field_id;
    uint32_t link;
  };
} ChildPrototype;

typedef Array(ChildPrototype) ChildPrototypeArray;


typedef enum {
    CORE_ATTACH, CORE_DETACH, CORE_UNLOAD, CORE_LOAD, CORE_UPDATE
} CoreEditTag;

typedef enum {
    ATTACH, DETACH, UNLOAD, LOAD, LOAD_ATTACH, DETACH_UNLOAD, UPDATE
} EditTag;

typedef struct {
  void *id;
  TSSymbol tag;
  void *parent_id;
  TSSymbol parent_tag;
  bool is_field;
  union {
    TSFieldId field_id;
    uint32_t link;
  };
} Attach;

typedef struct {
  void *id;
  TSSymbol tag;
  void *parent_id;
  TSSymbol parent_tag;
  bool is_field;
  union {
    TSFieldId field_id;
    uint32_t link;
  };
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
    ChildPrototypeArray kids;
} Load;

typedef struct {
    void *id;
    TSSymbol tag;
    Length old_start;
    Length old_size;
    Length new_start;
    Length new_size;
} Update;

typedef struct {
  bool is_leaf: 1;
  void *id;
  void *parent_id;
  TSSymbol parent_tag;
  TSSymbol tag;
  ChildPrototypeArray kids;
  bool is_field;
  union {
    TSFieldId field_id;
    uint32_t link;
  };
} LoadAttach;

typedef struct {
  void *id;
  void *parent_id;
  TSSymbol parent_tag;
  TSSymbol tag;
  ChildPrototypeArray kids;
  bool is_field;
  union {
    TSFieldId field_id;
    uint32_t link;
  };
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
} SugaredEdit;

typedef Array(SugaredEdit) EditArray;
typedef Array(CoreEdit) CoreEditArray;

#endif //TREE_SITTER_EDIT_H
