/*
 * TINY HTTP + JSON SERVER  (metro --gui)
 *
 * The browser page is only a renderer: every route, search trace, fire
 * spread, Tarjan scan and benchmark below is computed by this C engine and
 * sent as JSON. Single-threaded, GET only, bound to 127.0.0.1.
 *
 *   /              static files from web/
 *   /api/network   stations, lines, tracks, incidents
 *   /api/route     Dijkstra + A* (with heap pop traces)
 *   /api/simulate  re-time a given path under current delays
 *   /api/alternatives  Yen K-shortest     /api/fewest   BFS
 *   /api/fire      fire spread + naive vs deadline escape
 *   /api/evacuate  multi-source Dijkstra  /api/critical Tarjan
 *   /api/mst       Kruskal                /api/islands  union-find
 *   /api/search    trie                   /api/timeline segment trees
 *   /api/incident /api/undo /api/reset /api/settings   (incident stack)
 *   /api/bench     900-station benchmark
 *   /api/segtree   raw segment-tree nodes + nodes the last update touched
 *   /api/hash      hash-table buckets
 * Route, Tarjan, Kruskal and delay updates also return EXECUTION TRACES
 * recorded inside the C code (heap snapshots, DFS events, union-find
 * parents, touched tree nodes) so the GUI can replay them step by step.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "metro.h"

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

/* ======================= growable string buffer ======================= */
typedef struct { char *s; int len, cap; } Buf;

static void bgrow(Buf *b, int need)
{
    if (b->len + need + 1 <= b->cap) return;
    while (b->len + need + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 4096;
    b->s = (char *)realloc(b->s, b->cap);
}

static void bput(Buf *b, const char *fmt, ...)
{
    va_list ap;
    int n, room;
    bgrow(b, 256);
    for (;;) {
        room = b->cap - b->len;
        va_start(ap, fmt);
        n = vsnprintf(b->s + b->len, room, fmt, ap);
        va_end(ap);
        if (n >= 0 && n < room) { b->len += n; return; }
        bgrow(b, n >= 0 ? n + 1 : room * 2);       /* old MSVCRT returns -1 when full */
    }
}

static void jstr(Buf *b, const char *s)
{
    bput(b, "\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') bput(b, "\\%c", *s);
        else if ((unsigned char)*s < 32) bput(b, " ");
        else bput(b, "%c", *s);
    }
    bput(b, "\"");
}

static void jints(Buf *b, const char *key, const int *a, int n, int infAsMinus1)
{
    int i;
    bput(b, "\"%s\":[", key);
    for (i = 0; i < n; i++) bput(b, "%s%d", i ? "," : "", (infAsMinus1 && a[i] >= INF) ? -1 : a[i]);
    bput(b, "]");
}

/* ======================= query-string helpers ======================= */
static int hexval(int c) { return isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10); }

static int qget(const char *q, const char *key, char *out, int n)
{
    size_t kl = strlen(key);
    while (q && *q) {
        if (strncmp(q, key, kl) == 0 && q[kl] == '=') {
            int i = 0;
            q += kl + 1;
            while (*q && *q != '&' && i < n - 1) {
                if (*q == '%' && isxdigit((unsigned char)q[1]) && isxdigit((unsigned char)q[2])) {
                    out[i++] = (char)(hexval(q[1]) * 16 + hexval(q[2]));
                    q += 3;
                } else { out[i++] = *q == '+' ? ' ' : *q; q++; }
            }
            out[i] = 0;
            return 1;
        }
        q = strchr(q, '&');
        if (q) q++;
    }
    return 0;
}

static int qint(const char *q, const char *key, int def)
{
    char v[32];
    return qget(q, key, v, sizeof v) && v[0] ? atoi(v) : def;
}

static int qstation(Metro *m, const char *q, const char *key)
{
    char v[32];
    char *end;
    long id;
    if (!qget(q, key, v, sizeof v) || !v[0]) return -1;
    id = strtol(v, &end, 10);
    if (*end == '\0' && id >= 0 && id < m->nStations) return (int)id;
    return hash_get(m, v);
}

/* ======================= JSON builders ======================= */
static const char *hex_color(int ansi)
{
    switch (ansi) {
        case 91: case 31: return "#ff5a5f";
        case 94: case 34: return "#4c8dff";
        case 92: case 32: return "#2fd27a";
        case 93: case 33: return "#ffc53d";
        case 95: case 35: return "#b07cff";
        case 96: case 36: return "#2ee6e6";
        default:          return "#c8d0e0";
    }
}

static void j_route(Buf *b, Metro *m, const Route *r, const int *trace, int nt, double us)
{
    int n = r->found ? r->len : 0, ne = n > 0 ? n - 1 : 0;
    (void)m;
    bput(b, "{\"found\":%d,\"total\":%d,\"transfers\":%d,\"explored\":%d,\"us\":%.2f,",
         r->found, r->found ? r->total : -1, r->transfers, r->explored, us);
    jints(b, "stations", r->station, n, 0);
    bput(b, ",");
    jints(b, "edges", r->edge, ne, 0);
    bput(b, ",");
    jints(b, "arrive", r->arrive, n, 0);
    if (trace) { bput(b, ","); jints(b, "trace", trace, nt, 0); }
    bput(b, "}");
}

static void j_network(Buf *b, Metro *m)
{
    int i, l, used, longest;
    AdjNode *a;
    int adj = 0;
    hash_stats(m, &used, &longest);
    for (i = 0; i < m->nStations; i++) for (a = m->adj[i]; a; a = a->next) adj++;

    bput(b, "{\"ok\":1,\"clock\":%d,\"transfer\":%d,\"lines\":[", g_clock, m->transferPenalty);
    for (l = 0; l < m->nLines; l++) {
        bput(b, "%s{\"key\":\"%c\",\"name\":", l ? "," : "", m->ln[l].key);
        jstr(b, m->ln[l].name);
        bput(b, ",\"color\":\"%s\",\"delayNow\":%d}", hex_color(m->ln[l].color), delay_at(m, l, g_clock));
    }
    bput(b, "],\"stations\":[");
    for (i = 0; i < m->nStations; i++) {
        const Station *s = &m->st[i];
        bput(b, "%s{\"id\":%d,\"code\":\"%s\",\"name\":", i ? "," : "", i, s->code);
        jstr(b, s->name);
        bput(b, ",\"x\":%d,\"y\":%d,\"lx\":%d,\"ly\":%d,\"exit\":%d,\"closed\":%d,\"lines\":[",
             s->x, s->y, s->lx, s->ly, s->isExit, m->closed[i]);
        for (l = 0, used = 0; l < m->nLines; l++)
            if (s->lineMask & (1 << l)) bput(b, "%s%d", used++ ? "," : "", l);
        bput(b, "]}");
    }
    bput(b, "],\"edges\":[");
    for (i = 0; i < m->nEdges; i++)
        bput(b, "%s{\"u\":%d,\"v\":%d,\"line\":%d,\"min\":%d}", i ? "," : "",
             m->ed[i].u, m->ed[i].v, m->ed[i].line, m->ed[i].minutes);
    bput(b, "],\"incidents\":[");
    for (i = 0; i < incident_count(); i++) {
        const Incident *in = incident_at(i);
        if (in->type == INC_CLOSE) bput(b, "%s{\"type\":\"close\",\"station\":%d}", i ? "," : "", in->station);
        else bput(b, "%s{\"type\":\"delay\",\"line\":%d,\"from\":%d,\"to\":%d,\"amount\":%d}", i ? "," : "",
                  in->line, in->from, in->to + 1, in->amount);
    }
    hash_stats(m, &used, &longest);
    bput(b, "],\"engine\":{\"hashBuckets\":%d,\"hashUsed\":%d,\"hashLongest\":%d,\"trieNodes\":%d,"
            "\"adjEntries\":%d,\"states\":%d,\"segNodes\":%d,\"minPerUnit\":%.3f}}",
         HASH_SIZE, used, longest, trie_count(m->trie), adj, m->nStations * NL1, 4 * DAY_MIN, m->minPerUnit);
}

/* run a search many times for a stable microsecond timing, then once for the trace */
static double timed_search(Metro *m, int a, int b, int astar, Route *r, int *trace, int *nt)
{
    int i, reps = 200;
    double t0 = now_ms(), us;
    for (i = 0; i < reps; i++) route_search(m, a, b, g_clock, NO_LINE, astar, NULL, r);
    us = (now_ms() - t0) * 1000.0 / reps;
    g_recHeap = 1;                               /* the traced run also records heap snapshots */
    route_search(m, a, b, g_clock, NO_LINE, astar, NULL, r);
    g_recHeap = 0;
    *nt = g_traceLen;
    memcpy(trace, g_trace, sizeof(int) * g_traceLen);
    return us;
}

static void j_heap(Buf *b)
{
    int k, i;
    bput(b, "[");
    for (k = 0; k < g_heapRec.nSteps; k++) {
        int ps = g_heapRec.popState[k];
        bput(b, "%s[%d,%d,%d,[", k ? "," : "", g_heapRec.popKey[k], ps / NL1, ps % NL1 == NO_LINE ? -1 : ps % NL1);
        for (i = 0; i < g_heapRec.len[k]; i++) {
            const int *it = g_heapRec.item[g_heapRec.start[k] + i];
            bput(b, "%s[%d,%d,%d,%d]", i ? "," : "", it[0], it[1] / NL1, it[1] % NL1 == NO_LINE ? -1 : it[1] % NL1, it[2]);
        }
        bput(b, "]]");
    }
    bput(b, "]");
}

/* segment-tree node ranges, depth-limited, for the viewer */
static void j_segnodes(Buf *b, SegTree *t, int node, int lo, int hi, int depth, int *first)
{
    int mid = (lo + hi) / 2;
    bput(b, "%s[%d,%d,%d,%d,%d]", *first ? "" : ",", node, lo, hi, t->mx[node], t->lz[node]);
    *first = 0;
    if (depth == 0 || lo == hi) return;
    j_segnodes(b, t, 2 * node, lo, mid, depth - 1, first);
    j_segnodes(b, t, 2 * node + 1, mid + 1, hi, depth - 1, first);
}

static SegRecord lastSeg[MAX_LINES];              /* nodes touched by each line's last delay update */

/* ======================= API endpoints ======================= */
static int api(Metro *m, const char *ep, const char *q, Buf *b)
{
    static Route r1, r2, alt[5];
    static int t1[NSTATE], t2[NSTATE];
    static int arr1[MAX_STATIONS], arr2[MAX_STATIONS], arr3[MAX_STATIONS], earr[MAX_EDGES];
    static FirePlan fp;
    int a, z, n1, n2, i;
    double us, t0;

    if (!strcmp(ep, "network")) { j_network(b, m); return 1; }

    if (!strcmp(ep, "route") || !strcmp(ep, "fewest")) {
        if ((a = qstation(m, q, "from")) < 0 || (z = qstation(m, q, "to")) < 0) return 0;
        if (!strcmp(ep, "fewest")) {
            t0 = now_ms();
            route_fewest_stops(m, a, z, g_clock, &r1);
            us = (now_ms() - t0) * 1000.0;
            route_search(m, a, z, g_clock, NO_LINE, 1, NULL, &r2);
            bput(b, "{\"bfs\":");
            j_route(b, m, &r1, NULL, 0, us);
            bput(b, ",\"fastest\":");
            j_route(b, m, &r2, NULL, 0, 0);
            bput(b, "}");
            return 1;
        }
        us = timed_search(m, a, z, 0, &r1, t1, &n1);
        bput(b, "{\"dijkstra\":");
        j_route(b, m, &r1, t1, n1, us);
        bput(b, ",\"dijkstraHeap\":");
        j_heap(b);
        us = timed_search(m, a, z, 1, &r2, t2, &n2);
        bput(b, ",\"astar\":");
        j_route(b, m, &r2, t2, n2, us);
        bput(b, ",\"astarHeap\":");
        j_heap(b);
        bput(b, "}");
        return 1;
    }

    if (!strcmp(ep, "simulate")) {               /* re-time an explicit path under current delays */
        char list[1024], *p;
        if ((a = qstation(m, q, "from")) < 0 || !qget(q, "edges", list, sizeof list)) return 0;
        r1.len = 1; r1.station[0] = a;
        for (p = strtok(list, ","); p && r1.len < MAX_PATH; p = strtok(NULL, ",")) {
            int e = atoi(p), cur = r1.station[r1.len - 1];
            if (e < 0 || e >= m->nEdges || (m->ed[e].u != cur && m->ed[e].v != cur)) return 0;
            r1.edge[r1.len - 1] = e;
            r1.station[r1.len++] = edge_other(m, e, cur);
        }
        route_simulate(m, &r1, g_clock);
        r1.found = 1;
        r1.explored = 0;
        j_route(b, m, &r1, NULL, 0, 0);
        return 1;
    }

    if (!strcmp(ep, "alternatives")) {
        int k = qint(q, "k", 3);
        if ((a = qstation(m, q, "from")) < 0 || (z = qstation(m, q, "to")) < 0) return 0;
        if (k < 1) k = 1;
        if (k > 5) k = 5;
        t0 = now_ms();
        n1 = route_k_shortest(m, a, z, g_clock, k, alt);
        us = (now_ms() - t0) * 1000.0;
        bput(b, "{\"us\":%.1f,\"routes\":[", us);
        for (i = 0; i < n1; i++) { if (i) bput(b, ","); j_route(b, m, &alt[i], NULL, 0, 0); }
        bput(b, "]}");
        return 1;
    }

    if (!strcmp(ep, "fire")) {
        int pct = qint(q, "pct", 140), fire = qstation(m, q, "fire"), me = qstation(m, q, "me");
        if (pct < 50) pct = 50;
        if (pct > 400) pct = 400;
        if (fire < 0 || me < 0 || fire == me)
            if (!find_fire_scenario(m, pct, g_clock, &me, &fire)) return 0;
        t0 = now_ms();
        fire_compute(m, me, fire, pct, g_clock, &fp);
        us = (now_ms() - t0) * 1000.0;
        bput(b, "{\"fire\":%d,\"me\":%d,\"pct\":%d,\"us\":%.1f,\"margin\":%d,\"shelter\":%d,\"naiveCaught\":%.2f,",
             fire, me, pct, us, fp.margin, fp.shelter, fp.naiveCaught >= INF ? -1.0 : fp.naiveCaught);
        jints(b, "burn", fp.burn, m->nStations, 1);
        bput(b, ",\"naive\":");
        j_route(b, m, &fp.naive, NULL, 0, 0);
        bput(b, ",\"smart\":");
        j_route(b, m, &fp.smart, NULL, 0, 0);
        bput(b, "}");
        return 1;
    }

    if (!strcmp(ep, "evacuate")) {
        t0 = now_ms();
        evacuate(m, arr1, arr2, arr3);
        us = (now_ms() - t0) * 1000.0;
        bput(b, "{\"us\":%.1f,", us);
        jints(b, "time", arr1, m->nStations, 1);
        bput(b, ",");
        jints(b, "exitOf", arr2, m->nStations, 0);
        bput(b, ",");
        jints(b, "parentEdge", arr3, m->nStations, 0);
        bput(b, "}");
        return 1;
    }

    if (!strcmp(ep, "critical")) {
        MinHeap h;
        int first = 1;
        t0 = now_ms();
        find_critical(m, arr1, earr);
        us = (now_ms() - t0) * 1000.0;
        bput(b, "{\"us\":%.1f,\"impact\":[", us);
        heap_init(&h, 16);
        for (i = 0; i < m->nStations; i++)
            if (arr1[i]) heap_push(&h, -stranded_if_closed(m, i), i);   /* max-heap by impact */
        while (h.size) {
            HeapItem it = heap_pop(&h);
            bput(b, "%s{\"station\":%d,\"stranded\":%d}", first ? "" : ",", it.val, -it.key);
            first = 0;
        }
        heap_free(&h);
        bput(b, "],\"bridges\":[");
        for (i = 0, first = 1; i < m->nEdges; i++)
            if (earr[i]) { bput(b, "%s%d", first ? "" : ",", i); first = 0; }
        g_recDfs = 1;                                     /* replayable DFS: enter / back / return events */
        find_critical(m, arr1, earr);
        g_recDfs = 0;
        bput(b, "],\"dfs\":[");
        for (i = 0; i < g_dfsRec.n; i++)
            bput(b, "%s[%d,%d,%d,%d]", i ? "," : "", g_dfsRec.ev[i][0], g_dfsRec.ev[i][1], g_dfsRec.ev[i][2], g_dfsRec.ev[i][3]);
        bput(b, "]}");
        return 1;
    }

    if (!strcmp(ep, "mst")) {
        int total, all = 0;
        t0 = now_ms();
        total = kruskal_mst(m, earr);
        us = (now_ms() - t0) * 1000.0;
        for (i = 0; i < m->nEdges; i++)
            if (!m->closed[m->ed[i].u] && !m->closed[m->ed[i].v]) all += m->ed[i].minutes;
        bput(b, "{\"us\":%.1f,\"total\":%d,\"all\":%d,", us, total, all);
        jints(b, "inMst", earr, m->nEdges, 0);
        g_recKruskal = 1;                                 /* replay: each edge in sorted order + DSU parents */
        kruskal_mst(m, earr);
        g_recKruskal = 0;
        bput(b, ",\"steps\":[");
        for (i = 0; i < g_kruRec.n; i++) {
            int j;
            bput(b, "%s[%d,%d,[", i ? "," : "", g_kruRec.edge[i], g_kruRec.ok[i]);
            for (j = 0; j < m->nStations; j++) bput(b, "%s%d", j ? "," : "", g_kruRec.parent[i][j]);
            bput(b, "]]");
        }
        bput(b, "]}");
        return 1;
    }

    if (!strcmp(ep, "islands")) {
        t0 = now_ms();
        n1 = find_islands(m, arr1);
        us = (now_ms() - t0) * 1000.0;
        bput(b, "{\"us\":%.1f,\"count\":%d,", us, n1);
        jints(b, "comp", arr1, m->nStations, 0);
        bput(b, "}");
        return 1;
    }

    if (!strcmp(ep, "search")) {
        char text[64];
        if (!qget(q, "q", text, sizeof text)) text[0] = 0;
        t0 = now_ms();
        n1 = text[0] ? trie_collect(m->trie, text, arr1, MAX_STATIONS) : 0;
        z = text[0] ? hash_get(m, text) : -1;
        us = (now_ms() - t0) * 1000.0;
        bput(b, "{\"us\":%.2f,\"exact\":%d,", us, z);
        jints(b, "ids", arr1, n1, 0);
        {
            static TrieNode *path[64];
            char chars[64];
            int np = text[0] ? trie_walk(m->trie, text, path, chars, 64) : 0, c, f;
            bput(b, ",\"bucket\":%d,\"path\":[", text[0] ? (int)hash_bucket(text) : -1);
            for (i = 0; i < np; i++) {
                bput(b, "%s{\"ch\":\"%c\",\"ids\":%d,\"kids\":\"", i ? "," : "", chars[i] ? chars[i] : '^', path[i]->nids);
                for (c = 0; c < TRIE_ALPHA; c++) if (path[i]->child[c]) bput(b, "%c", trie_char(c) == ' ' ? '_' : trie_char(c));
                bput(b, "\"}");
            }
            bput(b, "],\"next\":[");
            for (c = 0, f = 1; np > 0 && c < TRIE_ALPHA; c++)
                if (path[np - 1]->child[c]) {
                    bput(b, "%s{\"ch\":\"%c\",\"size\":%d}", f ? "" : ",", trie_char(c) == ' ' ? '_' : trie_char(c),
                         trie_count(path[np - 1]->child[c]));
                    f = 0;
                }
            bput(b, "]");
        }
        bput(b, "}");
        return 1;
    }

    if (!strcmp(ep, "timeline")) {
        int l, h;
        bput(b, "{\"lines\":[");
        for (l = 0; l < m->nLines; l++) {
            bput(b, "%s{\"peak\":%d,\"now\":%d,\"hours\":[", l ? "," : "",
                 seg_max(&m->delay[l], 0, DAY_MIN - 1), delay_at(m, l, g_clock));
            for (h = 0; h < 24; h++) bput(b, "%s%d", h ? "," : "", seg_max(&m->delay[l], h * 60, h * 60 + 59));
            bput(b, "]}");
        }
        bput(b, "]}");
        return 1;
    }

    if (!strcmp(ep, "incident")) {
        char type[16], key[8];
        Incident in;
        memset(&in, 0, sizeof in);
        if (!qget(q, "type", type, sizeof type)) return 0;
        if (!strcmp(type, "close")) {
            in.type = INC_CLOSE;
            in.station = qstation(m, q, "station");
        } else {
            int to;
            in.type = INC_DELAY;
            in.line = qget(q, "line", key, sizeof key) ? line_by_key(m, key[0]) : -1;
            in.from = qint(q, "from", -1);
            to = qint(q, "to", -1);
            in.amount = qint(q, "amount", 0);
            if (in.from < 0 || in.from >= DAY_MIN || to < 0 || to > DAY_MIN || to == in.from || in.amount <= 0) {
                bput(b, "{\"ok\":0,\"error\":\"invalid delay window or amount\"}");
                return 1;
            }
            in.to = (to + DAY_MIN - 1) % DAY_MIN;          /* exclusive -> inclusive */
            if (in.amount > 120) in.amount = 120;
        }
        g_segRec.n = 0;
        g_recSeg = 1;
        if (!incident_push(m, in)) { g_recSeg = 0; bput(b, "{\"ok\":0,\"error\":\"could not apply incident\"}"); return 1; }
        g_recSeg = 0;
        if (in.type == INC_DELAY) lastSeg[in.line] = g_segRec;
        j_network(b, m);
        return 1;
    }

    if (!strcmp(ep, "undo"))  { incident_pop(m, NULL); j_network(b, m); return 1; }
    if (!strcmp(ep, "reset")) { while (incident_pop(m, NULL)) ; g_clock = 8 * 60 + 30; m->transferPenalty = 4;
                                j_network(b, m); return 1; }
    if (!strcmp(ep, "settings")) {
        int c = qint(q, "clock", g_clock), t = qint(q, "transfer", m->transferPenalty);
        if (c >= 0 && c < DAY_MIN) g_clock = c;
        if (t >= 0 && t <= 60) m->transferPenalty = t;
        j_network(b, m);
        return 1;
    }

    if (!strcmp(ep, "segtree")) {
        char key[8];
        int l = qget(q, "line", key, sizeof key) ? line_by_key(m, key[0]) : -1, first = 1, depth = qint(q, "depth", 4);
        if (l < 0) return 0;
        if (depth < 1) depth = 1;
        if (depth > 6) depth = 6;
        bput(b, "{\"line\":%d,\"n\":%d,\"clock\":%d,\"nodes\":[", l, DAY_MIN, g_clock);
        j_segnodes(b, &m->delay[l], 1, 0, DAY_MIN - 1, depth, &first);
        bput(b, "],\"touched\":[");
        for (i = 0; i < lastSeg[l].n; i++) bput(b, "%s[%d,%d]", i ? "," : "", lastSeg[l].node[i], lastSeg[l].full[i]);
        bput(b, "]}");
        return 1;
    }

    if (!strcmp(ep, "hash")) {
        int bk;
        bput(b, "{\"size\":%d,\"buckets\":[", HASH_SIZE);
        for (bk = 0; bk < HASH_SIZE; bk++) {
            HashEntry *e;
            bput(b, "%s[", bk ? "," : "");
            for (e = m->table[bk], i = 0; e; e = e->next, i++) bput(b, "%s\"%s\"", i ? "," : "", e->key);
            bput(b, "]");
        }
        bput(b, "]}");
        return 1;
    }

    if (!strcmp(ep, "bench")) {
        BenchResult R;
        bench_compute(&R);
        bput(b, "{\"stations\":%d,\"edges\":%d,\"states\":%d,\"expD\":%.1f,\"expA\":%.1f,\"msD\":%.2f,\"msA\":%.2f,"
                "\"mismatch\":%d,\"msArray\":%.2f,\"msHeap\":%.2f,\"sameDist\":%d,\"msYen\":%.3f,"
                "\"msTarjan\":%.4f,\"msKruskal\":%.4f,\"msDsu\":%.4f,\"nsHash\":%.1f,\"nsTrie\":%.1f,"
                "\"nArt\":%d,\"mstCost\":%d}",
             R.stations, R.edges, R.states, R.expD, R.expA, R.msD, R.msA, R.mismatch, R.msArray, R.msHeap,
             R.sameDist, R.msYen, R.msTarjan, R.msKruskal, R.msDsu, R.nsHash, R.nsTrie, R.nArt, R.mstCost);
        return 1;
    }
    return 0;
}

/* ======================= HTTP plumbing ======================= */
static char g_web[256] = "web";

static void send_all(sock_t c, const char *p, int n)
{
    while (n > 0) {
        int k = send(c, p, n, 0);
        if (k <= 0) return;
        p += k;
        n -= k;
    }
}

static void respond(sock_t c, int code, const char *type, const char *body, int len)
{
    char head[256];
    int hl = sprintf(head, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                           "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                     code, code == 200 ? "OK" : code == 404 ? "Not Found" : "Bad Request", type, len);
    send_all(c, head, hl);
    send_all(c, body, len);
}

static const char *mime(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".js"))   return "application/javascript; charset=utf-8";
    if (!strcmp(dot, ".css"))  return "text/css; charset=utf-8";
    if (!strcmp(dot, ".svg"))  return "image/svg+xml";
    if (!strcmp(dot, ".png"))  return "image/png";
    return "application/octet-stream";
}

static void serve_file(sock_t c, const char *path)
{
    char full[2560];
    FILE *f;
    long n;
    char *data;
    if (path[0] != '/' || strstr(path, "..")) { respond(c, 400, "text/plain", "bad path", 8); return; }
    if (!strcmp(path, "/")) path = "/index.html";
    if (snprintf(full, sizeof full, "%s%s", g_web, path) >= (int)sizeof full) {
        respond(c, 400, "text/plain", "path too long", 13);
        return;
    }
    f = fopen(full, "rb");
    if (!f) { respond(c, 404, "text/plain", "not found", 9); return; }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (char *)malloc(n > 0 ? n : 1);
    n = (long)fread(data, 1, n, f);
    fclose(f);
    respond(c, 200, mime(full), data, (int)n);
    free(data);
}

static void handle(sock_t c, Metro *m)
{
    static Buf body;
    char req[8192], method[8], target[2048], *q;
    int len = 0, k;
    while (len < (int)sizeof req - 1) {               /* read until end of headers */
        k = recv(c, req + len, (int)sizeof req - 1 - len, 0);
        if (k <= 0) break;
        len += k;
        req[len] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    if (len <= 0) return;
    req[len] = 0;
    if (sscanf(req, "%7s %2047s", method, target) != 2 || strcmp(method, "GET")) {
        respond(c, 400, "text/plain", "GET only", 8);
        return;
    }
    q = strchr(target, '?');
    if (q) *q++ = 0;
    if (strncmp(target, "/api/", 5) == 0) {
        body.len = 0;
        if (api(m, target + 5, q ? q : "", &body)) respond(c, 200, "application/json", body.s, body.len);
        else respond(c, 400, "application/json", "{\"ok\":0,\"error\":\"bad request\"}", 31);
        return;
    }
    serve_file(c, target);
}

int server_run(Metro *m, int port, int openBrowser)
{
    struct sockaddr_in addr;
    sock_t s;
    int tries;
    FILE *probe;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }
#endif
    if ((probe = fopen("web/index.html", "rb"))) fclose(probe);
    else if ((probe = fopen("../web/index.html", "rb"))) { fclose(probe); strcpy(g_web, "../web"); }
    else { fprintf(stderr, "Can't find web/index.html - run from the project folder.\n"); return 1; }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { fprintf(stderr, "socket() failed\n"); return 1; }
    {
        int yes = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);         /* 127.0.0.1 only */
    for (tries = 0; tries < 10; tries++, port++) {
        addr.sin_port = htons((unsigned short)port);
        if (bind(s, (struct sockaddr *)&addr, sizeof addr) == 0) break;
    }
    if (tries == 10 || listen(s, 16) != 0) { fprintf(stderr, "Could not open a local port.\n"); return 1; }

    printf("\n  ");
    cprintf(92, "Metro Escape GUI running at  http://localhost:%d/", port);
    printf("\n  Every route and simulation is computed by this C engine. Press Ctrl+C to stop.\n\n");
    fflush(stdout);
    if (openBrowser) {
        char cmd[128];
#ifdef _WIN32
        sprintf(cmd, "start \"\" \"http://localhost:%d/\"", port);
#else
        sprintf(cmd, "xdg-open http://localhost:%d/ >/dev/null 2>&1 &", port);
#endif
        if (system(cmd) != 0) printf("  Open http://localhost:%d/ in your browser.\n", port);
    }

    for (;;) {
        sock_t c = accept(s, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        {
#ifdef _WIN32
            DWORD tmo = 2000;                         /* don't let an idle socket block us */
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
#else
            struct timeval tv = { 2, 0 };
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
        }
        handle(c, m);
        CLOSESOCK(c);
    }
    return 0;
}
