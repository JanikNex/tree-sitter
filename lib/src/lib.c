// The Tree-sitter library can be built by compiling this one source file.
//
// The following directories must be added to the include path:
//   - include

#define _POSIX_C_SOURCE 200112L

#include "./get_changed_ranges.c"
#include "./language.c"
#include "./lexer.c"
#include "./node.c"
#include "./parser.c"
#include "./query.c"
#include "./stack.c"
#include "./subtree.c"
#include "./tree_cursor.c"
#include "./tree.c"
#include "./diff_heap.c"
#include "./literal_map.c"
#include "./pqueue.c"
#include "./diff_graph.c"
#include "./subtree_registry.c"
#include "./subtree_share.c"
#include "./edit_script_buffer.c"
#include "./edit_script.c"
#include "sha_digest/sha256.c"
#include "rax/rax.c"