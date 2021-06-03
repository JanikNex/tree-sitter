#ifndef TREE_SITTER_EDIT_SCRIPT_BUFFER_H
#define TREE_SITTER_EDIT_SCRIPT_BUFFER_H

#include <stdio.h>
#include "tree_sitter/api.h"
#include "array.h"
#include "edit.h"
#include "edit_script.h"

typedef struct {
    EditArray negative_buffer;
    EditArray positive_buffer;
} EditScriptBuffer;

EditScriptBuffer ts_edit_script_buffer_create();

void ts_edit_script_buffer_add(EditScriptBuffer *, SugaredEdit);

EditScript *ts_edit_script_buffer_finalize(EditScriptBuffer *);

#endif //TREE_SITTER_EDIT_SCRIPT_BUFFER_H
