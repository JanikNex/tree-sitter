#ifndef TREE_SITTER_LITERAL_MAP_H
#define TREE_SITTER_LITERAL_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tree_sitter/api.h"
#include <stdbool.h>

/**
 * TSLiteralMap
 *
 * The LiteralMap is used as a datastructure to store the literal nodes of a TSLanguage.
 * Since it is not possible to read out directly whether a type is a literal, these must be defined explicitly.
 * The struct therefore contains the number of different types and a bit array of the corresponding
 * size (rounded up to the next byte). To identify a type as a literal, the bits are set to 1 in the
 * corresponding places (by SymbolID).
 *
 * We extend the TSLiteralMap to recognize relevant unnamed nodes.
 */
struct TSLiteralMap {
    uint32_t symbol_count;
    uint8_t *symbol_map;
    uint8_t *unnamed_tokens;
};

/**
 * This function returns the current value of the n-th bit of the symbol map
 * @param self Pointer to the TSLiteralMap
 * @param symbol TSSymbol to be tested
 * @return bool that indicates whether the symbol is a literal
 */
static inline bool ts_literal_map_is_literal(const TSLiteralMap *self, uint16_t symbol) {
  return self->symbol_map[symbol / 8] & (1 << (symbol % 8));
}

/**
 * This function returns the current value of the n-th bit of the symbol map
 * @param self Pointer to the TSLiteralMap
 * @param symbol TSSymbol to be tested
 * @return bool that indicates whether the symbol is a an unnamed node
 */
static inline bool ts_literal_map_is_unnamed_token(const TSLiteralMap *self, uint16_t symbol) {
  return self->unnamed_tokens[symbol / 8] & (1 << (symbol % 8));
}


#ifdef __cplusplus
}
#endif

#endif //TREE_SITTER_LITERAL_MAP_H
