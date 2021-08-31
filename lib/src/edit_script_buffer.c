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

static inline void fix_links(ChildPrototypeArray *acpa){
  int link = 0;
  for (uint32_t i = 0; i < acpa->size; i++) {
    ChildPrototype *pt = array_get(acpa, i);
    if (!pt->is_field){
      pt->link = link;
      link++;
    }
  }
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
      array_push(pos_buff, edit);
      break;
    case LOAD:
      fix_links(&edit.load.kids);
      array_push(pos_buff, edit);
      break;
    case LOAD_ATTACH:
      fix_links(&edit.load_attach.kids);
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
            .is_field = edit.attach.is_field,
            .kids =last_edit->load.kids
          };
          if (la_data.is_field) {
            la_data.field_id = edit.attach.field_id;
          } else {
            la_data.link = edit.attach.link;
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
      fix_links(&edit.unload.kids);
      if (neg_buff->size > 0) {
        SugaredEdit *last_edit = array_back(neg_buff);
        if (last_edit->edit_tag == DETACH && last_edit->detach.id == edit.unload.id) {
          DetachUnload du_data = {
            .id=last_edit->detach.id,
            .tag=edit.unload.tag,
            .parent_tag=last_edit->detach.parent_tag,
            .parent_id=last_edit->detach.parent_id,
            .is_field = last_edit->detach.is_field,
            .kids = edit.unload.kids
          };
          if (du_data.is_field) {
            du_data.field_id = last_edit->detach.field_id;
          } else {
            du_data.link = edit.detach.link;
          }
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
