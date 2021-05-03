#ifndef TREE_SITTER_LITERAL_MAP_H
#define TREE_SITTER_LITERAL_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tree_sitter/api.h"
#include <stdbool.h>


struct TSLiteralMap {
    uint32_t symbol_count;
    uint8_t *symbol_map;
    uint16_t boolean_symbols[2];
};

static inline bool ts_literal_map_is_literal(const TSLiteralMap *self, uint16_t symbol) {
  return self->symbol_map[symbol / 8] & (1 << (symbol % 8));
}

static inline bool ts_literal_map_is_bool(const TSLiteralMap *self, uint16_t symbol) {
  return symbol == self->boolean_symbols[0] || symbol == self->boolean_symbols[1];
}


#ifdef __cplusplus
}
#endif

#endif //TREE_SITTER_LITERAL_MAP_H
