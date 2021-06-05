#include "array.h"
#include "edit.h"
#include "edit_script.h"
#include "diff_heap.h"

void print__edit_script(const TSLanguage *language, const EditScript *edit_script, bool minimized) {
  const EditArray edit_array = edit_script->edits;
  for (uint32_t i = 0; i < edit_array.size; i++) {
    SugaredEdit *edit = array_get(&edit_array, i);
    switch (edit->edit_tag) {
      case UPDATE:
        printf("[UPDATE | %p] Old literal from %d (%d) [-> %d] => New literal from %d (%d) [-> %d]\n", edit->update.id,
               edit->update.old_start.bytes, edit->update.old_size.bytes, edit->update.old_padding.bytes,
               edit->update.new_start.bytes, edit->update.new_size.bytes, edit->update.new_padding.bytes);
        break;
      case LOAD:
        if (minimized && ts_language_symbol_type(language, edit->load.tag) != TSSymbolTypeRegular) {
          break;
        }
        if (edit->load.is_leaf) {
          printf("[LOAD | %p] Load new leaf of type \"%s\"\n", edit->load.id,
                 ts_language_symbol_name(language, edit->load.tag));
        } else {
          printf("[LOAD | %p] Load new subtree of type \"%s\" with kids [", edit->load.id,
                 ts_language_symbol_name(language, edit->load.tag));
          for (uint32_t j = 0; j < edit->load.node.kids.size; j++) {
            ChildPrototype *prototype = array_get(&edit->load.node.kids, j);
            if (j > 0) {
              printf(", ");
            }
            printf("%p", prototype->child_id);
          }
          printf("]\n");
        }
        break;
      case ATTACH:
        if (minimized && ts_language_symbol_type(language, edit->attach.tag) != TSSymbolTypeRegular) {
          break;
        }
        printf("[ATTACH | %p] To parent %p of type \"%s\" on link %d\n", edit->attach.id, edit->attach.parent_id,
               ts_language_symbol_name(language, edit->attach.parent_tag), edit->attach.link);
        break;
      case LOAD_ATTACH:
        if (minimized && ts_language_symbol_type(language, edit->load_attach.tag) != TSSymbolTypeRegular) {
          break;
        }
        if (edit->load_attach.is_leaf) {
          printf("[LOAD_ATTACH | %p] Load new leaf of type \"%s\" and attach to parent %p of type %s on link %d\n",
                 edit->load_attach.id,
                 ts_language_symbol_name(language, edit->load_attach.tag), edit->load_attach.parent_id,
                 ts_language_symbol_name(language, edit->load_attach.parent_tag), edit->load_attach.link);
        } else {
          printf("[LOAD_ATTACH | %p] Load new subtree of type \"%s\" with kids [", edit->load_attach.id,
                 ts_language_symbol_name(language, edit->load_attach.tag));
          EditNodeData *node_data = &edit->load_attach.node;
          for (uint32_t j = 0; j < node_data->kids.size; j++) {
            ChildPrototype *prototype = array_get(&node_data->kids, j);
            if (j > 0) {
              printf(", ");
            }
            printf("%p", prototype->child_id);
          }
          printf("] and attach to parent %p of type \"%s\" on link %d\n", edit->load_attach.parent_id,
                 ts_language_symbol_name(language, edit->load_attach.parent_tag), edit->load_attach.link);
        }
        break;
      case DETACH:
        if (minimized && ts_language_symbol_type(language, edit->detach.tag) != TSSymbolTypeRegular) {
          break;
        }
        printf("[DETACH | %p] Node of type \"%s\" from parent %p of type \"%s\" on link %d\n", edit->detach.id,
               ts_language_symbol_name(language, edit->detach.tag), edit->detach.parent_id,
               ts_language_symbol_name(language, edit->detach.parent_tag), edit->detach.link);
        break;
      case UNLOAD:
        if (minimized && ts_language_symbol_type(language, edit->unload.tag) != TSSymbolTypeRegular) {
          break;
        }
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
            printf("%p", prototype->child_id);
          }
          printf("]");
        }
        printf("\n");
        break;
      case DETACH_UNLOAD:
        if (minimized && ts_language_symbol_type(language, edit->detach_unload.tag) != TSSymbolTypeRegular) {
          break;
        }
        printf("[DETACH_UNLOAD | %p] Node of type \"%s\" from parent %p of type \"%s\" on link %d",
               edit->detach_unload.id,
               ts_language_symbol_name(language, edit->detach_unload.tag),
               edit->detach_unload.parent_id, ts_language_symbol_name(language, edit->detach_unload.parent_tag),
               edit->detach_unload.link);
        if (edit->detach_unload.kids.size > 0) {
          printf(" and set its kids free [");
          ChildPrototypeArray kids = edit->detach_unload.kids;
          for (uint32_t j = 0; j < kids.size; j++) {
            ChildPrototype *prototype = array_get(&kids, j);
            if (j > 0) {
              printf(", ");
            }
            printf("%p", prototype->child_id);
          }
          printf("]");
        }
        printf("\n");
        break;
    }
  }
}

void print_edit_script(const TSLanguage *language, const EditScript *edit_script) {
  print__edit_script(language, edit_script, false);
}

void print_minimized_edit_script(const TSLanguage *language, const EditScript *edit_script) {
  print__edit_script(language, edit_script, true);
}

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
      if (edit.load_attach.is_leaf) {
        load_data.leaf = edit.load_attach.leaf;
      } else {
        load_data.node = edit.load_attach.node;
      }
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
        if (!edit->load.is_leaf) {
          array_delete(&edit->load.node.kids);
        }
        break;
      case LOAD_ATTACH:
        if (!edit->load_attach.is_leaf) {
          array_delete(&edit->load_attach.node.kids);
        }
        break;
      default:
        break;
    }
  }
  array_delete(&edit_script->edits);
  ts_free(edit_script);
}

uint32_t ts_edit_script_length(EditScript *edit_script) {
  return edit_script->edits.size;
}
