#include "pqueue.h"

static inline int32_t queue_val(PriorityQueue *queue, int idx) {
  if (idx == 0) {
    return INT32_MAX;
  }
  if (idx > queue->used) {
    return 0;
  }
  TSDiffHeap *diff_heap = ts_subtree_node_diff_heap(*queue->queue[idx]);
  return (int32_t) diff_heap->treeheight;
}

static inline int parent(int idx) {
  return idx / 2;
}

static inline int left(int idx) {
  return idx * 2;
}

static inline int right(int idx) {
  return (idx * 2) + 1;
}

static inline void priority_queue_swap(PriorityQueue *queue, int idx1, int idx2) {
  Subtree *tmp = queue->queue[idx1];
  queue->queue[idx1] = queue->queue[idx2];
  queue->queue[idx2] = tmp;
}

PriorityQueue *priority_queue_create() {
  PriorityQueue *queue = ts_calloc(1, sizeof(PriorityQueue));
  queue->size = 2;
  queue->used = 0;
  queue->queue = ts_calloc(queue->size + 1, sizeof(Subtree *));
  queue->queue[0] = NULL;
  return queue;
}

void priority_queue_destroy(PriorityQueue *queue) {
  ts_free(queue->queue);
  ts_free(queue);
}


void priority_queue_insert(PriorityQueue *queue, Subtree *node) {
  if (queue->used == queue->size) {
    queue->size *= 2;
    queue->queue = ts_realloc(queue->queue, (queue->size + 1) * sizeof(Subtree *));
  }
  queue->used++;
  queue->queue[queue->used] = node;
  int current = queue->used;
  int par = parent(current);
  while (queue_val(queue, current) > queue_val(queue, par)) {
    priority_queue_swap(queue, current, parent(current));
    current = par;
    par = parent(current);
  }
}

static inline void priority_queue_heapify(PriorityQueue *queue) {
  int idx = 1;
  while (idx < queue->used) {
    int idx_val = queue_val(queue, idx);
    int left_child = left(idx);
    int right_child = right(idx);
    bool has_left = left_child <= queue->used;
    bool has_right = right_child <= queue->used;
    if (!has_left && !has_right) {
      return;
    }
    int max_child_idx = -1;
    int max_child_val = -1;
    if (has_left) {
      int left_val = queue_val(queue, left_child);
      max_child_val = left_val;
      max_child_idx = left_child;
    }
    if (has_right) {
      int right_val = queue_val(queue, right_child);
      if (right_val > max_child_val) {
        max_child_val = right_val;
        max_child_idx = right_child;
      }
    }
    if (idx_val < max_child_val) {
      priority_queue_swap(queue, idx, max_child_idx);
      idx = max_child_idx;
    } else {
      break;
    }
  }
}

Subtree *priority_queue_pop(PriorityQueue *queue) {
  if (queue->used == 0) {
    return NULL;
  }
  Subtree *popped = queue->queue[1];
  queue->queue[1] = queue->queue[queue->used];
  queue->used--;
  priority_queue_heapify(queue);
  return popped;
}