#ifndef TREE_SITTER_NODE_DIFF_HEAP_H
#define TREE_SITTER_NODE_DIFF_HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tree_sitter/api.h"

// A heap-allocated structure to hold additional attributes for the truediff algorithm
//
// When using the truediff algorithm, every node is assigned an additional DiffHeap,
// that holds any additional data that is only needed by truediff. Thereby the size
// of a node is increased by just one byte, that can hold a pointer to a DiffHeap.
struct TSNodeDiffHeap {
    void *id;
    const unsigned char structural_hash[SHA256_HASH_SIZE];
    unsigned char literal_hash[SHA256_HASH_SIZE];
    int treeheight;
    int treesize;
    void *share; //TODO: Change type to share
    unsigned int skip_node;
    TSNode *assigned;
};

#ifdef __cplusplus
}
#endif

#endif //TREE_SITTER_NODE_DIFF_HEAP_H
