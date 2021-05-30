#ifndef TREE_SITTER_EDIT_SCRIPT_H
#define TREE_SITTER_EDIT_SCRIPT_H

#include "edit_script.h"

struct EditScript {
    EditArray edits;
};

CoreEditArray edit_as_core_edit(Edit);

#endif //TREE_SITTER_EDIT_SCRIPT_H
