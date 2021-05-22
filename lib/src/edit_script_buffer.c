#include "edit_script_buffer.h"

#define ADVANCED_EDITS

EditScriptBuffer ts_edit_script_buffer_create() {
  return (EditScriptBuffer) {.negative_buffer = array_new(), .positive_buffer=array_new()};
}

void ts_edit_script_buffer_add(EditScriptBuffer *buffer, Edit edit) {
  EditArray *pos_buff = &buffer->positive_buffer;
  EditArray *neg_buff = &buffer->negative_buffer;
  switch (edit.type) {
    case UPDATE:
    case LOAD:
    case LOAD_ATTACH:
      array_push(pos_buff, edit);
      break;
    case ATTACH:
#ifdef ADVANCED_EDITS
      if (pos_buff->size > 0) {
        Edit *last_edit = array_back(pos_buff);
        if (last_edit->type == LOAD && last_edit->loading.id == edit.basic.id) {
          void *id = last_edit->loading.id;
          TSSymbol symbol = last_edit->loading.tag;
          ChildPrototypeArray child_array = last_edit->loading.kids;
          last_edit->type = LOAD_ATTACH;
          last_edit->advanced.id = id;
          last_edit->advanced.tag = symbol;
          last_edit->advanced.kids = child_array;
          last_edit->advanced.link = edit.basic.link;
          last_edit->advanced.parent_tag = edit.basic.parent_tag;
          last_edit->advanced.parent_id = edit.advanced.parent_id;
        }
      } else {
        array_push(pos_buff, edit);
      }
#else
      array_push(pos_buff, edit);
#endif
      break;
    case DETACH:
    case DETACH_UNLOAD:
      array_push(neg_buff, edit);
      break;
    case UNLOAD:
#ifdef ADVANCED_EDITS
      if (neg_buff->size > 0) {
        Edit *last_edit = array_back(neg_buff);
        if (last_edit->type == DETACH && last_edit->subtree == edit.subtree) {
          last_edit->type = DETACH_UNLOAD;
        }
      } else {
        array_push(neg_buff, edit);
      }
#else
      array_push(neg_buff, edit);
#endif
  }
}

EditScript ts_edit_script_buffer_finalize(EditScriptBuffer *buffer) {
  array_push_all(&buffer->negative_buffer, &buffer->positive_buffer);
  array_delete(&buffer->positive_buffer);
  printf("Generated %d edits!\n", buffer->negative_buffer.size);
  return (EditScript) {.edits = buffer->negative_buffer};
}
