/*
 * SCALE BENCHMARK
 * Generates a 30 x 30 grid city (900 stations) in memory and measures the
 * same algorithms the planner uses - showing why the data structures matter.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "metro.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define GRID 30

static Metro B;

double now_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    return (double)clock() * 1000.0 / CLOCKS_PER_SEC;
#endif
}

static int gid(int r, int c) { return r * GRID + c; }

static void build_grid(Metro *m)
{
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const int colors[] = { 91, 94, 92, 93, 95 };
    int r, c, i;
    metro_init(m);
    for (i = 0; i < 5; i++) {
        m->ln[i].key = (char)('1' + i);
        sprintf(m->ln[i].name, "L%d", i + 1);
        m->ln[i].color = colors[i];
    }
    m->nLines = 5;
    for (r = 0; r < GRID; r++)
        for (c = 0; c < GRID; c++) {
            int id = gid(r, c);
            char code[4], name[NAME_LEN];
            code[0] = digits[id / 1296]; code[1] = digits[id / 36 % 36]; code[2] = digits[id % 36]; code[3] = 0;
            sprintf(name, "Block %d-%d", r, c);
            metro_add_station(m, code, name, c * 3, r * 2, 1, 0);
        }
    srand(42);
    for (r = 0; r < GRID; r++)                    /* east-west lines on every street */
        for (c = 0; c + 1 < GRID; c++)
            metro_add_edge(m, r % 4, gid(r, c), gid(r, c + 1), 2 + rand() % 3);
    for (c = 0; c < GRID; c += 3)                 /* north-south lines every 3rd avenue */
        for (r = 0; r + 1 < GRID; r++)
            metro_add_edge(m, 4, gid(r, c), gid(r + 1, c), 2 + rand() % 3);
    m->st[gid(0, 0)].isExit = m->st[gid(0, GRID - 1)].isExit = 1;
    m->st[gid(GRID - 1, 0)].isExit = m->st[gid(GRID - 1, GRID - 1)].isExit = 1;
    metro_finalize(m);
}

/* textbook O(V^2) Dijkstra: scan an array for the minimum instead of a heap */
static void dijkstra_array(Metro *m, int src, int dist[])
{
    static char done[MAX_STATIONS];
    int i, k;
    for (i = 0; i < m->nStations; i++) { dist[i] = INF; done[i] = 0; }
    dist[src] = 0;
    for (k = 0; k < m->nStations; k++) {
        int u = -1;
        AdjNode *a;
        for (i = 0; i < m->nStations; i++)
            if (!done[i] && (u < 0 || dist[i] < dist[u])) u = i;
        if (u < 0 || dist[u] >= INF) break;
        done[u] = 1;
        for (a = m->adj[u]; a; a = a->next)
            if (dist[u] + m->ed[a->edge].minutes < dist[a->to])
                dist[a->to] = dist[u] + m->ed[a->edge].minutes;
    }
}

static void dijkstra_heap(Metro *m, int src, int dist[])
{
    static char done[MAX_STATIONS];
    MinHeap h;
    int i;
    for (i = 0; i < m->nStations; i++) { dist[i] = INF; done[i] = 0; }
    heap_init(&h, 256);
    dist[src] = 0;
    heap_push(&h, 0, src);
    while (h.size) {
        int u = heap_pop(&h).val;
        AdjNode *a;
        if (done[u]) continue;
        done[u] = 1;
        for (a = m->adj[u]; a; a = a->next) {
            int nd = dist[u] + m->ed[a->edge].minutes;
            if (nd < dist[a->to]) { dist[a->to] = nd; heap_push(&h, nd, a->to); }
        }
    }
    heap_free(&h);
}

static void row(const char *label, double value, double maxv, const char *unit, int color)
{
    int i, w = maxv > 0 ? (int)(30.0 * value / maxv + 0.5) : 0;
    printf("    %-30s ", label);
    for (i = 0; i < 30; i++) cprintf(i < w ? color : 90, i < w ? "#" : ".");
    printf("  %9.1f %s\n", value, unit);
}

void bench_compute(BenchResult *R)
{
    static int d1[MAX_STATIONS], d2[MAX_STATIONS], isArt[MAX_STATIONS], isBr[MAX_EDGES], inMst[MAX_EDGES];
    static int comp[MAX_STATIONS], qa[300], qb[300];
    static Route r1, r2, alt[5];
    long expD = 0, expA = 0;
    double t0;
    int i;
    char code[4], buf[16];

    memset(R, 0, sizeof *R);
    build_grid(&B);
    R->stations = B.nStations;
    R->edges = B.nEdges;
    R->states = B.nStations * NL1;

    srand(7);
    for (i = 0; i < 300; i++) { qa[i] = rand() % B.nStations; qb[i] = rand() % B.nStations; }

    t0 = now_ms();
    for (i = 0; i < 300; i++) { route_search(&B, qa[i], qb[i], 480, NO_LINE, 0, NULL, &r1); expD += r1.explored; }
    R->msD = now_ms() - t0;
    t0 = now_ms();
    for (i = 0; i < 300; i++) { route_search(&B, qa[i], qb[i], 480, NO_LINE, 1, NULL, &r2); expA += r2.explored; }
    R->msA = now_ms() - t0;
    R->expD = expD / 300.0;
    R->expA = expA / 300.0;
    for (i = 0; i < 300; i++) {                     /* verify they agree */
        route_search(&B, qa[i], qb[i], 480, NO_LINE, 0, NULL, &r1);
        route_search(&B, qa[i], qb[i], 480, NO_LINE, 1, NULL, &r2);
        if (r1.total != r2.total) R->mismatch++;
    }

    t0 = now_ms();
    for (i = 0; i < 100; i++) dijkstra_array(&B, qa[i], d1);
    R->msArray = now_ms() - t0;
    t0 = now_ms();
    for (i = 0; i < 100; i++) dijkstra_heap(&B, qa[i], d2);
    R->msHeap = now_ms() - t0;
    R->sameDist = 1;
    for (i = 0; i < 100; i++) {
        dijkstra_heap(&B, qa[i], d2);
        dijkstra_array(&B, qa[i], d1);
        if (memcmp(d1, d2, sizeof(int) * B.nStations)) R->sameDist = 0;
    }

    t0 = now_ms();
    for (i = 0; i < 10; i++) route_k_shortest(&B, qa[i], qb[i], 480, 5, alt);
    R->msYen = (now_ms() - t0) / 10;
    t0 = now_ms();
    for (i = 0; i < 50; i++) find_critical(&B, isArt, isBr);
    R->msTarjan = (now_ms() - t0) / 50;
    for (i = 0; i < B.nStations; i++) R->nArt += isArt[i];
    t0 = now_ms();
    for (i = 0; i < 50; i++) R->mstCost = kruskal_mst(&B, inMst);
    R->msKruskal = (now_ms() - t0) / 50;
    t0 = now_ms();
    for (i = 0; i < 50; i++) find_islands(&B, comp);
    R->msDsu = (now_ms() - t0) / 50;

    t0 = now_ms();
    for (i = 0; i < 100000; i++) {
        int id = rand() % B.nStations;
        memcpy(code, B.st[id].code, 4);
        if (hash_get(&B, code) != id) R->mismatch++;
    }
    R->nsHash = (now_ms() - t0) * 1e6 / 100000;
    t0 = now_ms();
    for (i = 0; i < 100000; i++) {
        sprintf(buf, "block %d", rand() % GRID);
        trie_collect(B.trie, buf, comp, 8);
    }
    R->nsTrie = (now_ms() - t0) * 1e6 / 100000;
    metro_free(&B);
}

void run_benchmark(void)
{
    BenchResult R;
    printf("\n  Building a %dx%d grid city and timing every algorithm ... ", GRID, GRID);
    fflush(stdout);
    bench_compute(&R);
    printf("%d stations, %d tracks, %d search states\n", R.stations, R.edges, R.states);

    printf("\n  ");
    cprintf(1, "1. Point-to-point routing, 300 random trips (state graph, transfers counted)\n");
    row("Dijkstra  states explored", R.expD, R.expD, "avg", 94);
    row("A*        states explored", R.expA, R.expD, "avg", 93);
    printf("    %-30s ", "");
    cprintf(92, "A* explores %d%% fewer states", (int)(100 - 100.0 * R.expA / R.expD));
    printf("  |  %.1f ms vs %.1f ms  |  ", R.msD, R.msA);
    if (R.mismatch) cprintf(91, "%d MISMATCHES\n", R.mismatch); else cprintf(92, "same answers\n");

    printf("\n  ");
    cprintf(1, "2. Why the heap matters: all-destinations Dijkstra from 100 sources\n");
    row("array scan    O(V^2)", R.msArray, R.msArray, "ms", 91);
    row("binary heap   O(E log V)", R.msHeap, R.msArray, "ms", 92);
    printf("    %-30s ", "");
    cprintf(92, "heap is %.0fx faster", R.msHeap > 0 ? R.msArray / R.msHeap : 0);
    printf("  |  ");
    if (R.sameDist) cprintf(92, "identical distances\n"); else cprintf(91, "DISTANCES DIFFER\n");

    printf("\n  ");
    cprintf(1, "3. Everything else on 900 stations\n");
    printf("    Yen top-5 routes            %8.2f ms per query\n", R.msYen);
    printf("    Tarjan articulation points  %8.3f ms   (%d critical stations)\n", R.msTarjan, R.nArt);
    printf("    Kruskal MST + merge sort    %8.3f ms   (%d track-minutes)\n", R.msKruskal, R.mstCost);
    printf("    Union-find islands          %8.3f ms\n", R.msDsu);
    printf("    Hash table lookups          %8.1f ns each\n", R.nsHash);
    printf("    Trie prefix searches        %8.1f ns each  (up to 8 matches)\n", R.nsTrie);
}
