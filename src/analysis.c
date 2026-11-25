/*
 * NETWORK ANALYSIS
 *   evacuate        multi-source Dijkstra from every city exit
 *   find_critical   Tarjan's articulation points + bridges (DFS low-link)
 *   kruskal_mst     Kruskal's MST with merge sort + union-find
 *   find_islands    connected components with union-find
 */
#include <string.h>
#include "metro.h"

/* ---------------- spreading: multi-source Dijkstra ----------------
 * Something spreads from all sources at once along the tracks, taking
 * (minutes * pct / 100) to cross each track. Used twice:
 *   - FIRE: one source, ignores closures, pct > 100 = slower than trains
 *   - EVACUATION: every exit is a source; time[v] = minutes to nearest exit
 */
void spread(Metro *m, const int src[], int nsrc, int pct, int respectClosed,
            int time[], int origin[], int parentEdge[])
{
    static char done[MAX_STATIONS];
    MinHeap h;
    int i;

    heap_init(&h, 64);
    for (i = 0; i < m->nStations; i++) {
        time[i] = INF;
        done[i] = 0;
        if (origin) origin[i] = -1;
        if (parentEdge) parentEdge[i] = -1;
    }
    for (i = 0; i < nsrc; i++) {
        if (respectClosed && m->closed[src[i]]) continue;
        time[src[i]] = 0;
        if (origin) origin[src[i]] = src[i];
        heap_push(&h, 0, src[i]);
    }
    while (h.size) {
        HeapItem it = heap_pop(&h);
        int u = it.val;
        AdjNode *a;
        if (done[u]) continue;
        done[u] = 1;
        for (a = m->adj[u]; a; a = a->next) {
            int v = a->to, nd;
            if (respectClosed && m->closed[v]) continue;
            nd = time[u] + (m->ed[a->edge].minutes * pct + 99) / 100;
            if (nd < time[v]) {
                time[v] = nd;
                if (origin) origin[v] = origin[u];
                if (parentEdge) parentEdge[v] = a->edge;
                heap_push(&h, nd, v);
            }
        }
    }
    heap_free(&h);
}

void evacuate(Metro *m, int time[], int exitOf[], int parentEdge[])
{
    static int exits[MAX_STATIONS];
    int i, n = 0;
    for (i = 0; i < m->nStations; i++)
        if (m->st[i].isExit) exits[n++] = i;
    spread(m, exits, n, 100, 1, time, exitOf, parentEdge);
}

/* ---------------- Tarjan: articulation points & bridges ---------------- */
static int disc[MAX_STATIONS], low[MAX_STATIONS], timer_;
int g_recDfs = 0, g_recKruskal = 0;
DfsRecord g_dfsRec;
KruskalRecord g_kruRec;

static void ev(int type, int a, int b, int c)
{
    if (!g_recDfs || g_dfsRec.n >= REC_DFS) return;
    g_dfsRec.ev[g_dfsRec.n][0] = type;
    g_dfsRec.ev[g_dfsRec.n][1] = a;
    g_dfsRec.ev[g_dfsRec.n][2] = b;
    g_dfsRec.ev[g_dfsRec.n][3] = c;
    g_dfsRec.n++;
}

static void tarjan_dfs(Metro *m, int u, int parentEdge, int isArt[], int isBridge[])
{
    AdjNode *a;
    int children = 0;
    disc[u] = low[u] = ++timer_;
    ev(DFS_ENTER, u, parentEdge, disc[u]);
    for (a = m->adj[u]; a; a = a->next) {
        int v = a->to;
        if (m->closed[v] || a->edge == parentEdge) continue;
        if (!disc[v]) {
            children++;
            tarjan_dfs(m, v, a->edge, isArt, isBridge);
            if (low[v] < low[u]) low[u] = low[v];
            ev(DFS_RET, u, v, low[u]);
            if (parentEdge != -1 && low[v] >= disc[u]) { isArt[u] = 1; ev(DFS_ART, u, v, low[v]); }
            if (low[v] > disc[u]) { isBridge[a->edge] = 1; ev(DFS_BRIDGE, a->edge, u, v); }
        } else if (disc[v] < disc[u]) {
            if (disc[v] < low[u]) low[u] = disc[v];   /* back edge to an ancestor */
            ev(DFS_BACK, u, v, low[u]);
        }
    }
    if (parentEdge == -1 && children > 1) { isArt[u] = 1; ev(DFS_ART, u, -1, children); }
}

void find_critical(Metro *m, int isArt[], int isBridge[])
{
    int i;
    memset(disc, 0, sizeof disc);
    memset(low, 0, sizeof low);
    timer_ = 0;
    g_dfsRec.n = 0;
    for (i = 0; i < m->nStations; i++) isArt[i] = 0;
    for (i = 0; i < m->nEdges; i++) isBridge[i] = 0;
    for (i = 0; i < m->nStations; i++)
        if (!disc[i] && !m->closed[i]) tarjan_dfs(m, i, -1, isArt, isBridge);
}

/* ---------------- Kruskal MST ---------------- */
static void merge_sort(int *a, int n, const Edge *ed, int *tmp)
{
    int mid = n / 2, i = 0, j, k = 0;
    if (n < 2) return;
    merge_sort(a, mid, ed, tmp);
    merge_sort(a + mid, n - mid, ed, tmp);
    j = mid;
    while (i < mid && j < n)
        tmp[k++] = ed[a[i]].minutes <= ed[a[j]].minutes ? a[i++] : a[j++];
    while (i < mid) tmp[k++] = a[i++];
    while (j < n)   tmp[k++] = a[j++];
    memcpy(a, tmp, sizeof(int) * n);
}

int kruskal_mst(Metro *m, int inMst[])
{
    static int order[MAX_EDGES], tmp[MAX_EDGES];
    static DSU d;
    int n = 0, i, total = 0;
    for (i = 0; i < m->nEdges; i++) {
        inMst[i] = 0;
        if (!m->closed[m->ed[i].u] && !m->closed[m->ed[i].v]) order[n++] = i;
    }
    merge_sort(order, n, m->ed, tmp);
    dsu_init(&d, m->nStations);
    g_kruRec.n = 0;
    for (i = 0; i < n; i++) {
        int e = order[i], ok = dsu_union(&d, m->ed[e].u, m->ed[e].v);
        if (ok) {                                         /* no cycle => take it */
            inMst[e] = 1;
            total += m->ed[e].minutes;
        }
        if (g_recKruskal && g_kruRec.n < REC_KRU && m->nStations <= 64) {
            int k = g_kruRec.n++, j;
            g_kruRec.edge[k] = e;
            g_kruRec.ok[k] = ok;
            for (j = 0; j < m->nStations; j++) g_kruRec.parent[k][j] = d.parent[j];
        }
    }
    return total;
}

/* ---------------- islands (connected components) ---------------- */
int find_islands(Metro *m, int comp[])
{
    static DSU d;
    int i, count = 0;
    dsu_init(&d, m->nStations);
    for (i = 0; i < m->nEdges; i++)
        if (!m->closed[m->ed[i].u] && !m->closed[m->ed[i].v])
            dsu_union(&d, m->ed[i].u, m->ed[i].v);
    for (i = 0; i < m->nStations; i++) {
        if (m->closed[i]) { comp[i] = -1; continue; }
        comp[i] = dsu_find(&d, i);
        if (comp[i] == i) count++;
    }
    return count;
}

/* stations reachable from the largest island - with station s also closed */
static int stranded_count(Metro *m)
{
    static int comp[MAX_STATIONS], size[MAX_STATIONS];
    int i, open = 0, best = 0;
    memset(size, 0, sizeof size);
    find_islands(m, comp);
    for (i = 0; i < m->nStations; i++)
        if (comp[i] >= 0) { open++; size[comp[i]]++; }
    for (i = 0; i < m->nStations; i++)
        if (size[i] > best) best = size[i];
    return open - best;
}

/* how many MORE stations get cut off from the main network if s closes */
int stranded_if_closed(Metro *m, int s)
{
    int before = stranded_count(m), after, was = m->closed[s];
    m->closed[s] = 1;
    after = stranded_count(m);
    m->closed[s] = was;
    return after - before;
}
