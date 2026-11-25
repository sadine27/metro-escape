/*
 * Self-check for Metro Escape's algorithms.
 *   gcc -std=c99 -O2 -Isrc -o test_routes tests/test_routes.c src/graph.c src/heap.c src/trie.c
 *       src/segtree.c src/dsu.c src/route.c src/analysis.c src/render.c src/anim.c src/bench.c src/incidents.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include "metro.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static Metro m;
static Route r1, r2, alt[4];

static void all_pairs(const char *label)
{
    int a, b, k, pairs = 0;
    long dj = 0, as = 0;
    for (a = 0; a < m.nStations; a++)
        for (b = 0; b < m.nStations; b++) {
            int f1 = route_search(&m, a, b, 480, NO_LINE, 0, NULL, &r1);
            int f2 = route_search(&m, a, b, 480, NO_LINE, 1, NULL, &r2);
            CHECK(f1 == f2, "%s %s->%s reachability differs", label, m.st[a].code, m.st[b].code);
            if (!f1) continue;
            pairs++;
            dj += r1.explored;
            as += r2.explored;
            CHECK(r1.total == r2.total, "%s %s->%s dijkstra %d vs A* %d",
                  label, m.st[a].code, m.st[b].code, r1.total, r2.total);
            if (a != b && (a + b) % 5 == 0) {           /* Yen on a sample of pairs */
                int n = route_k_shortest(&m, a, b, 480, 4, alt);
                CHECK(n >= 1 && alt[0].total == r1.total, "%s yen best %s->%s", label, m.st[a].code, m.st[b].code);
                for (k = 1; k < n; k++)
                    CHECK(alt[k].total >= alt[k - 1].total, "%s yen order %s->%s", label, m.st[a].code, m.st[b].code);
            }
        }
    printf("  %-26s %3d pairs ok   avg explored: Dijkstra %.1f  A* %.1f\n",
           label, pairs, (double)dj / pairs, (double)as / pairs);
}

/* leaving later must never arrive earlier (needed for Dijkstra to be exact) */
static void fifo_check(void)
{
    int a, b, t, viol = 0, prev;
    seg_add(&m.delay[line_by_key(&m, 'R')], 480, 539, 9);
    seg_add(&m.delay[line_by_key(&m, 'G')], 500, 520, 15);
    for (a = 0; a < m.nStations; a++)
        for (b = 0; b < m.nStations; b++)
            for (prev = -1, t = 470; t < 560; t++) {
                if (!route_search(&m, a, b, t, NO_LINE, 0, NULL, &r1)) continue;
                route_search(&m, a, b, t, NO_LINE, 1, NULL, &r2);
                if (prev >= 0 && t + r1.total < prev) viol++;
                CHECK(r1.total == r2.total, "windowed A* %s->%s @%d", m.st[a].code, m.st[b].code, t);
                prev = t + r1.total;
            }
    CHECK(viol == 0, "%d FIFO violations", viol);
    seg_add(&m.delay[line_by_key(&m, 'R')], 480, 539, -9);
    seg_add(&m.delay[line_by_key(&m, 'G')], 500, 520, -15);
    printf("  time windows (FIFO)        %d station pairs x 90 departure times\n", m.nStations * m.nStations);
}

/* deadline escape: never enters a station after the fire; at least as good as a safe naive plan */
static void fire_check(void)
{
    static int burn[MAX_STATIONS];
    int f, s, i, pct, runs = 0, safeNaive = 0;
    for (pct = 100; pct <= 200; pct += 50)
        for (f = 0; f < m.nStations; f++) {
            spread(&m, &f, 1, pct, 0, burn, NULL, NULL);
            CHECK(burn[f] == 0, "fire origin burns at 0");
            for (s = 0; s < m.nStations; s++) {
                int naiveSafe = 1;
                if (s == f) continue;
                runs++;
                if (route_escape(&m, s, 480, burn, &r1)) {
                    CHECK(m.st[r1.station[r1.len - 1]].isExit, "escape ends at an exit");
                    for (i = 0; i < r1.len; i++)
                        CHECK(r1.arrive[i] - 480 < burn[r1.station[i]], "escape %s fire %s unsafe at %s",
                              m.st[s].code, m.st[f].code, m.st[r1.station[i]].code);
                }
                if (route_escape(&m, s, 480, NULL, &r2)) {
                    for (i = 0; i < r2.len; i++) {   /* arrive AND leave (after transfer walk) before the fire */
                        int leave = r2.arrive[i] - 480;
                        if (i > 0 && i + 1 < r2.len && m.ed[r2.edge[i]].line != m.ed[r2.edge[i - 1]].line)
                            leave += m.transferPenalty;
                        if (r2.arrive[i] - 480 >= burn[r2.station[i]] || leave >= burn[r2.station[i]]) naiveSafe = 0;
                    }
                    if (naiveSafe) {
                        safeNaive++;
                        CHECK(r1.found && r1.total <= r2.total, "safe naive route but escape worse %s/%s",
                              m.st[s].code, m.st[f].code);
                    }
                }
            }
        }
    printf("  fire escape safety         %d scenarios (%d where naive plan also survives)\n", runs, safeNaive);
}

int main(void)
{
    int comp[MAX_STATIONS];
    if (!metro_load(&m, "data/metro.txt")) { printf("cannot load data/metro.txt\n"); return 1; }

    {   /* segment tree vs brute force */
        SegTree s;
        static int arr[DAY_MIN];
        int t, l, r, v, j, mx;
        seg_init(&s, DAY_MIN);
        srand(7);
        for (t = 0; t < 2000; t++) {
            l = rand() % DAY_MIN; r = l + rand() % (DAY_MIN - l); v = rand() % 9 - 3;
            seg_add(&s, l, r, v);
            for (j = l; j <= r; j++) arr[j] += v;
            l = rand() % DAY_MIN; r = l + rand() % (DAY_MIN - l);
            for (mx = arr[l], j = l; j <= r; j++) if (arr[j] > mx) mx = arr[j];
            CHECK(seg_max(&s, l, r) == mx, "segtree range max [%d,%d]", l, r);
        }
        seg_free(&s);
        printf("  segment tree               2000 random ops vs brute force\n");
    }

    all_pairs("normal network");
    fifo_check();
    fire_check();
    seg_add(&m.delay[line_by_key(&m, 'G')], 0, DAY_MIN - 1, 7);
    seg_add(&m.delay[line_by_key(&m, 'R')], 0, DAY_MIN - 1, 3);
    all_pairs("with delays");
    m.closed[hash_get(&m, "DOC")] = 1;
    all_pairs("delays + DOC closed");
    CHECK(find_islands(&m, comp) == 2, "closing DOC should isolate SGT");
    m.closed[hash_get(&m, "DOC")] = 0;
    CHECK(find_islands(&m, comp) == 1, "network should be connected");

    {   /* Tarjan critical infrastructure & Kruskal MST */
        static int isArt[MAX_STATIONS], isBr[MAX_EDGES], inMst[MAX_EDGES];
        int i, nMst = 0, mstCost, doc = hash_get(&m, "DOC");
        find_critical(&m, isArt, isBr);
        CHECK(doc >= 0 && isArt[doc], "DOC should be an articulation point");
        mstCost = kruskal_mst(&m, inMst);
        for (i = 0; i < m.nEdges; i++) nMst += inMst[i];
        CHECK(nMst == m.nStations - 1, "MST should have V-1 edges (%d != %d)", nMst, m.nStations - 1);
        CHECK(mstCost > 0, "MST cost positive");
        printf("  Tarjan + Kruskal MST       %d MST edges, cost %d min\n", nMst, mstCost);
    }

    {   /* hash + trie + incident undo stack */
        static int ids[MAX_STATIONS];
        Incident in;
        int i, n;
        for (i = 0; i < m.nStations; i++) {
            CHECK(hash_get(&m, m.st[i].code) == i, "hash mismatch for %s", m.st[i].code);
            n = trie_collect(m.trie, m.st[i].code, ids, MAX_STATIONS);
            CHECK(n >= 1, "trie missing %s", m.st[i].code);
        }
        in.type = INC_CLOSE; in.station = hash_get(&m, "CEN");
        CHECK(incident_push(&m, in) == 1 && m.closed[in.station] == 1, "incident push close");
        in.type = INC_DELAY; in.line = line_by_key(&m, 'B'); in.from = 480; in.to = 539; in.amount = 10;
        CHECK(incident_push(&m, in) == 1 && delay_at(&m, in.line, 500) == 10, "incident push delay");
        CHECK(incident_pop(&m, NULL) == 1 && delay_at(&m, in.line, 500) == 0, "incident pop delay");
        CHECK(incident_pop(&m, NULL) == 1 && m.closed[hash_get(&m, "CEN")] == 0, "incident pop close");
        printf("  hash, trie & undo stack    all %d stations + undo verified\n", m.nStations);
    }

    {   /* fire escape under active line delays */
        static int burn[MAX_STATIONS];
        int f = hash_get(&m, "FRT"), s = hash_get(&m, "BCH"), i;
        seg_add(&m.delay[line_by_key(&m, 'P')], 480, 520, 12);
        spread(&m, &f, 1, 140, 0, burn, NULL, NULL);
        if (route_escape(&m, s, 480, burn, &r1)) {
            for (i = 0; i < r1.len; i++)
                CHECK(r1.arrive[i] - 480 < burn[r1.station[i]], "delayed fire escape unsafe at %s",
                      m.st[r1.station[i]].code);
        }
        seg_add(&m.delay[line_by_key(&m, 'P')], 480, 520, -12);
        printf("  delayed fire escape        safe with active wait windows\n");
    }

    metro_free(&m);
    printf(fails ? "\n%d FAILURES\n" : "\nALL TESTS PASSED\n", fails);
    return fails != 0;
}
