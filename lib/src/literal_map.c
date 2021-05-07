#include <string.h>
#include "alloc.h"
#include "literal_map.h"

TSLiteralMap *ts_literal_map_create(const TSLanguage *lang) {
  TSLiteralMap *lit_map = ts_malloc(sizeof(TSLiteralMap));
  lit_map->symbol_count = ts_language_symbol_count(lang);
  uint32_t map_size = (lit_map->symbol_count / 8) + 1;
  lit_map->symbol_map = ts_malloc(map_size);
  memset(lit_map->symbol_map, 0, map_size);
  lit_map->boolean_symbols[0] = 0;
  lit_map->boolean_symbols[1] = 0;
  return lit_map;
}

void ts_literal_map_add_literal(const TSLiteralMap *self, uint16_t idx) { //TODO: Can this be generated dynamically?
  self->symbol_map[idx / 8] |= 1 << (idx % 8);
}

void ts_literal_map_destroy(TSLiteralMap *self) {
  ts_free(self->symbol_map);
  ts_free(self);
}

void ts_literal_map_set_booleans(TSLiteralMap *self, uint16_t sym_true, uint16_t sym_false) {
  self->boolean_symbols[0] = sym_true;
  self->boolean_symbols[1] = sym_false;
}
