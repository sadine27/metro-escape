/*
 * ANIMATIONS - every frame is just render_map() redrawn in place.
 *
 *   anim_search_race  replays the heap pop order of Dijkstra and A*
 *   anim_ride         a train travels the route with a ticking clock
 *   fire_escape       fire spreads along tracks (Dijkstra from the fire);
 *                     an ordinary planner vs. deadline-aware Dijkstra
 *   anim_evac_wave    multi-source Dijkstra from every exit, as a wave
 */
#include <stdio.h>
#include <string.h>
#include "metro.h"

static MapStyle S;                     /* one big style reused by every frame */
static int traceA[NSTATE], traceB[NSTATE];

static void bar(double frac, int width, int color)
{
    int i, fill;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    fill = (int)(frac * width + 0.5);
    printf("[");
    for (i = 0; i < width; i++) cprintf(i < fill ? color : 90, i < fill ? "#" : ".");
    printf("]");
}

static void station_name(Metro *m, int id) { printf("%s", m->st[id].name); }

/* ===================== search race ===================== */
static void play_trace(Metro *m, int src, int dst, const int *trace, int n, int color,
                       const char *name, const char *how, const Route *r, int rival)
{
    static char seen[MAX_STATIONS];
    int k, i, reached = 0;
    memset(seen, 0, sizeof seen);

    for (k = 0; k <= n; k++) {
        if (k > 0 && !seen[trace[k - 1]]) { seen[trace[k - 1]] = 1; reached++; }
        style_default(&S);
        S.compact = 1;
        for (i = 0; i < m->nEdges; i++)
            S.edgeMode[i] = (seen[m->ed[i].u] && seen[m->ed[i].v]) ? 0 : 2;
        for (i = 0; i < m->nStations; i++)
            if (seen[i]) S.stationColor[i] = color;
        S.stationMark[src] = 'S'; S.stationColor[src] = 96;
        S.stationMark[dst] = 'E'; S.stationColor[dst] = 96;
        if (k > 0) {
            int u = trace[k - 1];
            S.nSprites = 1;
            S.spriteX[0] = m->st[u].x; S.spriteY[0] = m->st[u].y;
            S.spriteCh[0] = '*'; S.spriteFg[0] = 97;
        }
        anim_home();
        render_map(m, &S, NULL);
        printf("  ");
        cprintf(color, "%-9s", name);
        printf(" heap pops %3d   stations touched %2d/%-2d  ", k, reached, m->nStations);
        bar(rival > 0 ? (double)k / rival : 0, 24, color);
        eol();
        cprintf(90, "  %s", how);
        eol();
        eol();
        sleep_ms(g_animMs * 2 + (k == n ? 300 : 0));
    }

    /* reveal the path on top of the explored region */
    style_route(m, &S, r);
    S.compact = 1;
    for (i = 0; i < m->nStations; i++)
        if (seen[i] && !S.stationColor[i]) S.stationColor[i] = color;
    anim_home();
    render_map(m, &S, NULL);
    printf("  ");
    cprintf(color, "%-9s", name);
    printf(" found the route: ");
    cprintf(1, "%d min", r->total);
    printf(" after %d heap pops", n);
    eol();
    eol();
    eol();
    sleep_ms(g_animMs * 25);
}

void anim_search_race(Metro *m, int src, int dst, int depart, Route *dj, Route *as)
{
    int nA, nB;
    route_search(m, src, dst, depart, NO_LINE, 0, NULL, dj);
    nA = g_traceLen;
    memcpy(traceA, g_trace, sizeof(int) * nA);
    route_search(m, src, dst, depart, NO_LINE, 1, NULL, as);
    nB = g_traceLen;
    memcpy(traceB, g_trace, sizeof(int) * nB);
    if (!anim_on() || !dj->found) return;

    anim_begin();
    play_trace(m, src, dst, traceA, nA, 94, "DIJKSTRA",
               "ripples out in every direction - it has no idea where the target is", dj, nA);
    play_trace(m, src, dst, traceB, nB, 93, "A* SEARCH",
               "the straight-line heuristic pulls the search toward the target like a beam", as, nA);
    anim_end();
}

/* ===================== train ride ===================== */
static char dir_char(Metro *m, int a, int b)
{
    int dx = m->st[b].x - m->st[a].x, dy = m->st[b].y - m->st[a].y;
    if ((dx < 0 ? -dx : dx) >= 2 * (dy < 0 ? -dy : dy)) return dx >= 0 ? '>' : '<';
    return dy >= 0 ? 'v' : '^';
}

/* when the train actually leaves station i (after any transfer walk), minutes after start */
static double leave_time(Metro *m, const Route *r, int i)
{
    double t = r->arrive[i] - r->arrive[0];
    if (i > 0 && m->ed[r->edge[i]].line != m->ed[r->edge[i - 1]].line) t += m->transferPenalty;
    return t;
}

/* where is the train at t minutes after departure? returns segment index */
static int train_at(Metro *m, const Route *r, double t, int *x, int *y)
{
    int i = 0, dep = r->arrive[0];
    double a, b, f;
    while (i + 2 < r->len && r->arrive[i + 1] - dep <= t) i++;
    a = leave_time(m, r, i);                      /* waits on the platform while changing lines */
    b = r->arrive[i + 1] - dep;
    f = b > a ? (t - a) / (b - a) : 1;
    map_point(m, r->edge[i], r->station[i], f, x, y);
    return i;
}

void anim_ride(Metro *m, const Route *r, const char *title)
{
    double t, step;
    int k = 0;
    if (!anim_on() || !r->found || r->len < 2) return;
    step = r->total / 110.0;
    if (step < 0.1) step = 0.1;

    anim_begin();
    for (t = 0;; t += step, k++) {
        int x, y, i, L;
        char clk[6];
        if (t > r->total) t = r->total;
        i = train_at(m, r, t, &x, &y);
        L = m->ed[r->edge[i]].line;
        style_route(m, &S, r);
        S.compact = 1;
        S.nSprites = 1;
        S.spriteX[0] = x; S.spriteY[0] = y;
        S.spriteCh[0] = dir_char(m, r->station[i], r->station[i + 1]);
        S.spriteFg[0] = 97;
        anim_home();
        render_map(m, &S, NULL);
        fmt_time(r->arrive[0] + (int)t, clk);
        printf("  ");
        cprintf(1, "%s", title);
        eol();
        printf("  %s  ", clk);
        cprintf(m->ln[L].color, "%-6s", m->ln[L].name);
        printf(" ");
        station_name(m, r->station[i]);
        printf(" -> ");
        station_name(m, r->station[i + 1]);
        eol();
        printf("  ");
        bar(t / (r->total ? r->total : 1), 50, 96);
        printf(" %3d%%", (int)(100 * t / (r->total ? r->total : 1)));
        eol();
        sleep_ms(g_animMs);
        if (t >= r->total) break;
    }
    anim_end();
}

/* ===================== fire escape ===================== */
static const char *ESCAPED_ART[4] = {
    "  ___ ___  ___   _   ___ ___ ___  _ ",
    " | __/ __|/ __| /_\\ | _ \\ __|   \\| |",
    " | _|\\__ \\ (__ / _ \\|  _/ _|| |) |_|",
    " |___|___/\\___/_/ \\_\\_| |___|___/(_)"
};
static const char *CAUGHT_ART[4] = {
    "   ___   _  _   _  ___ _  _ _____ _ ",
    "  / __| /_\\| | | |/ __| || |_   _| |",
    " | (__ / _ \\ |_| | (_ | __ | | | |_|",
    "  \\___/_/ \\_\\___/ \\___|_||_| |_| (_)"
};

/* first moment the fire touches the train on route r (INF if never) */
static double caught_time(Metro *m, const Route *r, const int *burn, int pct)
{
    int i, k, dep = r->arrive[0];
    if (!r->found) return INF;
    if (burn[r->station[0]] <= 0) return 0;
    for (i = 0; i + 1 < r->len; i++) {
        double a = leave_time(m, r, i), b = r->arrive[i + 1] - dep;
        if (burn[r->station[i]] <= a)              /* fire arrives while we wait on the platform */
            return burn[r->station[i]] > r->arrive[i] - dep ? burn[r->station[i]] : r->arrive[i] - dep;
        for (k = 0; k <= 64; k++) {
            double f = k / 64.0, tt = a + f * (b - a);
            if (overlay_time_at(m, burn, pct, r->edge[i], r->station[i], f) <= tt) return tt;
        }
    }
    return INF;
}

/* smallest head start over the fire along the route */
static int safety_margin(const Route *r, const int *burn)
{
    int i, best = INF;
    for (i = 0; i < r->len; i++) {
        int slack = burn[r->station[i]] - (r->arrive[i] - r->arrive[0]);
        if (slack < best) best = slack;
    }
    return best;
}

static int burning_count(Metro *m, const int *burn, double t)
{
    int i, n = 0;
    for (i = 0; i < m->nStations; i++) if (burn[i] <= t) n++;
    return n;
}

static void play_escape(Metro *m, const Route *r, const int *burn, int pct, double caughtT,
                        const char *label, int labelColor, const char *sub)
{
    double end = caughtT < INF ? caughtT : r->total, t;
    int k = 0;
    for (t = 0;; t += 0.25, k++) {
        double tt = t > end ? end : t;
        int x, y, i, L;
        char clk[6];
        i = train_at(m, r, tt, &x, &y);
        L = m->ed[r->edge[i]].line;
        style_route(m, &S, r);
        S.compact = 1;
        S.burn = burn;
        S.overlayPct = pct;
        S.overlayT = t;
        S.frame = k / 2;
        S.nSprites = 1;
        S.spriteX[0] = x; S.spriteY[0] = y;
        if (t >= end && caughtT < INF) { S.spriteCh[0] = 'X'; S.spriteFg[0] = 91; }
        else { S.spriteCh[0] = dir_char(m, r->station[i], r->station[i + 1]); S.spriteFg[0] = 97; }
        anim_home();
        render_map(m, &S, NULL);
        printf("  ");
        cprintf(labelColor, "%s", label);
        cprintf(90, "  %s", sub);
        eol();
        fmt_time(r->arrive[0] + (int)t, clk);
        printf("  %s  T+%4.1f min   ", clk, t);
        cprintf(91, "fire: %2d/%d stations burning", burning_count(m, burn, t), m->nStations);
        printf("   you: ");
        cprintf(m->ln[L].color, "%s", m->ln[L].name);
        printf(" -> %s", m->st[r->station[i + 1]].name);
        eol();
        if (t >= end) {
            if (caughtT < INF) cprintf(91, "  >>> THE FIRE REACHED THE TRAIN near %s <<<", m->st[r->station[i + 1]].name);
            else cprintf(92, "  >>> REACHED %s - OUT OF THE CITY <<<", m->st[r->station[r->len - 1]].name);
        }
        eol();
        sleep_ms(g_animMs);
        if (t >= end + 2.0) break;                /* let the fire burn on a moment */
    }
}

static void art(const char **rows, int color)
{
    int i;
    for (i = 0; i < 4; i++) { cprintf(color, "        %s", rows[i]); eol(); }
}

/* the whole fire analysis, no output: used by the terminal and the web GUI */
void fire_compute(Metro *m, int me, int fire, int pct, int depart, FirePlan *p)
{
    int i;
    spread(m, &fire, 1, pct, 0, p->burn, NULL, NULL);       /* when does the fire reach each station */
    route_escape(m, me, depart, NULL, &p->naive);            /* nearest exit, fire ignored */
    route_escape(m, me, depart, p->burn, &p->smart);         /* nearest exit reachable before the fire */
    p->naiveCaught = caught_time(m, &p->naive, p->burn, pct);
    p->margin = p->smart.found ? safety_margin(&p->smart, p->burn) : 0;
    p->shelter = -1;
    for (i = 0; i < m->nStations; i++)                       /* furthest from the fire = best shelter */
        if (!m->closed[i] && (p->shelter < 0 || p->burn[i] > p->burn[p->shelter])) p->shelter = i;
}

/* full story: returns 1 if a safe route exists */
int fire_escape(Metro *m, int me, int fire, int pct, int depart)
{
    static FirePlan P;
    char clk[6];

    fire_compute(m, me, fire, pct, depart, &P);
    if (anim_on()) {
        anim_begin();
        if (P.naive.found) {
            play_escape(m, &P.naive, P.burn, pct, P.naiveCaught, "PLAN A", 93,
                        "ordinary planner: nearest exit, fastest route, ignores fire");
            art(P.naiveCaught < INF ? CAUGHT_ART : ESCAPED_ART, P.naiveCaught < INF ? 91 : 92);
            sleep_ms(g_animMs * 50);
        }
        if (P.smart.found) {
            anim_begin();
            play_escape(m, &P.smart, P.burn, pct, INF, "PLAN B", 92,
                        "deadline Dijkstra: never enters a station after the fire");
            art(ESCAPED_ART, 92);
            sleep_ms(g_animMs * 25);
        }
        anim_end();
    }

    fmt_time(depart, clk);
    printf("\n  Fire started at ");
    cprintf(91, "%s", m->st[fire].name);
    printf(" at %s, spreading along the tracks (%d%% of train travel time).\n", clk, pct);
    printf("  You are at ");
    cprintf(96, "%s", m->st[me].name);
    printf(". The fire reaches you in %d min.\n\n", P.burn[me]);

    printf("  PLAN A  (ordinary planner)   ");
    if (!P.naive.found) cprintf(91, "no exit reachable at all\n");
    else {
        printf("%2d min to %-10s ", P.naive.total, m->st[P.naive.station[P.naive.len - 1]].name);
        if (P.naiveCaught < INF) cprintf(91, "CAUGHT after %.1f min\n", P.naiveCaught);
        else cprintf(92, "survives\n");
    }
    printf("  PLAN B  (deadline Dijkstra)  ");
    if (P.smart.found) {
        printf("%2d min to %-10s ", P.smart.total, m->st[P.smart.station[P.smart.len - 1]].name);
        cprintf(92, "SAFE - always at least %d min ahead of the fire\n", P.margin);
    } else {
        cprintf(91, "no safe route exists\n");
        if (P.shelter >= 0) {
            printf("  Best shelter: ");
            cprintf(96, "%s", m->st[P.shelter].name);
            printf(" - the last place the fire reaches (%d min).\n", P.burn[P.shelter]);
        }
    }
    printf("\n  How: fire arrival times come from Dijkstra seeded at the fire. The escape search\n"
           "  is Dijkstra over (station, line) states that prunes any move arriving at or after\n"
           "  the fire. Because arrival times are FIFO, the earliest arrival is always safest.\n");
    return P.smart.found;
}

/* pick a dramatic demo: the naive route burns, the smart route escapes elsewhere */
int find_fire_scenario(Metro *m, int pct, int depart, int *me, int *fire)
{
    static int burn[MAX_STATIONS];
    static Route naive, smart;
    int f, s, best = -1;
    for (f = 0; f < m->nStations; f++) {
        if (m->closed[f]) continue;
        spread(m, &f, 1, pct, 0, burn, NULL, NULL);
        for (s = 0; s < m->nStations; s++) {
            int score, lines = 0, b;
            double ct;
            if (s == f || m->closed[s] || m->st[s].isExit) continue;
            if (!route_escape(m, s, depart, NULL, &naive)) continue;
            if ((ct = caught_time(m, &naive, burn, pct)) >= INF) continue;
            if (!route_escape(m, s, depart, burn, &smart)) continue;
            if (smart.station[smart.len - 1] == naive.station[naive.len - 1]) continue;
            for (b = m->st[f].lineMask; b; b >>= 1) lines += b & 1;
            score = (int)ct * 3 + smart.len * 2 + lines * 2;   /* caught late = more suspense */
            if (score > best) { best = score; *me = s; *fire = f; }
        }
    }
    return best >= 0;
}

/* ===================== evacuation wave ===================== */
void anim_evac_wave(Metro *m, const int time[], const int pe[])
{
    double t, maxT = 0;
    int i, k = 0;
    if (!anim_on()) return;
    for (i = 0; i < m->nStations; i++)
        if (time[i] < INF && time[i] > maxT) maxT = time[i];

    anim_begin();
    for (t = 0;; t += 0.25, k++) {
        int reached = 0;
        style_default(&S);
        S.compact = 1;
        for (i = 0; i < m->nEdges; i++) S.edgeMode[i] = 2;
        for (i = 0; i < m->nStations; i++) {
            if (m->closed[i] || time[i] > t) continue;
            reached++;
            S.stationColor[i] = time[i] <= 8 ? 92 : time[i] <= 16 ? 93 : 91;
            if (t >= maxT && pe[i] >= 0) S.edgeMode[pe[i]] = 1;   /* final escape tree */
        }
        S.burn = time;
        S.overlayWave = 1;
        S.overlayT = t >= maxT ? -1 : t;
        anim_home();
        render_map(m, &S, NULL);
        printf("  ");
        cprintf(96, "RESCUE WAVE");
        printf("  spreading from every exit at once   +%4.1f min   ", t);
        eol();
        printf("  stations reached %2d/%-2d ", reached, m->nStations);
        bar(maxT > 0 ? t / maxT : 1, 40, 96);
        eol();
        if (t >= maxT) cprintf(92, "  every reachable station has an escape path - glowing tracks = escape tree");
        eol();
        sleep_ms(g_animMs);
        if (t >= maxT) break;
    }
    anim_end();
}
