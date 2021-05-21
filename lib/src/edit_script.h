#ifndef TREE_SITTER_EDIT_SCRIPT_H
#define TREE_SITTER_EDIT_SCRIPT_H

#include "edit_script.h"

typedef struct {
    EditArray edits;
} EditScript;

void print_edit_script(const TSLanguage *, const EditScript *);

#endif //TREE_SITTER_EDIT_SCRIPT_H
