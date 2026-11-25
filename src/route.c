/*
 * ROUTING ALGORITHMS
 *
 * Search runs on an expanded "state graph": state = (station, line you
 * arrived on). That lets us charge a transfer penalty when changing lines
 * and model time-dependent delays (looked up in each line's segment tree).
 *
 *   route_search       Dijkstra / A*  with binary heap    O((V+E) log V)
 *   route_escape       Dijkstra with DEADLINES: every station must be
 *                      reached before the fire does; target = any exit
 *   route_fewest_stops BFS with a circular queue          O(V+E)
 *   route_k_shortest   Yen's algorithm, K loopless paths  O(K * V * Dijkstra)
 */
#include <string.h>
#include "metro.h"

#define MAX_CAND 64

int g_trace[NSTATE];
int g_traceLen;
int g_recHeap = 0;
HeapRecord g_heapRec;

int delay_at(Metro *m, int line, int clock)
{
    clock %= DAY_MIN;
    return seg_max(&m->delay[line], clock, clock);
}

/*
 * Minutes to traverse edge e when currently riding curLine at `clock`.
 * If a delay window is active the passenger may WAIT for it to clear:
 *     cost = min over w >= 0 of ( w + delay(t + w) )
 * This makes arrival time non-decreasing in departure time (the FIFO
 * property), which is exactly what Dijkstra needs to stay optimal on a
 * time-dependent graph. Waiting longer than delay(t) never helps.
 */
static int edge_step(Metro *m, int e, int curLine, int clock, int maxLeave, int *waitOut)
{
    int line = m->ed[e].line;
    int c = 0, t, best, bestW = 0, w, limit;
    if (curLine != NO_LINE && curLine != line) c += m->transferPenalty;
    if (maxLeave >= 0 && c >= maxLeave) return INF;
    t = clock + c;
    best = delay_at(m, line, t);
    limit = best;
    if (maxLeave >= 0 && maxLeave - c < limit) limit = maxLeave - c;
    for (w = 1; w < limit; w++) {
        int d = w + delay_at(m, line, t + w);
        if (d < best) { best = d; bestW = w; }
    }
    if (waitOut) *waitOut = c + bestW;
    return c + m->ed[e].minutes + best;
}

int edge_cost(Metro *m, int e, int curLine, int clock)
{
    return edge_step(m, e, curLine, clock, -1, NULL);
}

/* fill arrive[], total, transfers from station[]/edge[] */
void route_simulate(Metro *m, Route *r, int depart)
{
    int i, cl = NO_LINE, t = 0;
    r->transfers = 0;
    r->arrive[0] = depart;
    for (i = 0; i + 1 < r->len; i++) {
        int L = m->ed[r->edge[i]].line;
        t += edge_cost(m, r->edge[i], cl, depart + t);
        if (cl != NO_LINE && cl != L) r->transfers++;
        cl = L;
        r->arrive[i + 1] = depart + t;
    }
    r->total = t;
}

static int blocked(Metro *m, const Bans *b, int s)
{
    return m->closed[s] || (b && b->bannedNode[s]);
}

static int heuristic(Metro *m, int u, int dst)
{
    return (int)(station_dist(m, u, dst) * m->minPerUnit);   /* floor => admissible */
}

/* X-ray: copy the whole heap array after a pop; entries that are already
   settled or superseded by a cheaper push are "stale" (lazy deletion) */
static void rec_heap(Metro *m, const MinHeap *h, HeapItem popped, const char *done, const int *dist,
                     int astar, int dst)
{
    HeapRecord *R = &g_heapRec;
    int i, k = R->nSteps;
    if (k >= REC_STEPS) return;
    R->popKey[k] = popped.key;
    R->popState[k] = popped.val;
    R->start[k] = R->nItems;
    for (i = 0; i < h->size && R->nItems < REC_ITEMS; i++) {
        int s = h->data[i].val;
        int expect = dist[s] + (astar ? (int)(station_dist(m, s / NL1, dst) * m->minPerUnit) : 0);
        R->item[R->nItems][0] = h->data[i].key;
        R->item[R->nItems][1] = s;
        R->item[R->nItems][2] = done[s] || h->data[i].key != expect;
        R->nItems++;
    }
    R->len[k] = R->nItems - R->start[k];
    R->nSteps++;
}

/*
 * The one search engine behind Dijkstra, A*, Yen spurs and fire escape.
 *   dst == ANY_EXIT  -> stop at the first open exit popped
 *   deadline != NULL -> station v may only be used if we arrive strictly
 *                       before deadline[v] (minutes after depart)
 */
static int search(Metro *m, int src, int dst, int depart, int startLine, int useAstar,
                  const Bans *bans, const int *deadline, Route *out)
{
    static int  dist[NSTATE], parent[NSTATE], pedge[NSTATE];
    static char done[NSTATE];
    static int  stack[MAX_PATH + 1];
    MinHeap h;
    int s, goal = -1, n, i, nstates = m->nStations * NL1;

    out->found = 0;
    out->explored = 0;
    out->len = 0;
    g_traceLen = 0;
    if (dst == ANY_EXIT) useAstar = 0;            /* no single target to aim at */
    if (blocked(m, bans, src) || (dst >= 0 && m->closed[dst])) return 0;
    if (deadline && deadline[src] <= 0) return 0;

    for (i = 0; i < nstates; i++) { dist[i] = INF; parent[i] = -1; pedge[i] = -1; done[i] = 0; }
    if (g_recHeap) g_heapRec.nSteps = g_heapRec.nItems = 0;
    heap_init(&h, 64);
    s = src * NL1 + startLine;
    dist[s] = 0;
    heap_push(&h, useAstar ? heuristic(m, src, dst) : 0, s);

    while (h.size) {
        HeapItem it = heap_pop(&h);
        AdjNode *a;
        int u, cl;
        s = it.val;
        if (done[s]) continue;                 /* stale heap entry (lazy deletion) */
        done[s] = 1;
        out->explored++;
        u = s / NL1;
        cl = s % NL1;
        g_trace[g_traceLen++] = u;
        if (dst == ANY_EXIT ? m->st[u].isExit : u == dst) {
            goal = s;
            if (g_recHeap) rec_heap(m, &h, it, done, dist, useAstar, dst);
            break;
        }

        for (a = m->adj[u]; a; a = a->next) {
            int e = a->edge, v = a->to, ns, nd;
            if (blocked(m, bans, v) || (bans && bans->bannedEdge[e])) continue;
            ns = v * NL1 + m->ed[e].line;
            if (done[ns]) continue;
            if (deadline) {
                int leave = 0;
                int step = edge_step(m, e, cl, depart + dist[s], deadline[u] - dist[s], &leave);
                if (step >= INF || dist[s] + leave >= deadline[u]) continue;
                nd = dist[s] + step;
                if (nd >= deadline[v]) continue;   /* fire gets there first */
            } else {
                nd = dist[s] + edge_cost(m, e, cl, depart + dist[s]);
            }
            if (nd < dist[ns]) {               /* relax */
                dist[ns] = nd;
                parent[ns] = s;
                pedge[ns] = e;
                heap_push(&h, nd + (useAstar ? heuristic(m, v, dst) : 0), ns);
            }
        }
        if (g_recHeap) rec_heap(m, &h, it, done, dist, useAstar, dst);
    }
    heap_free(&h);
    if (goal < 0) return 0;

    /* walk parents back to the source, then reverse via a stack */
    n = 0;
    for (s = goal; s != -1 && n <= MAX_PATH; s = parent[s]) stack[n++] = s;
    if (n > MAX_PATH) return 0;
    out->len = n;
    for (i = 0; i < n; i++) {
        int st = stack[n - 1 - i];
        out->station[i] = st / NL1;
        if (i + 1 < n) out->edge[i] = pedge[stack[n - 2 - i]];
    }
    route_simulate(m, out, depart);
    out->found = 1;
    return 1;
}

int route_search(Metro *m, int src, int dst, int depart, int startLine,
                 int useAstar, const Bans *bans, Route *out)
{
    return search(m, src, dst, depart, startLine, useAstar, bans, NULL, out);
}

/* nearest exit; with a deadline array it only uses stations not yet on fire */
int route_escape(Metro *m, int src, int depart, const int *deadline, Route *out)
{
    return search(m, src, ANY_EXIT, depart, NO_LINE, 0, NULL, deadline, out);
}

int route_fewest_stops(Metro *m, int src, int dst, int depart, Route *out)
{
    static int queue[MAX_STATIONS], pedge[MAX_STATIONS], path[MAX_STATIONS];
    static char seen[MAX_STATIONS];
    int head = 0, tail = 0, n = 0, u, i;

    out->found = 0;
    out->explored = 0;
    out->len = 0;
    if (m->closed[src] || m->closed[dst]) return 0;

    memset(seen, 0, sizeof seen);
    seen[src] = 1;
    pedge[src] = -1;
    queue[tail++] = src;
    while (head < tail) {
        AdjNode *a;
        u = queue[head++];
        out->explored++;
        if (u == dst) break;
        for (a = m->adj[u]; a; a = a->next) {
            if (seen[a->to] || m->closed[a->to]) continue;
            seen[a->to] = 1;
            pedge[a->to] = a->edge;
            queue[tail++] = a->to;
        }
    }
    if (!seen[dst]) return 0;

    for (u = dst; u != src; u = edge_other(m, pedge[u], u)) path[n++] = u;
    path[n++] = src;
    if (n > MAX_PATH) return 0;
    out->len = n;
    for (i = 0; i < n; i++) out->station[i] = path[n - 1 - i];
    for (i = 0; i + 1 < n; i++) out->edge[i] = pedge[out->station[i + 1]];
    route_simulate(m, out, depart);
    out->found = 1;
    return 1;
}

/* ---------------- Yen's K shortest loopless paths ---------------- */
static int same_prefix(const Route *a, const Route *b, int i)
{
    int k;
    if (a->len - 1 <= i || b->len - 1 <= i) return 0;
    for (k = 0; k < i; k++)
        if (a->edge[k] != b->edge[k]) return 0;
    return 1;
}

static int same_route(const Route *a, const Route *b)
{
    int k;
    if (a->len != b->len) return 0;
    for (k = 0; k + 1 < a->len; k++)
        if (a->edge[k] != b->edge[k]) return 0;
    return 1;
}

int route_k_shortest(Metro *m, int src, int dst, int depart, int K, Route A[])
{
    static Route B[MAX_CAND], spur, cand;
    static Bans bans;
    int used[MAX_CAND];
    int nB = 0, k, i, j;
    MinHeap h;

    if (!route_search(m, src, dst, depart, NO_LINE, 1, NULL, &A[0])) return 0;
    heap_init(&h, 16);

    for (k = 1; k < K; k++) {
        Route *prev = &A[k - 1];
        int got = 0;

        /* every node of the previous path is tried as a "spur" node */
        for (i = 0; i + 1 < prev->len; i++) {
            int dup = 0, startLine;
            memset(&bans, 0, sizeof bans);
            for (j = 0; j < k; j++)            /* block edges already used by same root */
                if (same_prefix(&A[j], prev, i)) bans.bannedEdge[A[j].edge[i]] = 1;
            for (j = 0; j < i; j++)            /* root nodes can't be revisited */
                bans.bannedNode[prev->station[j]] = 1;

            startLine = i > 0 ? m->ed[prev->edge[i - 1]].line : NO_LINE;
            if (!route_search(m, prev->station[i], dst, prev->arrive[i], startLine, 1, &bans, &spur))
                continue;
            if (i + spur.len > MAX_PATH) continue;

            cand.len = i + spur.len;           /* root + spur */
            for (j = 0; j < i; j++) { cand.station[j] = prev->station[j]; cand.edge[j] = prev->edge[j]; }
            for (j = 0; j < spur.len; j++) {
                cand.station[i + j] = spur.station[j];
                if (j + 1 < spur.len) cand.edge[i + j] = spur.edge[j];
            }
            route_simulate(m, &cand, depart);
            cand.found = 1;
            cand.explored = 0;

            for (j = 0; j < k && !dup; j++)  dup = same_route(&A[j], &cand);
            for (j = 0; j < nB && !dup; j++) dup = same_route(&B[j], &cand);
            if (dup || nB >= MAX_CAND) continue;
            B[nB] = cand;
            used[nB] = 0;
            heap_push(&h, cand.total, nB);
            nB++;
        }

        while (h.size) {                       /* cheapest unused candidate */
            HeapItem it = heap_pop(&h);
            if (used[it.val]) continue;
            used[it.val] = 1;
            A[k] = B[it.val];
            got = 1;
            break;
        }
        if (!got) break;
    }
    heap_free(&h);
    return k;
}
