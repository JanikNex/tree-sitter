#include "array.h"
#include "edit.h"
#include "edit_script.h"


/**
 * Indicates whether a parent node is the actual root.
 * @param id ID (void *) of the parent node
 * @param symbol TSSymbol of the parent node
 * @return bool that indicates whether the parent node is the actual root
 */
static inline bool is_root(void *id, TSSymbol symbol) {
  return id == NULL && symbol == UINT16_MAX;
}

/**
 * Prints the full EditScript
 * @param language Pointer to the TSLanguage
 * @param edit_script Pointer to the EditScript
 */
void print_edit_script(const TSLanguage *language, const EditScript *edit_script) {
  const EditArray edit_array = edit_script->edits;
  for (uint32_t i = 0; i < edit_array.size; i++) {
    SugaredEdit *edit = array_get(&edit_array, i);
    switch (edit->edit_tag) {
      case UPDATE:
        printf("[UPDATE | %p] Old literal from %d (%d) => New literal from %d (%d)\n", edit->update.id,
               edit->update.old_start.bytes, edit->update.old_size.bytes,
               edit->update.new_start.bytes, edit->update.new_size.bytes);
        break;
      case LOAD:
        if (edit->load.is_leaf) {
          printf("[LOAD | %p] Load new leaf of type \"%s\"\n", edit->load.id,
                 ts_language_symbol_name(language, edit->load.tag));
        } else {
          printf("[LOAD | %p] Load new subtree of type \"%s\" with kids [", edit->load.id,
                 ts_language_symbol_name(language, edit->load.tag));
          for (uint32_t j = 0; j < edit->load.kids.size; j++) {
            ChildPrototype *prototype = array_get(&edit->load.kids, j);
            if (j > 0) {
              printf(", ");
            }
            if (prototype->is_field){
              printf("f%d:%p", prototype->field_id,prototype->child_id);
            }else{
              printf("_%d:%p", prototype->link,prototype->child_id);
            }
          }
          printf("]\n");
        }
        break;
      case ATTACH:
        if (is_root(edit->attach.parent_id, edit->attach.parent_tag)) {
          printf("[ATTACH | %p] To parent ROOT on link %d\n", edit->attach.id, edit->attach.link);
        } else {
          if (edit->attach.is_field){
            printf("[ATTACH | %p] To parent %p of type \"%s\" on field %d\n", edit->attach.id, edit->attach.parent_id,
                   ts_language_symbol_name(language, edit->attach.parent_tag), edit->attach.field_id);
          }else {
            printf("[ATTACH | %p] To parent %p of type \"%s\" on link %d\n", edit->attach.id, edit->attach.parent_id,
                   ts_language_symbol_name(language, edit->attach.parent_tag), edit->attach.link);
          }
        }
        break;
      case LOAD_ATTACH:
        if (edit->load_attach.is_leaf) {
          if (edit->load_attach.is_field){
            printf("[LOAD_ATTACH | %p] Load new leaf of type \"%s\" and attach to parent %p of type %s on field %d\n",
                   edit->load_attach.id,
                   ts_language_symbol_name(language, edit->load_attach.tag), edit->load_attach.parent_id,
                   ts_language_symbol_name(language, edit->load_attach.parent_tag), edit->load_attach.field_id);
          }else {
            printf("[LOAD_ATTACH | %p] Load new leaf of type \"%s\" and attach to parent %p of type %s on link %d\n",
                   edit->load_attach.id,
                   ts_language_symbol_name(language, edit->load_attach.tag), edit->load_attach.parent_id,
                   ts_language_symbol_name(language, edit->load_attach.parent_tag), edit->load_attach.link);
          }
        } else {
          printf("[LOAD_ATTACH | %p] Load new subtree of type \"%s\" with kids [", edit->load_attach.id,
                 ts_language_symbol_name(language, edit->load_attach.tag));
          for (uint32_t j = 0; j < edit->load_attach.kids.size; j++) {
            ChildPrototype *prototype = array_get(&edit->load_attach.kids, j);
            if (j > 0) {
              printf(", ");
            }
            if (prototype->is_field){
              printf("f%d:%p", prototype->field_id,prototype->child_id);
            }else{
              printf("_%d:%p", prototype->link,prototype->child_id);
            }
          }
          if (is_root(edit->load_attach.parent_id, edit->load_attach.parent_tag)) {
            printf("] and attach to parent ROOT on link %d\n", edit->load_attach.link);
          } else {
            if (edit->load_attach.is_field){
              printf("] and attach to parent %p of type \"%s\" on field %d\n", edit->load_attach.parent_id,
                     ts_language_symbol_name(language, edit->load_attach.parent_tag), edit->load_attach.field_id);
            }else{
              printf("] and attach to parent %p of type \"%s\" on link %d\n", edit->load_attach.parent_id,
                     ts_language_symbol_name(language, edit->load_attach.parent_tag), edit->load_attach.link);
            }
          }
        }
        break;
      case DETACH:
        if (is_root(edit->detach.parent_id, edit->detach.parent_tag)) {
          printf("[DETACH | %p] Node of type \"%s\" from parent ROOT on link %d\n", edit->detach.id,
                 ts_language_symbol_name(language, edit->detach.tag), edit->detach.link);
        } else {
          if (edit->detach.is_field){
            printf("[DETACH | %p] Node of type \"%s\" from parent %p of type \"%s\" on field %d\n", edit->detach.id,
                   ts_language_symbol_name(language, edit->detach.tag), edit->detach.parent_id,
                   ts_language_symbol_name(language, edit->detach.parent_tag), edit->detach.field_id);
          }else{
            printf("[DETACH | %p] Node of type \"%s\" from parent %p of type \"%s\" on link %d\n", edit->detach.id,
                   ts_language_symbol_name(language, edit->detach.tag), edit->detach.parent_id,
                   ts_language_symbol_name(language, edit->detach.parent_tag), edit->detach.link);
          }
        }
        break;
      case UNLOAD:
        printf("[UNLOAD | %p] Node of type \"%s\"", edit->unload.id,
               ts_language_symbol_name(language, edit->unload.tag));
        if (edit->unload.kids.size > 0) {
          printf(" and set its kids free [");
          ChildPrototypeArray kids = edit->unload.kids;
          for (uint32_t j = 0; j < kids.size; j++) {
            ChildPrototype *prototype = array_get(&kids, j);
            if (j > 0) {
              printf(", ");
            }
            if (prototype->is_field){
              printf("f%d:%p", prototype->field_id,prototype->child_id);
            }else{
              printf("_%d:%p", prototype->link,prototype->child_id);
            }
          }
          printf("]");
        }
        printf("\n");
        break;
      case DETACH_UNLOAD:
        if (is_root(edit->detach_unload.parent_id, edit->detach_unload.parent_tag)) {
          printf("[DETACH_UNLOAD | %p] Node of type \"%s\" from parent ROOT on link %d",
                 edit->detach_unload.id,
                 ts_language_symbol_name(language, edit->detach_unload.tag),
                 edit->detach_unload.link);
        } else {
          if (edit->detach_unload.is_field){
            printf("[DETACH_UNLOAD | %p] Node of type \"%s\" from parent %p of type \"%s\" on field %d",
                   edit->detach_unload.id,
                   ts_language_symbol_name(language, edit->detach_unload.tag),
                   edit->detach_unload.parent_id, ts_language_symbol_name(language, edit->detach_unload.parent_tag),
                   edit->detach_unload.field_id);
          }else{
            printf("[DETACH_UNLOAD | %p] Node of type \"%s\" from parent %p of type \"%s\" on link %d",
                   edit->detach_unload.id,
                   ts_language_symbol_name(language, edit->detach_unload.tag),
                   edit->detach_unload.parent_id, ts_language_symbol_name(language, edit->detach_unload.parent_tag),
                   edit->detach_unload.link);
          }

        }
        if (edit->detach_unload.kids.size > 0) {
          printf(" and set its kids free [");
          ChildPrototypeArray kids = edit->detach_unload.kids;
          for (uint32_t j = 0; j < kids.size; j++) {
            ChildPrototype *prototype = array_get(&kids, j);
            if (j > 0) {
              printf(", ");
            }
            if (prototype->is_field){
              printf("f%d:%p", prototype->field_id,prototype->child_id);
            }else{
              printf("_%d:%p", prototype->link,prototype->child_id);
            }
          }
          printf("]");
        }
        printf("\n");
        break;
    }
  }
}


/**
 * Converts a single SugaredEdit into an array of CoreEdits
 * Allocates memory!
 * @param edit SugaredEdit
 * @return Array of CoreEdit(s)
 */
CoreEditArray edit_as_core_edit(SugaredEdit edit) {
  CoreEditArray result = array_new();
  CoreEdit ce1;
  switch (edit.edit_tag) {
    case UPDATE:
      ce1 = (CoreEdit) {.edit_tag=CORE_UPDATE, .update=edit.update};
      array_push(&result, ce1);
      break;
    case LOAD:
      ce1 = (CoreEdit) {.edit_tag=CORE_LOAD, .load=edit.load};
      array_push(&result, ce1);
      break;
    case ATTACH:
      ce1 = (CoreEdit) {.edit_tag=CORE_ATTACH, .attach=edit.attach};
      array_push(&result, ce1);
      break;
    case LOAD_ATTACH: {
      Load load_data = {.id=edit.load_attach.id, .tag=edit.load_attach.tag, .is_leaf=edit.load_attach.is_leaf};
      Attach attach_data = {.id=edit.load_attach.id, .tag=edit.load_attach.tag, .link=edit.load_attach.link, .parent_id=edit.load_attach.parent_id, .parent_tag=edit.load_attach.parent_tag};
      ce1 = (CoreEdit) {.edit_tag=CORE_LOAD, .load=load_data};
      array_push(&result, ce1);
      CoreEdit ce2 = (CoreEdit) {.edit_tag=CORE_ATTACH, .attach=attach_data};
      array_push(&result, ce2);
      break;
    }
    case DETACH:
      ce1 = (CoreEdit) {.edit_tag=CORE_DETACH, .detach=edit.detach};
      array_push(&result, ce1);
      break;
    case UNLOAD:
      ce1 = (CoreEdit) {.edit_tag=CORE_UNLOAD, .unload=edit.unload};
      array_push(&result, ce1);
      break;
    case DETACH_UNLOAD: {
      Detach detach_data = {.id=edit.detach_unload.id, .tag=edit.detach_unload.tag, .link=edit.detach_unload.link, .parent_tag=edit.detach_unload.parent_tag, .parent_id=edit.detach_unload.parent_id};
      Unload unload_data = {.id=edit.detach_unload.id, .tag=edit.detach_unload.tag, .kids=edit.detach_unload.kids};
      ce1 = (CoreEdit) {.edit_tag=CORE_DETACH, .detach=detach_data};
      array_push(&result, ce1);
      CoreEdit ce2 = (CoreEdit) {.edit_tag=CORE_UNLOAD, .unload=unload_data};
      array_push(&result, ce2);
      break;
    }
  }
  return result;
}

/**
 * Deletes an EditScript and frees the used memory.
 * @param edit_script Pointer to the EditScript
 */
void ts_edit_script_delete(EditScript *edit_script) {
  for (uint32_t i = 0; i < edit_script->edits.size; ++i) {
    SugaredEdit *edit = array_get(&edit_script->edits, i);
    switch (edit->edit_tag) {
      case UNLOAD:
        array_delete(&edit->unload.kids);
        break;
      case DETACH_UNLOAD:
        array_delete(&edit->detach_unload.kids);
        break;
      case LOAD:
        array_delete(&edit->load.kids);
        break;
      case LOAD_ATTACH:
        array_delete(&edit->load_attach.kids);
        break;
      default:
        break;
    }
  }
  array_delete(&edit_script->edits);
  ts_free(edit_script);
}

/**
 * Returns the number of edits of an EditScript.
 * @param edit_script Pointer to the EditScript
 * @return uint32_t Number of edits
 */
uint32_t ts_edit_script_length(EditScript *edit_script) {
  return edit_script->edits.size;
}
