#include "diff_graph.h"

void ts_tree_diff_graph(TSNode self, TSNode other, const TSLanguage *language, FILE *file) {
  ColorMap color_map = array_new();
  fprintf(file, "digraph tree {\n");
  fprintf(file, "edge [arrowhead=none]\n");
  ts_tree_diff_graph_node(self, language, file, NULL, &color_map);
  fprintf(file, "}\n");
  fprintf(file, "digraph tree {\n");
  fprintf(file, "edge [arrowhead=none]\n");
  ts_tree_diff_graph_node(other, language, file, NULL, &color_map);
  fprintf(file, "}\n");
  array_delete(&color_map);
}

static void write_dot_string(FILE *f, const char *string) {
  for (const char *c = string; *c; c++) {
    if (*c == '"') {
      fputs("\\\"", f);
    } else if (*c == '\n') {
      fputs("\\n", f);
    } else {
      fputc(*c, f);
    }
  }
}

static unsigned char *find_color(const TSDiffHeap *self, ColorMap *color_map) {
  for (uint32_t i = 0; i < color_map->size; i++) {
    ColorMapping *mapping = array_get(color_map, i);
    if (mapping->one == self->id || mapping->two == self->id) {
      unsigned char *target_color = mapping->color;
      array_erase(color_map, i);
      return target_color;
    }
  }
  return NULL;
}

void
ts_tree_diff_graph_node(TSNode self, const TSLanguage *language, FILE *file, unsigned char *color,
                        ColorMap *color_table) {
  TSSymbol symbol = ts_node_symbol(self);
  const TSDiffHeap *diff_heap = self.diff_heap;
  fprintf(file, "tree_%p [label=\"", diff_heap->id);
  write_dot_string(file, ts_language_symbol_name(language, symbol));
  fprintf(file, "\"");

  if (ts_node_child_count(self) == 0) fprintf(file, ", shape=plaintext");
  if (color == NULL && diff_heap->assigned != NULL) {
    TSDiffHeap *assigned_diff_heap = ts_subtree_node_diff_heap(*diff_heap->assigned);
    color = find_color(diff_heap, color_table);
    if (color == NULL) {
      color = find_color(assigned_diff_heap, color_table);
    }
    if (color == NULL) {
      unsigned int offset = color_table->size <= COLOR_SIZE ? color_table->size * 3 : 0;
      color = (unsigned char *) &(colors[offset]);
      ColorMapping map = {.color = color, .one=diff_heap->id, .two=assigned_diff_heap->id};
      array_push(color_table, map);
    }
  }
  if (color != NULL)
    fprintf(file, ", style=filled, fillcolor=\"#%02hhX%02hhX%02hhX\"", color[0], color[1], color[2]);
  fprintf(file, "]\n");

  for (uint32_t i = 0; i < ts_node_child_count(self); i++) {
    TSNode child = ts_node_child(self, i);
    ts_tree_diff_graph_node(child, language, file, color, color_table);
    const TSDiffHeap *child_diff_heap = child.diff_heap;
    fprintf(file, "tree_%p -> tree_%p [tooltip=%u]\n", diff_heap->id, child_diff_heap->id, i);
  }
}
