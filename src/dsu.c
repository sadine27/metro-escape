/*
 * UNION-FIND / Disjoint Set Union
 * path compression + union by rank => ~O(alpha(n)) per operation.
 * Used by Kruskal's MST, island detection and closure impact analysis.
 */
#include "metro.h"

void dsu_init(DSU *d, int n)
{
    int i;
    for (i = 0; i < n; i++) { d->parent[i] = i; d->rank[i] = 0; }
}

int dsu_find(DSU *d, int x)
{
    if (d->parent[x] != x) d->parent[x] = dsu_find(d, d->parent[x]);
    return d->parent[x];
}

/* returns 1 if two different sets were merged */
int dsu_union(DSU *d, int a, int b)
{
    a = dsu_find(d, a);
    b = dsu_find(d, b);
    if (a == b) return 0;
    if (d->rank[a] < d->rank[b]) { int t = a; a = b; b = t; }
    d->parent[b] = a;
    if (d->rank[a] == d->rank[b]) d->rank[a]++;
    return 1;
}
