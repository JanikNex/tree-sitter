#include "pqueue.h"

PriorityQueue *priority_queue_create() {
    PriorityQueue *queue = calloc(1, sizeof(PriorityQueue));
    queue->size = 2;
    queue->used = 0;
    queue->queue = ts_calloc(queue->size + 1, sizeof(Subtree *));
    return queue;
}


void priority_queue_insert(PriorityQueue *queue, Subtree *node) {
    if (queue->used == queue->size) {
        queue->size *= 2;
        queue->queue = ts_realloc(queue->queue, queue->size * sizeof(Subtree *));
    }
    queue->queue[queue->used++] = node;
    for (int i = queue->used / 2 - 1; i >= 0; i--) {
        priority_queue_heapify(queue, i);
    }
}

Subtree *priority_queue_pop(PriorityQueue *queue) {
    Subtree *node = queue->queue[0];
    priority_queue_swap(queue, 0, queue->used - 1);
    queue->used--;
    for (int i = queue->used / 2 - 1; i >= 0; i--) {
        priority_queue_heapify(queue, 0);
    }
    return node;
}

void priority_queue_heapify(PriorityQueue *queue, int i) {
    if (queue->used <= 1) {
        return;
    }
    int largest = i;
    int l = 2 * i + 1;
    int r = 2 * i + 2;
    if (l < queue->used && subtree_treeheight(queue->queue[l]) > subtree_treeheight(queue->queue[largest])) {
      largest = l;
    }
    if (r < queue->used && subtree_treeheight(queue->queue[r]) > subtree_treeheight(queue->queue[largest])) {
      largest = r;
    }
    if (largest != i) {
        priority_queue_swap(queue, i, largest);
        priority_queue_heapify(queue, largest);
    }
}

void priority_queue_destroy(PriorityQueue *queue) {
    ts_free(queue->queue);
    ts_free(queue);
}