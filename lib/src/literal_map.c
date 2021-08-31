#include <string.h>
#include "alloc.h"
#include "literal_map.h"

/**
 * Creates a new LiteralMap
 * Allocates Memory!
 * @param lang Pointer to the TSLanguage
 * @return Pointer to the created TSLiteralMap
 */
TSLiteralMap *ts_literal_map_create(const TSLanguage *lang) {
  TSLiteralMap *lit_map = ts_malloc(sizeof(TSLiteralMap));
  lit_map->symbol_count = ts_language_symbol_count(lang);
  uint32_t map_size = (lit_map->symbol_count / 8) + 1;
  lit_map->symbol_map = ts_malloc(map_size);
  lit_map->unnamed_tokens = ts_malloc(map_size);
  memset(lit_map->symbol_map, 0, map_size);
  memset(lit_map->unnamed_tokens, 0, map_size);
  return lit_map;
}

/**
 * Sets the n-th bit of the symbol map to 1
 * @param self Pointer to the TSLiteralMap
 * @param idx TSSymbol of the literal to be added
 */
void ts_literal_map_add_literal(const TSLiteralMap *self, uint16_t idx) { //TODO: Can this be generated dynamically?
  self->symbol_map[idx / 8] |= 1 << (idx % 8);
}

/**
 * Sets the n-th bit of the symbol map to 1
 * @param self Pointer to the TSLiteralMap
 * @param idx TSSymbol of the unnamed symbol to be added
 */
void ts_literal_map_add_unnamed_token(const TSLiteralMap *self, uint16_t idx) { //TODO: Can this be generated dynamically?
  self->unnamed_tokens[idx / 8] |= 1 << (idx % 8);
}

/**
 * Destroys the TSLiteralMap and frees its memory.
 * @param self Pointer to the TSLiteralMap
 */
void ts_literal_map_destroy(TSLiteralMap *self) {
  ts_free(self->symbol_map);
  ts_free(self->unnamed_tokens);
  ts_free(self);
}
