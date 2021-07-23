#include "edit_script_buffer.h"

#define ADVANCED_EDITS

/**
 * Creates a new EditScriptBuffer
 * Allocates Memory!
 * @return New EditScriptBuffer
 */
EditScriptBuffer ts_edit_script_buffer_create() {
  return (EditScriptBuffer) {.negative_buffer = array_new(), .positive_buffer=array_new()};
}

/**
 * Inserts an edit into the positive or negative buffer depending on the type.
 * If advanced edits are activated, successive load and attach edits as well as detach
 * and unload edits can be converted into the combined form.
 * @param buffer Pointer to the EditScriptBuffer
 * @param edit SugaredEdit to be added
 */
void ts_edit_script_buffer_add(EditScriptBuffer *buffer, SugaredEdit edit) {
  EditArray *pos_buff = &buffer->positive_buffer;
  EditArray *neg_buff = &buffer->negative_buffer;
  switch (edit.edit_tag) {
    case UPDATE:
    case UPDATE_PADDING:
    case LOAD:
    case LOAD_ATTACH:
      array_push(pos_buff, edit);
      break;
    case ATTACH:
#ifdef ADVANCED_EDITS
      if (pos_buff->size > 0) {
        SugaredEdit *last_edit = array_back(pos_buff);
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
        SugaredEdit *last_edit = array_back(neg_buff);
        if (last_edit->edit_tag == DETACH && last_edit->detach.id == edit.unload.id) {
          DetachUnload du_data = {
            .id=last_edit->detach.id,
            .tag=edit.unload.tag,
            .parent_tag=last_edit->detach.parent_tag,
            .parent_id=last_edit->detach.parent_id,
            .link=last_edit->detach.link,
            .kids = edit.unload.kids
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

/**
 * Finalizes the buffer by appending the negative to the positive edits.
 * A new EditScript is created for this, for which memory is allocated first.
 * @param buffer Pointer to the EditScriptBuffer
 * @return Pointer to the generated EditScript
 */
EditScript *ts_edit_script_buffer_finalize(EditScriptBuffer *buffer) {
  array_push_all(&buffer->negative_buffer, &buffer->positive_buffer);
  array_delete(&buffer->positive_buffer);
  EditScript *edit_script = ts_malloc(sizeof(EditScript));
  edit_script->edits = buffer->negative_buffer;
  return edit_script;
}
