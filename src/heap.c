/*
 * Binary MIN-HEAP (priority queue), array based.
 *   push : O(log n)   sift-up
 *   pop  : O(log n)   sift-down
 * Used by Dijkstra, A*, evacuation and Yen's candidate list.
 */
#include <stdlib.h>
#include "metro.h"

void heap_init(MinHeap *h, int cap)
{
    if (cap < 4) cap = 4;
    h->data = (HeapItem *)malloc(sizeof(HeapItem) * cap);
    h->size = 0;
    h->cap  = cap;
}

void heap_free(MinHeap *h)
{
    free(h->data);
    h->data = NULL;
    h->size = h->cap = 0;
}

static void swap_items(HeapItem *a, HeapItem *b)
{
    HeapItem t = *a; *a = *b; *b = t;
}

void heap_push(MinHeap *h, int key, int val)
{
    int i;
    if (h->size == h->cap) {                 /* grow like a vector */
        h->cap *= 2;
        h->data = (HeapItem *)realloc(h->data, sizeof(HeapItem) * h->cap);
    }
    i = h->size++;
    h->data[i].key = key;
    h->data[i].val = val;
    while (i > 0) {                          /* sift up */
        int p = (i - 1) / 2;
        if (h->data[p].key <= h->data[i].key) break;
        swap_items(&h->data[p], &h->data[i]);
        i = p;
    }
}

HeapItem heap_pop(MinHeap *h)
{
    HeapItem top = h->data[0];
    int i = 0;
    h->data[0] = h->data[--h->size];
    for (;;) {                               /* sift down */
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < h->size && h->data[l].key < h->data[m].key) m = l;
        if (r < h->size && h->data[r].key < h->data[m].key) m = r;
        if (m == i) break;
        swap_items(&h->data[i], &h->data[m]);
        i = m;
    }
    return top;
}
