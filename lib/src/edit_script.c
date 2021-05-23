#include "array.h"
#include "edit.h"
#include "edit_script.h"
#include "diff_heap.h"

void print_edit_script(const TSLanguage *language, const EditScript *edit_script) {
  const EditArray edit_array = edit_script->edits;
  for (uint32_t i = 0; i < edit_array.size; i++) {
    Edit *edit = array_get(&edit_array, i);
    switch (edit->type) {
      case UPDATE:
        printf("[UPDATE | %p] Old literal from %d (%d) => New literal from %d (%d)\n", edit->id,
               edit->update.old_start.bytes, edit->update.old_size.bytes, edit->update.new_start.bytes,
               edit->update.new_size.bytes);
        break;
      case LOAD:
        if (edit->loading.is_leaf) {
          printf("[LOAD | >%p<] Load new leaf of type \"%s\"\n", edit->id,
                 ts_language_symbol_name(language, edit->loading.tag));
        } else {
          printf("[LOAD | >%p<] Load new subtree of type \"%s\" with kids [", edit->id,
                 ts_language_symbol_name(language, edit->loading.tag));
          for (uint32_t j = 0; j < edit->loading.node.kids.size; j++) {
            ChildPrototype *prototype = array_get(&edit->loading.node.kids, j);
            if (j > 0) {
              printf(", ");
            }
            printf("%p", prototype->child_id);
          }
          printf("]\n");
        }
        break;
      case ATTACH:
        printf("[ATTACH | %p] To parent %p of type \"%s\" on link %d\n", edit->id, edit->basic.parent_id,
               ts_language_symbol_name(language, edit->basic.parent_tag), edit->basic.link);
        break;
      case LOAD_ATTACH:
        if (edit->loading.is_leaf) {
          printf("[LOAD_ATTACH | >%p<] Load new leaf of type \"%s\" and attach to parent %p of type %s on link %d\n",
                 edit->id,
                 ts_language_symbol_name(language, edit->advanced.tag), edit->advanced.parent_id,
                 ts_language_symbol_name(language, edit->advanced.parent_tag), edit->advanced.link);
        } else {
          printf("[LOAD_ATTACH | >%p<] Load new subtree of type \"%s\" with kids [", edit->id,
                 ts_language_symbol_name(language, edit->advanced.tag));
          for (uint32_t j = 0; j < edit->advanced.kids.size; j++) {
            ChildPrototype *prototype = array_get(&edit->advanced.kids, j);
            if (j > 0) {
              printf(", ");
            }
            printf("%p", prototype->child_id);
          }
          printf("] and attach to parent %p of type \"%s\" on link %d\n", edit->advanced.parent_id,
                 ts_language_symbol_name(language, edit->advanced.parent_tag), edit->advanced.link);
        }
        break;
      case DETACH:
        printf("[DETACH | %p] From parent %p of type \"%s\" on link %d\n", edit->id, edit->basic.parent_id,
               ts_language_symbol_name(language, edit->basic.parent_tag), edit->basic.link);
        break;
      case UNLOAD:
        printf("[UNLOAD | \"%p\"] %s\n", edit->id, ts_language_symbol_name(language, edit->loading.tag));
        break;
      case DETACH_UNLOAD:
        printf("[DETACH_UNLOAD | %p] from parent %p of type \"%s\" on link %d\n", edit->id,
               edit->basic.parent_id, ts_language_symbol_name(language, edit->basic.parent_tag), edit->basic.link);
        break;
    }
  }
}
