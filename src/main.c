/*
 * METRO ESCAPE - main program: menus, incident stack, guided demo.
 *
 *   metro                 interactive mode
 *   metro --gui           browser GUI (C engine serves JSON on localhost)
 *   metro --demo          9-act cinematic demo for presentations
 *   metro --fast/--slow   animation speed
 *   metro --noanim        no animations (static maps only)
 *   metro --nocolor       plain ASCII (old terminals / printing)
 *   metro --map FILE      load a different city
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "metro.h"

#ifdef _WIN32
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif

static Metro    M;
static const char *g_path = "data/metro.txt";
static MapStyle g_style;                         /* big struct: keep it off the stack */

/* ======================= small I/O helpers ======================= */
static void read_line(const char *prompt, char *buf, int n)
{
    int len;
    if (prompt) cprintf(96, "%s", prompt);
    fflush(stdout);
    if (!fgets(buf, n, stdin)) { printf("\n"); exit(0); }
    len = (int)strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) buf[--len] = '\0';
    while (buf[0] == ' ') memmove(buf, buf + 1, strlen(buf));
}

static int read_int(const char *prompt, int def)
{
    char buf[32];
    read_line(prompt, buf, sizeof buf);
    if (!buf[0]) return def;
    return atoi(buf);
}

static int parse_time(const char *s)
{
    int h, mi;
    if (sscanf(s, "%d:%d", &h, &mi) == 2 && h >= 0 && h < 24 && mi >= 0 && mi < 60) return h * 60 + mi;
    if (sscanf(s, "%d", &h) == 1 && h >= 0 && h < 24 && !strchr(s, ':')) return h * 60;
    return -1;
}

static int read_time(const char *prompt)
{
    char buf[32];
    for (;;) {
        int t;
        read_line(prompt, buf, sizeof buf);
        if (!buf[0]) return -1;
        if ((t = parse_time(buf)) >= 0) return t;
        cprintf(91, "  use HH:MM, e.g. 08:15\n");
    }
}

static void pause_enter(const char *msg)
{
    char buf[8];
    read_line(msg ? msg : "\n  [press Enter to continue] ", buf, sizeof buf);
}

static void clear_screen(void)
{
    if (g_color) printf("\x1b[2J\x1b[H");
    else printf("\n\n");
}

static void typeline(int sgr, const char *text)
{
    cprintf(sgr, "%s", text);
    printf("\n");
    if (anim_on()) sleep_ms(g_animMs * 3);
}

static void header(const char *title, const char *algo)
{
    printf("\n");
    cprintf(1, "== %s ", title);
    if (algo) cprintf(90, "[%s]", algo);
    printf("\n");
}

static void print_station(int id)
{
    printf("%s ", M.st[id].name);
    cprintf(90, "[%s]", M.st[id].code);
}

/* ===================== station picker (hash + trie) ===================== */
static void print_lines_of(int id)
{
    int l;
    for (l = 0; l < M.nLines; l++)
        if (M.st[id].lineMask & (1 << l)) cprintf(M.ln[l].color, " %s", M.ln[l].name);
}

static int pick_station(const char *prompt)
{
    static int ids[MAX_STATIONS];
    char buf[64];
    int n, i, id;
    for (;;) {
        read_line(prompt, buf, sizeof buf);
        if (!buf[0]) return -1;
        if ((id = hash_get(&M, buf)) >= 0) {            /* exact code: O(1) hash hit */
            printf("    -> "); print_station(id); printf("\n");
            return id;
        }
        n = trie_collect(M.trie, buf, ids, MAX_STATIONS); /* prefix search */
        if (n == 1) { printf("    -> "); print_station(ids[0]); printf("\n"); return ids[0]; }
        if (n == 0) { cprintf(91, "    no station starts with '%s'\n", buf); continue; }
        cprintf(90, "    %d matches for '%s':\n", n, buf);
        for (i = 0; i < n; i++) {
            printf("     %2d) ", i + 1);
            print_station(ids[i]);
            print_lines_of(ids[i]);
            printf("\n");
        }
        i = read_int("    pick #: ", 0);
        if (i >= 1 && i <= n) return ids[i - 1];
    }
}

static int pick_line(void)
{
    char buf[32];
    int i, l;
    for (;;) {
        printf("  lines:");
        for (i = 0; i < M.nLines; i++) cprintf(M.ln[i].color, "  [%c] %s", M.ln[i].key, M.ln[i].name);
        printf("\n");
        read_line("  line key: ", buf, sizeof buf);
        if (!buf[0]) return -1;
        if ((l = line_by_key(&M, buf[0])) >= 0) return l;
    }
}

static int ask_yes(const char *prompt)
{
    char buf[8];
    read_line(prompt, buf, sizeof buf);
    return buf[0] == 'y' || buf[0] == 'Y';
}

/* ===================== route printing ===================== */
static void print_compact(const Route *r)
{
    int i = 0;
    printf("%s", M.st[r->station[0]].code);
    while (i + 1 < r->len) {
        int L = M.ed[r->edge[i]].line, j = i;
        while (j + 1 < r->len && M.ed[r->edge[j]].line == L) j++;
        cprintf(M.ln[L].color, " =%s=> ", M.ln[L].name);
        printf("%s", M.st[r->station[j]].code);
        i = j;
    }
}

static void print_itinerary(const Route *r)
{
    char t1[6], t2[6];
    int i = 0, prev = NO_LINE;
    if (!r->found) { cprintf(91, "  No route - the destination is cut off!\n"); return; }
    if (r->len == 1) { printf("  You are already there.\n"); return; }

    while (i + 1 < r->len) {
        int L = M.ed[r->edge[i]].line, j = i, k, delay = 0;
        while (j + 1 < r->len && M.ed[r->edge[j]].line == L) j++;
        for (k = i; k < j; k++) {
            int base = M.ed[r->edge[k]].minutes + (k == i && prev != NO_LINE ? M.transferPenalty : 0);
            delay += (r->arrive[k + 1] - r->arrive[k]) - base;
        }
        fmt_time(r->arrive[i], t1);
        fmt_time(r->arrive[j], t2);
        printf("  %s  %s ", t1, prev == NO_LINE ? "Board    " : "Change to");
        cprintf(M.ln[L].color, "%-6s", M.ln[L].name);
        printf(" at ");
        print_station(r->station[i]);
        if (prev != NO_LINE) cprintf(90, "  (+%d min walk)", M.transferPenalty);
        printf("\n");
        if (j - i > 1) {
            cprintf(90, "                   ride past ");
            for (k = i + 1; k < j; k++) cprintf(90, "%s%s", M.st[r->station[k]].name, k + 1 < j ? ", " : "");
            printf("\n");
        }
        printf("  %s  Alight at ", t2);
        print_station(r->station[j]);
        if (delay > 0) cprintf(93, "  (incl. +%d min delays/waiting)", delay);
        printf("\n");
        prev = L;
        i = j;
    }
    printf("  ");
    cprintf(1, "Total %d min", r->total);
    printf("  |  %d stops  |  %d transfer%s\n", r->len - 1, r->transfers, r->transfers == 1 ? "" : "s");
}

static void race_bars(const Route *dj, const Route *as)
{
    int i, wd = 40, wa = dj->explored ? 40 * as->explored / dj->explored : 0;
    printf("\n  Algorithm race (same answer, different effort):\n");
    printf("    Dijkstra  ");
    for (i = 0; i < wd; i++) cprintf(94, "#");
    printf(" %3d states -> %d min\n", dj->explored, dj->total);
    printf("    A*        ");
    for (i = 0; i < wd; i++) cprintf(i < wa ? 93 : 90, i < wa ? "#" : ".");
    printf(" %3d states -> %d min", as->explored, as->total);
    if (as->explored < dj->explored)
        cprintf(92, "   %d%% less work", 100 - 100 * as->explored / dj->explored);
    printf("\n");
}

/* ===================== features ===================== */
static void show_map(void)
{
    char t[6], title[64];
    style_default(&g_style);
    fmt_time(g_clock, t);
    sprintf(title, "NOVA CITY METRO  -  %s", t);
    render_map(&M, &g_style, title);
}

/* animate: 0 = static map, 1 = search race animation */
static int plan_route(int src, int dst, int animate, Route *out)
{
    static Route dj, as;
    char title[96];
    if (animate) anim_search_race(&M, src, dst, g_clock, &dj, &as);
    else {
        route_search(&M, src, dst, g_clock, NO_LINE, 0, NULL, &dj);
        route_search(&M, src, dst, g_clock, NO_LINE, 1, NULL, &as);
    }
    if (!animate || !anim_on() || !as.found) {
        sprintf(title, "FASTEST: %s -> %s", M.st[src].name, M.st[dst].name);
        style_route(&M, &g_style, &as);
        render_map(&M, &g_style, title);
    }
    printf("\n");
    print_itinerary(&as);
    if (as.found) race_bars(&dj, &as);
    if (out) *out = as;
    return as.found;
}

static void menu_route(void)
{
    static Route r;
    int a, b;
    char title[96];
    header("PLAN FASTEST ROUTE", "Dijkstra vs A* on (station,line) state graph, binary heap");
    if ((a = pick_station("  From: ")) < 0) return;
    if ((b = pick_station("  To  : ")) < 0) return;
    if (!plan_route(a, b, 1, &r) || !anim_on()) return;
    if (ask_yes("\n  Ride along? (y/N): ")) {
        sprintf(title, "RIDING  %s -> %s", M.st[a].name, M.st[b].name);
        anim_ride(&M, &r, title);
        print_itinerary(&r);
    }
}

static void show_alternatives(int a, int b, int K, int ask)
{
    static Route r[5];
    int n, i, pick = 1;
    char title[96];
    n = route_k_shortest(&M, a, b, g_clock, K, r);
    if (n == 0) { cprintf(91, "  No route at all.\n"); return; }
    printf("\n");
    for (i = 0; i < n; i++) {
        cprintf(i == 0 ? 92 : 97, "  #%d  %3d min, %d transfer%s  ", i + 1, r[i].total,
                r[i].transfers, r[i].transfers == 1 ? " " : "s");
        print_compact(&r[i]);
        printf("\n");
    }
    if (ask) pick = read_int("\n  show which on the map? [1]: ", 1);
    if (pick < 1 || pick > n) pick = 1;
    sprintf(title, "ROUTE #%d of %d: %s -> %s", pick, n, M.st[a].name, M.st[b].name);
    style_route(&M, &g_style, &r[pick - 1]);
    render_map(&M, &g_style, title);
    print_itinerary(&r[pick - 1]);
}

static void menu_alternatives(void)
{
    int a, b, K;
    header("ALTERNATIVE ROUTES", "Yen's K-shortest loopless paths");
    if ((a = pick_station("  From: ")) < 0) return;
    if ((b = pick_station("  To  : ")) < 0) return;
    K = read_int("  How many routes (1-5) [3]: ", 3);
    if (K < 1) K = 1;
    if (K > 5) K = 5;
    show_alternatives(a, b, K, 1);
}

static void menu_fewest(void)
{
    static Route r, f;
    int a, b;
    header("FEWEST STOPS", "Breadth-First Search with a queue");
    if ((a = pick_station("  From: ")) < 0) return;
    if ((b = pick_station("  To  : ")) < 0) return;
    if (!route_fewest_stops(&M, a, b, g_clock, &r)) { cprintf(91, "  unreachable\n"); return; }
    style_route(&M, &g_style, &r);
    render_map(&M, &g_style, "FEWEST STOPS (BFS)");
    print_itinerary(&r);
    route_search(&M, a, b, g_clock, NO_LINE, 1, NULL, &f);
    printf("\n  Compare: the fastest route takes %d min with %d stops", f.total, f.len - 1);
    if (f.total < r.total) printf(" - fewest stops is not always fastest!");
    printf("\n");
}

/* ---------- incidents (stack for undo) ---------- */
static void describe_incident(const Incident *in)
{
    char a[6], b[6];
    if (in->type == INC_CLOSE) {
        printf("CLOSED  ");
        print_station(in->station);
    } else {
        fmt_time(in->from, a);
        fmt_time(in->to + 1, b);
        printf("DELAY   ");
        cprintf(M.ln[in->line].color, "%s line", M.ln[in->line].name);
        printf(" +%d min  %s-%s", in->amount, a, b);
    }
}

static int push_incident(Incident in)
{
    if (!incident_push(&M, in)) {
        cprintf(93, in.type == INC_CLOSE ? "  already closed (or log full)\n" : "  could not apply delay\n");
        return 0;
    }
    cprintf(93, "  ! ");
    describe_incident(&in);
    printf("\n");
    return 1;
}

static void pop_incident(void)
{
    Incident in;
    if (!incident_pop(&M, &in)) { printf("  Nothing to undo.\n"); return; }
    cprintf(92, "  undone: ");
    describe_incident(&in);
    printf("\n");
}

static void menu_incident(void)
{
    Incident in;
    int c, i;
    header("REPORT INCIDENT", "closures + segment-tree range updates");
    if (incident_count()) {
        printf("  Active incidents (top of stack first):\n");
        for (i = incident_count() - 1; i >= 0; i--) { printf("    "); describe_incident(incident_at(i)); printf("\n"); }
    }
    printf("  [1] Close a station (fire / flood / protest)\n");
    printf("  [2] Delay a line during a time window\n");
    c = read_int("  > ", 0);
    memset(&in, 0, sizeof in);
    if (c == 1) {
        in.type = INC_CLOSE;
        if ((in.station = pick_station("  Station to close: ")) < 0) return;
        push_incident(in);
    } else if (c == 2) {
        in.type = INC_DELAY;
        if ((in.line = pick_line()) < 0) return;
        if ((in.from = read_time("  from (HH:MM): ")) < 0) return;
        if ((c = read_time("  to   (HH:MM): ")) < 0) return;
        if (c == in.from) { cprintf(91, "  empty time window - cancelled\n"); return; }
        in.to = (c + DAY_MIN - 1) % DAY_MIN;
        in.amount = read_int("  extra minutes per hop (1-120): ", 0);
        if (in.amount <= 0 || in.amount > 120) { printf("  cancelled\n"); return; }
        push_incident(in);
    }
}

static void menu_timeline(void)
{
    static const char shade[] = " .:-=+*#%@";
    int l, h;
    char t[6];
    header("DELAY TIMELINE", "24 range-max queries per line on lazy segment trees");
    printf("  hour      ");
    for (h = 0; h < 24; h++) printf("%d", h % 10);
    printf("   peak  now\n");
    for (l = 0; l < M.nLines; l++) {
        int peak = seg_max(&M.delay[l], 0, DAY_MIN - 1);
        int now = delay_at(&M, l, g_clock);
        cprintf(M.ln[l].color, "  %-8s", M.ln[l].name);
        printf("  ");
        for (h = 0; h < 24; h++) {
            int d = seg_max(&M.delay[l], h * 60, h * 60 + 59);
            int lvl = d <= 0 ? 0 : d >= 9 ? 9 : d;
            cprintf(lvl ? (lvl > 5 ? 91 : 93) : 90, "%c", lvl ? shade[lvl] : '.');
        }
        printf("  %3d   %3d  min\n", peak, now);
    }
    fmt_time(g_clock, t);
    printf("\n  legend: . none  ':' ~2  '=' ~4  '*' ~6  '@' 9+ minutes   (clock now %s)\n", t);
}

static void menu_evacuate(void)
{
    static int time[MAX_STATIONS], exitOf[MAX_STATIONS], pe[MAX_STATIONS];
    MinHeap h;
    int i, trapped = 0, worst = -1, col;
    header("EVACUATE NOVA CITY", "multi-source Dijkstra from every exit = shortest-path forest");
    evacuate(&M, time, exitOf, pe);
    if (anim_on()) anim_evac_wave(&M, time, pe);
    else {
        style_default(&g_style);
        for (i = 0; i < M.nEdges; i++) g_style.edgeMode[i] = 2;
        for (i = 0; i < M.nStations; i++) {
            if (M.closed[i] || time[i] >= INF) continue;
            if (pe[i] >= 0) g_style.edgeMode[pe[i]] = 1;
            g_style.stationColor[i] = time[i] <= 8 ? 92 : time[i] <= 16 ? 93 : 91;
        }
        render_map(&M, &g_style, "EVACUATION TREE  (green <=8 min, yellow <=16, red slower)");
    }

    heap_init(&h, M.nStations);
    for (i = 0; i < M.nStations; i++) {
        if (M.closed[i]) continue;
        if (time[i] >= INF) { trapped++; continue; }
        if (!M.st[i].isExit) heap_push(&h, time[i], i);
        if (worst < 0 || time[i] > time[worst]) worst = i;
    }
    printf("\n  Evacuation order (heap-sorted, fastest first):\n");
    i = 0;
    while (h.size) {
        int u = heap_pop(&h).val;
        col = time[u] <= 8 ? 92 : time[u] <= 16 ? 93 : 91;
        cprintf(col, "   %3d min", time[u]);
        printf("  %-3s -> %-3s%s", M.st[u].code, M.st[exitOf[u]].code, (++i % 4 == 0) ? "\n" : "   ");
    }
    heap_free(&h);
    printf("\n");
    if (worst >= 0) {
        printf("\n  Slowest to escape: ");
        print_station(worst);
        printf(" (%d min)\n", time[worst]);
    }
    if (trapped) {
        cprintf(95, "  %d station(s) TRAPPED - no exit reachable: ", trapped);
        for (i = 0; i < M.nStations; i++)
            if (!M.closed[i] && time[i] >= INF) cprintf(95, "%s ", M.st[i].code);
        printf("\n");
    }
}

static void menu_critical(void)
{
    static int isArt[MAX_STATIONS], isBridge[MAX_EDGES];
    int i, nA = 0, nB = 0;
    MinHeap h;
    header("CRITICAL INFRASTRUCTURE", "Tarjan's DFS low-link: articulation points + bridges");
    find_critical(&M, isArt, isBridge);
    style_default(&g_style);
    for (i = 0; i < M.nEdges; i++) if (isBridge[i]) { g_style.edgeMode[i] = 1; nB++; }
    for (i = 0; i < M.nStations; i++) if (isArt[i]) { g_style.stationColor[i] = 91; g_style.stationMark[i] = '!'; nA++; }
    render_map(&M, &g_style, "SINGLE POINTS OF FAILURE  (! station, glowing = bridge track)");

    printf("\n  %d critical stations - closing any one splits the network (worst first):\n", nA);
    heap_init(&h, 16);
    for (i = 0; i < M.nStations; i++)
        if (isArt[i]) heap_push(&h, -stranded_if_closed(&M, i), i);   /* max-heap via negated keys */
    while (h.size) {
        HeapItem it = heap_pop(&h);
        int k;
        printf("    ");
        cprintf(91, "!");
        printf(" %-14s strands %d station%s ", M.st[it.val].name, -it.key, -it.key == 1 ? " " : "s");
        for (k = 0; k < -it.key; k++) cprintf(91, "#");
        printf("\n");
    }
    heap_free(&h);
    printf("\n  %d bridge tracks - no detour exists if they fail:\n    ", nB);
    for (i = 0; i < M.nEdges; i++)
        if (isBridge[i]) {
            cprintf(M.ln[M.ed[i].line].color, "%s-%s", M.st[M.ed[i].u].code, M.st[M.ed[i].v].code);
            printf("  ");
        }
    printf("\n");
}

static void menu_mst(void)
{
    static int inMst[MAX_EDGES];
    int i, total, all = 0, kept = 0, cut = 0;
    header("NIGHT MAINTENANCE PLAN", "Kruskal's MST: merge sort + union-find");
    total = kruskal_mst(&M, inMst);
    style_default(&g_style);
    for (i = 0; i < M.nEdges; i++) {
        if (M.closed[M.ed[i].u] || M.closed[M.ed[i].v]) continue;
        all += M.ed[i].minutes;
        if (inMst[i]) kept++; else { g_style.edgeMode[i] = 2; cut++; }
    }
    render_map(&M, &g_style, "MINIMUM TRACK SET THAT KEEPS EVERY STATION CONNECTED");
    printf("\n  Keep %d tracks open (%d track-minutes), close %d for repairs tonight.\n", kept, total, cut);
    printf("  Tracks that can go under maintenance:\n    ");
    for (i = 0; i < M.nEdges; i++)
        if (!inMst[i] && !M.closed[M.ed[i].u] && !M.closed[M.ed[i].v]) {
            cprintf(M.ln[M.ed[i].line].color, "%s-%s", M.st[M.ed[i].u].code, M.st[M.ed[i].v].code);
            printf("  ");
        }
    printf("\n  Saving: %d of %d track-minutes powered down (%d%%).\n",
           all - total, all, all ? 100 * (all - total) / all : 0);
}

static void menu_islands(void)
{
    static const int palette[] = { 92, 96, 93, 95, 94, 91 };
    static int comp[MAX_STATIONS], rootIdx[MAX_STATIONS];
    int n, i, k = 0, j;
    header("NETWORK ISLANDS", "Union-Find connected components");
    n = find_islands(&M, comp);
    style_default(&g_style);
    for (i = 0; i < M.nStations; i++) rootIdx[i] = -1;
    for (i = 0; i < M.nStations; i++) {
        if (comp[i] < 0) continue;
        if (rootIdx[comp[i]] < 0) rootIdx[comp[i]] = k++;
        g_style.stationColor[i] = palette[rootIdx[comp[i]] % 6];
    }
    render_map(&M, &g_style, "CONNECTED COMPONENTS");
    if (n == 1) cprintf(92, "\n  The whole network is connected.\n");
    else cprintf(93, "\n  The network has split into %d islands:\n", n);
    for (j = 0; j < k && n > 1; j++) {
        printf("   ");
        for (i = 0; i < M.nStations; i++)
            if (comp[i] >= 0 && rootIdx[comp[i]] == j) cprintf(palette[j % 6], "%s ", M.st[i].code);
        printf("\n");
    }
}

static void menu_search(void)
{
    static int ids[MAX_STATIONS];
    char buf[64];
    int n, i;
    header("STATION SEARCH", "Trie prefix search over codes, names and every word");
    read_line("  type a prefix: ", buf, sizeof buf);
    n = trie_collect(M.trie, buf, ids, MAX_STATIONS);
    if (!n) { printf("  no matches\n"); return; }
    for (i = 0; i < n; i++) {
        printf("   ");
        print_station(ids[i]);
        print_lines_of(ids[i]);
        if (M.st[ids[i]].isExit) cprintf(96, "  (city exit)");
        if (M.closed[ids[i]]) cprintf(91, "  CLOSED");
        printf("\n");
    }
}

static void menu_fire(void)
{
    int me, fire, sp, pct;
    header("FIRE ESCAPE", "Dijkstra fire spread + deadline-constrained Dijkstra escape");
    printf("  A fire breaks out and spreads along the tracks. Can you reach a city exit first?\n");
    printf("  (Enter at both prompts for a dramatic auto-picked scenario)\n");
    fire = pick_station("  Fire starts at: ");
    me = fire >= 0 ? pick_station("  You are at    : ") : -1;
    sp = read_int("  Fire speed  1 slow / 2 medium / 3 fast [2]: ", 2);
    pct = sp == 1 ? 200 : sp == 3 ? 100 : 140;
    if (fire < 0 || me < 0) {
        if (!find_fire_scenario(&M, pct, g_clock, &me, &fire)) {
            printf("  No dramatic scenario on this network - pick stations yourself.\n");
            return;
        }
        printf("  Scenario: fire at ");
        cprintf(91, "%s", M.st[fire].name);
        printf(", you are at ");
        cprintf(96, "%s\n", M.st[me].name);
        if (anim_on()) sleep_ms(g_animMs * 30);
    }
    if (me == fire) { cprintf(91, "  You are standing in the fire. Start somewhere else.\n"); return; }
    fire_escape(&M, me, fire, pct, g_clock);
}

static void menu_settings(void)
{
    int t;
    char c[6];
    fmt_time(g_clock, c);
    header("SETTINGS", NULL);
    printf("  clock %s, transfer penalty %d min, animation %s (%d ms/frame)\n",
           c, M.transferPenalty, g_anim ? "on" : "off", g_animMs);
    if ((t = read_time("  new clock (HH:MM, Enter to keep): ")) >= 0) g_clock = t;
    t = read_int("  transfer penalty minutes (Enter to keep): ", M.transferPenalty);
    if (t >= 0 && t <= 60) M.transferPenalty = t;
    t = read_int("  animation ms per frame, 0 = off (Enter to keep): ", g_anim ? g_animMs : 0);
    if (t <= 0) g_anim = 0; else { g_anim = 1; g_animMs = t > 500 ? 500 : t; }
}

/* ===================== banner + boot ===================== */
static void banner(void)
{
    cprintf(91, "  __  __ ___ _____ ___  ___  ");
    cprintf(93, "  ___ ___  ___   _   ___ ___\n");
    cprintf(91, " |  \\/  | __|_   _| _ \\/ _ \\ ");
    cprintf(93, " | __/ __|/ __| /_\\ | _ \\ __|\n");
    cprintf(92, " | |\\/| | _|  | | |   / (_) |");
    cprintf(94, " | _|\\__ \\ (__ / _ \\|  _/ _|\n");
    cprintf(92, " |_|  |_|___| |_| |_|_\\\\___/ ");
    cprintf(95, " |___|___/\\___/_/ \\_\\_| |___|\n");
}

static void boot(void)
{
    static int isArt[MAX_STATIONS], isBridge[MAX_EDGES];
    char line[160];
    int used, longest, i, adj = 0, nArt = 0;
    AdjNode *a;
    hash_stats(&M, &used, &longest);
    for (i = 0; i < M.nStations; i++) for (a = M.adj[i]; a; a = a->next) adj++;
    find_critical(&M, isArt, isBridge);
    for (i = 0; i < M.nStations; i++) nArt += isArt[i];

    clear_screen();
    banner();
    cprintf(90, "  transit planner & emergency simulator - pure C, every structure hand-built\n\n");
    sprintf(line, "  [ ok ] parsed %-22s %d stations, %d tracks, %d lines", g_path, M.nStations, M.nEdges, M.nLines);
    typeline(0, line);
    sprintf(line, "  [ ok ] hash table              %d buckets, %d used, longest chain %d", HASH_SIZE, used, longest);
    typeline(0, line);
    sprintf(line, "  [ ok ] trie index              %d nodes over codes, names and words", trie_count(M.trie));
    typeline(0, line);
    sprintf(line, "  [ ok ] adjacency lists         %d entries (undirected multigraph)", adj);
    typeline(0, line);
    sprintf(line, "  [ ok ] search state graph      %d states = stations x (lines + 1)", M.nStations * NL1);
    typeline(0, line);
    sprintf(line, "  [ ok ] segment trees           %d lines x %d minutes, lazy propagation", M.nLines, DAY_MIN);
    typeline(0, line);
    sprintf(line, "  [ ok ] A* heuristic            %.3f min per map unit (admissible)", M.minPerUnit);
    typeline(0, line);
    sprintf(line, "  [ ok ] Tarjan pre-scan         %d single points of failure", nArt);
    typeline(0, line);
    typeline(92, "\n  Nova City Metro is online.");
}

/* ===================== cinematic demo ===================== */
#define ACTS 9
static void act(int n, const char *title, const char *story)
{
    pause_enter("\n  [Enter] next act > ");
    clear_screen();
    cprintf(96, "  ACT %d/%d ", n, ACTS);
    cprintf(1, " %s\n", title);
    cprintf(90, "  %s\n", story);
    if (anim_on()) sleep_ms(g_animMs * 30);
}

static void run_demo(void)
{
    static Route trip, before;
    Incident in;
    int air = hash_get(&M, "AIR"), bch = hash_get(&M, "BCH"), base = incident_count(), me, fire;
    int saveClock = g_clock;

    if (air < 0 || bch < 0) { printf("  The demo needs the default Nova City map.\n"); return; }
    g_clock = 8 * 60 + 30;
    clear_screen();
    banner();
    printf("\n  08:30, Nova City. A tourist lands at the ");
    cprintf(96, "Airport");
    printf(" and wants to reach the ");
    cprintf(96, "Beach");
    printf(".\n  Over the next hour the city throws everything at them.\n");
    show_map();

    act(1, "Watch the algorithms think",
        "Dijkstra pops states from a binary heap in distance order; A* adds a straight-line heuristic.");
    plan_route(air, bch, 1, &trip);
    before = trip;

    act(2, "Ride along", "The itinerary is replayed in time: the train moves along the rasterised tracks.");
    anim_ride(&M, &trip, "RIDING  Airport -> Beach");
    print_itinerary(&trip);

    act(3, "Rush hour hits the Green line",
        "One O(log n) range update on Green's segment tree: +7 min per hop, 08:00-10:00.");
    memset(&in, 0, sizeof in);
    in.type = INC_DELAY; in.line = line_by_key(&M, 'G'); in.from = 8 * 60; in.to = 10 * 60 - 1; in.amount = 7;
    push_incident(in);
    menu_timeline();
    plan_route(air, bch, 0, &trip);
    route_simulate(&M, &before, g_clock);          /* the old plan, re-timed under the delay */
    printf("\n  Old plan in rush hour: ");
    cprintf(91, "%d min", before.total);
    printf("   New plan: ");
    cprintf(92, "%d min", trip.total);
    if (before.total > trip.total) printf("  -> %d min saved by switching lines", before.total - trip.total);
    printf("\n  Every edge cost is a live point query on the line's segment tree, and the planner\n"
           "  may wait out a delay window when that is faster (this keeps Dijkstra exact).\n");

    act(4, "Flood at Docklands! Give me options",
        "Docklands leaves the graph. Yen's algorithm finds the 3 best loopless detours.");
    memset(&in, 0, sizeof in);
    in.type = INC_CLOSE; in.station = hash_get(&M, "DOC");
    push_incident(in);
    show_alternatives(air, bch, 3, 0);

    act(5, "Which stations could break the city?",
        "Tarjan's DFS (low-link values) on the live, flooded network: articulation points + bridges.");
    menu_critical();

    act(6, "All clear", "Incidents live on a stack, so they unwind in reverse order. Union-find checks the result.");
    while (incident_count() > base) pop_incident();
    menu_islands();

    act(7, "FIRE!",
        "A fire spreads along the tracks. An ordinary planner sends you to the nearest exit... watch.");
    me = bch;                                       /* our tourist has just reached the Beach */
    fire = hash_get(&M, "FRT");
    if (fire >= 0 || find_fire_scenario(&M, 140, g_clock, &me, &fire)) {
        printf("\n  Our tourist is at ");
        cprintf(96, "%s", M.st[me].name);
        printf(" when fire breaks out at ");
        cprintf(91, "%s", M.st[fire].name);
        printf(".\n  An ordinary journey planner says: nearest exit, fastest route. Let's see.\n");
        if (anim_on()) sleep_ms(g_animMs * 40);
        fire_escape(&M, me, fire, 140, g_clock);
    }

    act(8, "Evacuate the whole city",
        "The same multi-source Dijkstra, seeded at all 5 exits at once, sweeps the network as a rescue wave.");
    menu_evacuate();

    act(9, "Does it scale?", "A generated 900-station city. Same code, real timings.");
    run_benchmark();

    pause_enter("\n  [Enter] finish > ");
    clear_screen();
    banner();
    printf("\n");
    typeline(1, "  Built from scratch in C:");
    typeline(94, "    graph (adjacency list)   hash table      trie            binary heap");
    typeline(93, "    Dijkstra                 A*              BFS             Yen's K-shortest");
    typeline(92, "    lazy segment tree        stack (undo)    union-find      Kruskal + merge sort");
    typeline(91, "    Tarjan bridges & APs     multi-source Dijkstra (fire, evacuation)");
    typeline(95, "    deadline-constrained Dijkstra on a time-dependent (station, line) state graph");
    typeline(1, "\n  Thank you!");
    g_clock = saveClock;
}

/* ===================== main ===================== */
static void status_line(void)
{
    char t[6];
    fmt_time(g_clock, t);
    printf("\n  clock %s  |  transfer %d min  |  %d stations, %d tracks  |  ",
           t, M.transferPenalty, M.nStations, M.nEdges);
    if (incident_count()) cprintf(93, "%d incident%s active\n", incident_count(), incident_count() > 1 ? "s" : "");
    else cprintf(92, "all clear\n");
}

static void item(const char *key, const char *label, int labelColor, const char *algo)
{
    printf("  %3s  ", key);
    cprintf(labelColor, "%-24s", label);
    cprintf(90, "%s\n", algo);
}

int main(int argc, char **argv)
{
    int demo = 0, gui = 0, port = 8080, openBrowser = 1, i, choice;
    char buf[16];

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--demo")) demo = 1;
        else if (!strcmp(argv[i], "--gui")) gui = 1;
        else if (!strcmp(argv[i], "--no-open")) openBrowser = 0;
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nocolor")) g_color = 0;
        else if (!strcmp(argv[i], "--noanim")) g_anim = 0;
        else if (!strcmp(argv[i], "--fast")) g_animMs = 15;
        else if (!strcmp(argv[i], "--slow")) g_animMs = 80;
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) g_path = argv[++i];
    }
#ifdef _WIN32
    if (g_color) {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode;
        /* a real console that refuses ANSI (very old Windows): fall back to plain text */
        if (GetConsoleMode(h, &mode) && !SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            g_color = 0;
    }
#endif
    if (!metro_load(&M, g_path)) {
        if (strcmp(g_path, "data/metro.txt") == 0 && metro_load(&M, "../data/metro.txt"))
            g_path = "../data/metro.txt";
        else {
            fprintf(stderr, "Could not load the metro map (%s). Run from the project folder.\n", g_path);
            return 1;
        }
    }

    if (gui) g_anim = 0;                       /* no typewriter delay before the server starts */
    boot();
    if (gui) return server_run(&M, port, openBrowser);
    if (demo) { run_demo(); metro_free(&M); return 0; }

    for (;;) {
        status_line();
        cprintf(90, "\n  PLAN\n");
        item("1", "Network map", 0, "");
        item("2", "Fastest route", 0, "animated Dijkstra vs A* race + ride along");
        item("3", "Alternative routes", 0, "Yen's K-shortest paths");
        item("4", "Fewest stops", 0, "BFS");
        item("5", "Station search", 0, "trie autocomplete");
        cprintf(90, "  DISRUPT\n");
        item("6", "Report incident", 0, "closure / delay window (segment tree)");
        item("7", "Undo last incident", 0, "stack");
        item("8", "Delay timeline", 0, "segment tree range-max");
        cprintf(90, "  EMERGENCY\n");
        item("9", "FIRE ESCAPE", 91, "outrun a spreading fire - deadline Dijkstra");
        item("10", "Evacuate the city", 93, "multi-source Dijkstra rescue wave");
        item("11", "Critical stations", 0, "Tarjan articulation points & bridges");
        item("12", "Network islands", 0, "union-find");
        item("13", "Night maintenance plan", 0, "Kruskal MST");
        cprintf(90, "  LAB\n");
        item("14", "Scale benchmark", 0, "900-station city, real timings");
        item("15", "Settings", 0, "clock, transfer penalty, animation speed");
        item("d", "Guided demo", 96, "9 acts, ~4 minutes");
        item("g", "Open the GUI", 96, "modern browser interface, same C engine");
        item("0", "Quit", 0, "");
        read_line("\n  > ", buf, sizeof buf);
        if (buf[0] == 'd' || buf[0] == 'D') { run_demo(); pause_enter(NULL); continue; }
        if (buf[0] == 'q' || buf[0] == 'Q') break;
        if (buf[0] == 'g' || buf[0] == 'G') return server_run(&M, port, 1);
        choice = buf[0] ? atoi(buf) : -1;
        if (choice == 0 && buf[0] == '0') break;
        switch (choice) {
            case 1:  show_map(); break;
            case 2:  menu_route(); break;
            case 3:  menu_alternatives(); break;
            case 4:  menu_fewest(); break;
            case 5:  menu_search(); break;
            case 6:  menu_incident(); break;
            case 7:  pop_incident(); break;
            case 8:  menu_timeline(); break;
            case 9:  menu_fire(); break;
            case 10: menu_evacuate(); break;
            case 11: menu_critical(); break;
            case 12: menu_islands(); break;
            case 13: menu_mst(); break;
            case 14: run_benchmark(); break;
            case 15: menu_settings(); break;
            default: continue;
        }
        pause_enter(NULL);
    }
    metro_free(&M);
    return 0;
}
