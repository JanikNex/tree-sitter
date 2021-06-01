#include "edit_script_buffer.h"

#define ADVANCED_EDITS

EditScriptBuffer ts_edit_script_buffer_create() {
  return (EditScriptBuffer) {.negative_buffer = array_new(), .positive_buffer=array_new()};
}

void ts_edit_script_buffer_add(EditScriptBuffer *buffer, Edit edit) {
  EditArray *pos_buff = &buffer->positive_buffer;
  EditArray *neg_buff = &buffer->negative_buffer;
  switch (edit.edit_tag) {
    case UPDATE:
    case LOAD:
    case LOAD_ATTACH:
      array_push(pos_buff, edit);
      break;
    case ATTACH:
#ifdef ADVANCED_EDITS
      if (pos_buff->size > 0) {
        Edit *last_edit = array_back(pos_buff);
        if (last_edit->edit_tag == LOAD && last_edit->load.id == edit.attach.id) {
          LoadAttach la_data = (LoadAttach) {
            .id=last_edit->load.id,
            .tag=last_edit->load.tag,
            .is_leaf=last_edit->load.is_leaf,
            .parent_id=edit.attach.parent_id,
            .parent_tag=edit.attach.parent_tag,
            .link=edit.attach.link,
          };
          if (last_edit->load.is_leaf) {
            la_data.leaf = last_edit->load.leaf;
          } else {
            la_data.node = last_edit->load.node;
          }
          last_edit->edit_tag = LOAD_ATTACH;
          last_edit->load_attach = la_data;
        } else {
          array_push(pos_buff, edit);
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
        if (last_edit->edit_tag == DETACH && last_edit->detach.subtree == edit.unload.subtree) {
          DetachUnload du_data = {
            .id=last_edit->detach.id,
            .subtree=last_edit->detach.subtree,
            .tag=edit.unload.tag,
            .parent_tag=last_edit->detach.parent_tag,
            .parent_id=last_edit->detach.parent_id,
            .link=last_edit->detach.link
          };
          last_edit->edit_tag = DETACH_UNLOAD;
          last_edit->detach_unload = du_data;
        } else {
          array_push(neg_buff, edit);
        }
      } else {
        array_push(neg_buff, edit);
      }
#else
      array_push(neg_buff, edit);
#endif
  }
}

EditScript *ts_edit_script_buffer_finalize(EditScriptBuffer *buffer) {
  array_push_all(&buffer->negative_buffer, &buffer->positive_buffer);
  array_delete(&buffer->positive_buffer);
  EditScript *edit_script = ts_malloc(sizeof(EditScript));
  edit_script->edits = buffer->negative_buffer;
  return edit_script;
}
