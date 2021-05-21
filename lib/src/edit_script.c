#include "array.h"
#include "edit.h"
#include "edit_script.h"
#include "diff_heap.h"

void print_edit_script(const TSLanguage *language, const EditScript *edit_script) {
  const EditArray edit_array = edit_script->edits;
  for (uint32_t i = 0; i < edit_array.size; i++) {
    Edit *edit = array_get(&edit_array, i);
    void *subtree_id = NULL;
    if (edit->subtree != NULL) {
      TSDiffHeap *edit_diff_heap = ts_subtree_node_diff_heap(*edit->subtree);
      subtree_id = edit_diff_heap->id;
    }
    switch (edit->type) {
      case UPDATE:
        printf("[UPDATE | %p] Old literal from %d (%d) => New literal from %d (%d)\n", subtree_id,
               edit->update.old_start, edit->update.old_length, edit->update.new_start, edit->update.new_length);
        break;
      case LOAD:
        printf("[LOAD | %p]\n", subtree_id);
        break;
      case ATTACH:
        printf("[ATTACH | %p] To parent %p of type %s on link %d\n", subtree_id, edit->basic.parent,
               ts_language_symbol_name(language, edit->basic.parent_tag), edit->basic.link);
        break;
      case LOAD_ATTACH:
        printf("[LOAD_ATTACH | %p]\n", subtree_id);
        break;
      case DETACH:
        printf("[DETACH | %p] From parent %p of type %s on link %d\n", subtree_id, edit->basic.parent,
               ts_language_symbol_name(language, edit->basic.parent_tag), edit->basic.link);
        break;
      case UNLOAD:
        printf("[UNLOAD | %p]\n", subtree_id);
        break;
      case DETACH_UNLOAD:
        printf("[DETACH_UNLOAD | %p]\n", subtree_id);
        break;
    }
  }
}
