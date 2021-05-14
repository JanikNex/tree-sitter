#ifndef DIFF_SITTER_PRIORITYQUEUE_H
#define DIFF_SITTER_PRIORITYQUEUE_H

#include "tree_sitter/api.h"
#include "subtree.h"
#include "diff_heap.h"

typedef struct priorityq {
    Subtree **queue;
    int size;
    int used;
} PriorityQueue;

static inline unsigned int subtree_treeheight(const Subtree *subtree){
  const TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*subtree);
  return diff_heap->treeheight;
}

static inline void priority_queue_swap(PriorityQueue *queue, int idx1, int idx2) {
  Subtree *tmp = queue->queue[idx1];
  queue->queue[idx1] = queue->queue[idx2];
  queue->queue[idx2] = tmp;
}

static inline bool priority_queue_is_empty(const PriorityQueue *queue){
  return queue->used == 0;
}

static inline unsigned int priority_queue_head_value(const PriorityQueue *queue){
  Subtree *head_subtree = queue->queue[0];
  return ts_subtree_node_diff_heap(*head_subtree)->treeheight;
}

PriorityQueue *priority_queue_create();

void priority_queue_insert(PriorityQueue *queue, Subtree *node);

Subtree *priority_queue_pop(PriorityQueue *queue);

void priority_queue_heapify(PriorityQueue *queue, int i);

void priority_queue_destroy(PriorityQueue *queue);

#endif //DIFF_SITTER_PRIORITYQUEUE_H
