/*
 * LAZY SEGMENT TREE over the 1440 minutes of a day.
 * One tree per metro line stores "extra delay in minutes" at each minute.
 *   seg_add(l, r, v) : add v to every minute in [l, r]   O(log n)
 *   seg_max(l, r)    : worst delay in [l, r]              O(log n)
 * A point query seg_max(t, t) gives the delay when departing at time t.
 */
#include <stdlib.h>
#include "metro.h"

void seg_init(SegTree *s, int n)
{
    s->n  = n;
    s->mx = (int *)calloc(4 * n, sizeof(int));
    s->lz = (int *)calloc(4 * n, sizeof(int));
}

void seg_free(SegTree *s)
{
    free(s->mx);
    free(s->lz);
    s->mx = s->lz = NULL;
}

static int max2(int a, int b) { return a > b ? a : b; }

int g_recSeg = 0;
SegRecord g_segRec;

static void rec_node(int node, int full)
{
    if (g_recSeg && g_segRec.n < REC_SEG) {
        g_segRec.node[g_segRec.n] = node;
        g_segRec.full[g_segRec.n] = full;
        g_segRec.n++;
    }
}

/* push pending lazy value down to both children */
static void push_down(SegTree *s, int node)
{
    int v = s->lz[node];
    if (v) {
        s->mx[2 * node]     += v; s->lz[2 * node]     += v;
        s->mx[2 * node + 1] += v; s->lz[2 * node + 1] += v;
        s->lz[node] = 0;
    }
}

static void update(SegTree *s, int node, int lo, int hi, int l, int r, int v)
{
    int mid;
    if (r < lo || hi < l) return;
    if (l <= lo && hi <= r) { s->mx[node] += v; s->lz[node] += v; rec_node(node, 1); return; }
    rec_node(node, 0);
    push_down(s, node);
    mid = (lo + hi) / 2;
    update(s, 2 * node,     lo,      mid, l, r, v);
    update(s, 2 * node + 1, mid + 1, hi,  l, r, v);
    s->mx[node] = max2(s->mx[2 * node], s->mx[2 * node + 1]);
}

static int query(SegTree *s, int node, int lo, int hi, int l, int r)
{
    int mid;
    if (r < lo || hi < l) return -INF;
    if (l <= lo && hi <= r) return s->mx[node];
    push_down(s, node);
    mid = (lo + hi) / 2;
    return max2(query(s, 2 * node, lo, mid, l, r),
                query(s, 2 * node + 1, mid + 1, hi, l, r));
}

void seg_add(SegTree *s, int l, int r, int v) { update(s, 1, 0, s->n - 1, l, r, v); }
int  seg_max(SegTree *s, int l, int r)        { return query(s, 1, 0, s->n - 1, l, r); }
