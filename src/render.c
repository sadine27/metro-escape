/*
 * ASCII MAP RENDERER
 * Draws the network onto a 2D character canvas (a matrix of cells),
 * rasterising each track as a line between station coordinates.
 * Optional layers: a spreading overlay (fire / rescue wave) that crawls
 * along tracks cell by cell, and sprites (the moving train).
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "metro.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MAP_W 78
#define MAP_H 22
#define A_BOLD 1
#define A_REV  2

int g_color  = 1;      /* ANSI colours on?            */
int g_anim   = 1;      /* animations on?              */
int g_animMs = 40;     /* base frame time             */

typedef struct { char ch; unsigned char fg; unsigned char attr; } Cell;
static Cell cv[MAP_H][MAP_W];

/* ---------------- terminal helpers ---------------- */
void cprintf(int sgr, const char *fmt, ...)
{
    va_list ap;
    if (g_color && sgr) printf("\x1b[%dm", sgr);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_color && sgr) printf("\x1b[0m");
}

/* end a line, wiping leftovers from a previous animation frame */
void eol(void)
{
    if (g_color) printf("\x1b[K");
    putchar('\n');
}

void sleep_ms(int ms)
{
    fflush(stdout);
    if (ms <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

int  anim_on(void)    { return g_color && g_anim; }
void anim_begin(void) { if (g_color) printf("\x1b[2J\x1b[H\x1b[?25l"); }
void anim_home(void)  { if (g_color) printf("\x1b[H"); }
void anim_end(void)   { if (g_color) printf("\x1b[J\x1b[?25h"); fflush(stdout); }

void fmt_time(int minute, char out[6])
{
    minute = ((minute % DAY_MIN) + DAY_MIN) % DAY_MIN;
    sprintf(out, "%02d:%02d", minute / 60, minute % 60);
}

/* ---------------- styles ---------------- */
void style_default(MapStyle *s)
{
    memset(s, 0, sizeof *s);
    s->overlayPct = 100;
}

/* highlight a route: its tracks glow, everything else fades */
void style_route(Metro *m, MapStyle *s, const Route *r)
{
    int i;
    style_default(s);
    if (!r->found) return;
    for (i = 0; i < m->nEdges; i++) s->edgeMode[i] = 2;
    for (i = 0; i + 1 < r->len; i++) s->edgeMode[r->edge[i]] = 1;
    for (i = 0; i < r->len; i++) s->stationColor[r->station[i]] = 97;
    s->stationColor[r->station[0]] = 96;
    s->stationColor[r->station[r->len - 1]] = 96;
    s->stationMark[r->station[0]] = 'S';
    s->stationMark[r->station[r->len - 1]] = 'E';
}

/* ---------------- geometry ---------------- */
static int iabs(int v) { return v < 0 ? -v : v; }

static int edge_steps(const Metro *m, int e)
{
    const Station *a = &m->st[m->ed[e].u], *b = &m->st[m->ed[e].v];
    int dx = iabs(b->x - a->x), dy = iabs(b->y - a->y);
    return dx > dy ? dx : dy;
}

/* cell i of edge e, counted from endpoint u (the same rasterisation everywhere) */
static void edge_cell(const Metro *m, int e, int i, int *x, int *y)
{
    const Station *a = &m->st[m->ed[e].u], *b = &m->st[m->ed[e].v];
    int steps = edge_steps(m, e);
    if (steps == 0) { *x = a->x; *y = a->y; return; }
    *x = a->x + (int)floor((b->x - a->x) * (double)i / steps + 0.5);
    *y = a->y + (int)floor((b->y - a->y) * (double)i / steps + 0.5);
}

/* point at fraction f (0..1) along edge e, travelling away from station `from` */
void map_point(const Metro *m, int e, int from, double f, int *x, int *y)
{
    int steps = edge_steps(m, e);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    if (from != m->ed[e].u) f = 1 - f;
    edge_cell(m, e, (int)floor(f * steps + 0.5), x, y);
}

/* when does the spreading overlay reach fraction f of edge e (measured from `from`)? */
double overlay_time_at(const Metro *m, const int *burn, int pct, int e, int from, double f)
{
    int u = m->ed[e].u, v = m->ed[e].v;
    double W = (m->ed[e].minutes * pct + 99) / 100, tu, tv;
    if (from != u) f = 1 - f;                     /* now f is measured from u */
    tu = burn[u] >= INF ? 1e18 : burn[u] + f * W;
    tv = burn[v] >= INF ? 1e18 : burn[v] + (1 - f) * W;
    return tu < tv ? tu : tv;
}

/* ---------------- drawing ---------------- */
static void put(int x, int y, char ch, int fg, int attr)
{
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
    cv[y][x].ch = ch;
    cv[y][x].fg = (unsigned char)fg;
    cv[y][x].attr = (unsigned char)attr;
}

static void draw_edge(Metro *m, int e, int mode)
{
    int steps = edge_steps(m, e), i;
    int px = m->st[m->ed[e].u].x, py = m->st[m->ed[e].u].y;
    int fg = m->ln[m->ed[e].line].color;

    if (mode == 0 && (m->closed[m->ed[e].u] || m->closed[m->ed[e].v])) mode = 2;
    for (i = 1; i < steps; i++) {
        int x, y;
        char ch;
        edge_cell(m, e, i, &x, &y);
        if (y == py)      ch = '-';
        else if (x == px) ch = '|';
        else              ch = ((x - px) * (y - py) > 0) ? '\\' : '/';
        if (mode == 2)      put(x, y, '.', 90, 0);
        else if (mode == 1) put(x, y, g_color ? ch : '#', fg, A_BOLD | A_REV);
        else                put(x, y, ch, fg, 0);
        px = x; py = y;
    }
}

/* fire / wave crawling along a track: each cell lights up at its own time */
static void draw_overlay(Metro *m, const MapStyle *s, int e)
{
    int steps = edge_steps(m, e), i;
    if (s->overlayWave && (m->closed[m->ed[e].u] || m->closed[m->ed[e].v])) return;
    for (i = 1; i < steps; i++) {
        double tb = overlay_time_at(m, s->burn, s->overlayPct, e, m->ed[e].u, (double)i / steps);
        int x, y, front;
        if (tb > s->overlayT) continue;
        edge_cell(m, e, i, &x, &y);
        front = s->overlayT - tb < 1.5;            /* the leading edge glows */
        if (s->overlayWave)
            put(x, y, front ? '~' : '=', front ? 96 : 36, front ? A_BOLD | A_REV : A_BOLD);
        else
            put(x, y, "~^*"[(x * 7 + y * 3 + s->frame) % 3],
                ((x + y + s->frame) & 1) ? 91 : 93, front ? A_BOLD | A_REV : A_BOLD);
    }
}

static int popcount(int v) { int c = 0; while (v) { c += v & 1; v >>= 1; } return c; }

static void emit_row(int y)
{
    int x, fg = -1, at = -1;
    putchar('|');
    for (x = 0; x < MAP_W; x++) {
        Cell c = cv[y][x];
        if (g_color && (c.fg != fg || c.attr != at)) {
            printf("\x1b[0m");
            if (c.attr & A_BOLD) printf("\x1b[1m");
            if (c.attr & A_REV)  printf("\x1b[7m");
            if (c.fg) printf("\x1b[%dm", c.fg);
            fg = c.fg; at = c.attr;
        }
        putchar(c.ch);
    }
    if (g_color) printf("\x1b[0m");
    putchar('|');
    eol();
}

static void hrule(void)
{
    int x;
    putchar('+');
    for (x = 0; x < MAP_W; x++) putchar('-');
    putchar('+');
    eol();
}

void render_map(Metro *m, const MapStyle *s, const char *title)
{
    static const int passes[3] = { 2, 0, 1 };   /* faded, normal, highlight on top */
    int x, y, i, p;

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++) { cv[y][x].ch = ' '; cv[y][x].fg = 0; cv[y][x].attr = 0; }

    for (p = 0; p < 3; p++)
        for (i = 0; i < m->nEdges; i++)
            if (s->edgeMode[i] == passes[p]) draw_edge(m, i, passes[p]);
    if (s->burn)
        for (i = 0; i < m->nEdges; i++) draw_overlay(m, s, i);

    for (i = 0; i < m->nStations; i++) {
        const Station *st = &m->st[i];
        char mark = s->stationMark[i];
        int fg = s->stationColor[i], k, onFire = 0;
        if (s->burn && !s->overlayWave && s->burn[i] <= s->overlayT) onFire = 1;
        if (!mark) mark = m->closed[i] ? 'X' : st->isExit ? '@' : popcount(st->lineMask) > 1 ? 'O' : 'o';
        if (onFire)            put(st->x, st->y, '*', (s->frame & 1) ? 91 : 93, A_BOLD | A_REV);
        else if (m->closed[i]) put(st->x, st->y, mark, 91, A_BOLD | A_REV);
        else                   put(st->x, st->y, mark, fg ? fg : 97, A_BOLD);
        for (k = 0; k < 3; k++)
            put(st->x + st->lx + k, st->y + st->ly, st->code[k],
                (m->closed[i] || onFire) ? 91 : (fg ? fg : 37), (fg || onFire) ? A_BOLD : 0);
    }
    for (i = 0; i < s->nSprites; i++)
        put(s->spriteX[i], s->spriteY[i], s->spriteCh[i], s->spriteFg[i], A_BOLD | A_REV);

    if (!s->compact) printf("\n");
    hrule();
    if (title && !s->compact) {
        int len = (int)strlen(title), pad = (MAP_W - len) / 2;
        if (pad < 0) pad = 0;
        printf("|%*s", pad, "");
        cprintf(1, "%s", title);
        printf("%*s|\n", MAP_W - pad - len > 0 ? MAP_W - pad - len : 0, "");
        hrule();
    }
    for (y = 0; y < MAP_H; y++) emit_row(y);
    hrule();
    if (s->compact) return;

    printf(" ");
    for (i = 0; i < m->nLines; i++) {
        cprintf(m->ln[i].color, "--- %s", m->ln[i].name);
        printf("   ");
    }
    printf("\n o station   O interchange   @ city exit   ");
    cprintf(91, "X closed");
    printf("   . unused track\n");
}
