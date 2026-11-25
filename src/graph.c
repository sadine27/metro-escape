/*
 * GRAPH + HASH TABLE + LOADER
 * The metro is an undirected multigraph stored as adjacency lists.
 * Station codes ("CEN") are mapped to ids with a chained hash table.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "metro.h"

/* ---------------- hash table (djb2, separate chaining) ---------------- */
static unsigned hash_code(const char *s)
{
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned)toupper((unsigned char)*s++);
    return h % HASH_SIZE;
}

static void hash_put(Metro *m, const char *code, int id)
{
    unsigned b = hash_code(code);
    HashEntry *e = (HashEntry *)malloc(sizeof(HashEntry));
    int i;
    for (i = 0; i < 3 && code[i]; i++) e->key[i] = (char)toupper((unsigned char)code[i]);
    e->key[i] = '\0';
    e->id = id;
    e->next = m->table[b];
    m->table[b] = e;
}

unsigned hash_bucket(const char *code) { return hash_code(code); }

int hash_get(Metro *m, const char *code)
{
    HashEntry *e;
    char up[4];
    int i;
    for (i = 0; i < 3 && code[i]; i++) up[i] = (char)toupper((unsigned char)code[i]);
    up[i] = '\0';
    if (i == 0 || code[i] != '\0') return -1;   /* empty or longer than a code */
    for (e = m->table[hash_code(up)]; e; e = e->next)
        if (strcmp(e->key, up) == 0) return e->id;
    return -1;
}

void hash_stats(Metro *m, int *used, int *longest)
{
    int b, len;
    HashEntry *e;
    *used = *longest = 0;
    for (b = 0; b < HASH_SIZE; b++) {
        for (len = 0, e = m->table[b]; e; e = e->next) len++;
        if (len) (*used)++;
        if (len > *longest) *longest = len;
    }
}

/* ---------------- helpers ---------------- */
int line_by_key(Metro *m, char key)
{
    int i;
    key = (char)toupper((unsigned char)key);
    for (i = 0; i < m->nLines; i++)
        if (m->ln[i].key == key) return i;
    return -1;
}

/* map distance; terminal cells are ~2x taller than wide */
double station_dist(const Metro *m, int a, int b)
{
    double dx = m->st[a].x - m->st[b].x;
    double dy = 2.0 * (m->st[a].y - m->st[b].y);
    return sqrt(dx * dx + dy * dy);
}

int edge_other(const Metro *m, int e, int from)
{
    return m->ed[e].u == from ? m->ed[e].v : m->ed[e].u;
}

static void add_adj(Metro *m, int from, int to, int e)
{
    AdjNode *n = (AdjNode *)malloc(sizeof(AdjNode));
    n->edge = e;
    n->to = to;
    n->next = m->adj[from];
    m->adj[from] = n;
}

static void trim(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

/* ---------------- building a network ---------------- */
void metro_init(Metro *m)
{
    memset(m, 0, sizeof *m);
    m->transferPenalty = 4;
    m->trie = trie_new();
}

/* returns new station id, or -1 if full / duplicate code */
int metro_add_station(Metro *m, const char *code, const char *name, int x, int y, int lx, int ly)
{
    Station *s;
    int i;
    if (m->nStations >= MAX_STATIONS || hash_get(m, code) >= 0) return -1;
    s = &m->st[m->nStations];
    memset(s, 0, sizeof *s);
    for (i = 0; i < 3 && code[i]; i++) s->code[i] = (char)toupper((unsigned char)code[i]);
    strncpy(s->name, name, NAME_LEN - 1);
    s->x = x; s->y = y; s->lx = lx; s->ly = ly;
    hash_put(m, s->code, m->nStations);
    return m->nStations++;
}

int metro_add_edge(Metro *m, int line, int u, int v, int minutes)
{
    Edge *e;
    if (m->nEdges >= MAX_EDGES || u == v || minutes <= 0) return -1;
    e = &m->ed[m->nEdges];
    e->u = u; e->v = v; e->line = line; e->minutes = minutes;
    add_adj(m, u, v, m->nEdges);
    add_adj(m, v, u, m->nEdges);
    m->st[u].lineMask |= 1 << line;
    m->st[v].lineMask |= 1 << line;
    return m->nEdges++;
}

/* build trie, heuristic scale and delay trees once the network is complete */
void metro_finalize(Metro *m)
{
    int i;
    for (i = 0; i < m->nStations; i++) {          /* code, full name, each word */
        const char *p = m->st[i].name;
        trie_insert(m->trie, m->st[i].code, i);
        trie_insert(m->trie, p, i);
        for (; *p; p++)
            if (*p == ' ' && p[1]) trie_insert(m->trie, p + 1, i);
    }

    /* A* heuristic: fastest "minutes per map unit" over all tracks,
       so straight-line distance * minPerUnit never overestimates. */
    m->minPerUnit = 1e9;
    for (i = 0; i < m->nEdges; i++) {
        double d = station_dist(m, m->ed[i].u, m->ed[i].v);
        if (d > 0 && m->ed[i].minutes / d < m->minPerUnit) m->minPerUnit = m->ed[i].minutes / d;
    }
    if (m->minPerUnit > 1e8) m->minPerUnit = 0;

    for (i = 0; i < m->nLines; i++) seg_init(&m->delay[i], DAY_MIN);
}

/* ---------------- loader ---------------- */
int metro_load(Metro *m, const char *path)
{
    FILE *f = fopen(path, "r");
    char buf[256];
    int lineNo = 0;

    if (!f) return 0;
    metro_init(m);

    while (fgets(buf, sizeof buf, f)) {
        char kw[16];
        lineNo++;
        trim(buf);
        if (buf[0] == '\0' || buf[0] == '#') continue;
        if (sscanf(buf, "%15s", kw) != 1) continue;

        if (strcmp(kw, "LINE") == 0) {
            Line L;
            if (m->nLines < MAX_LINES &&
                sscanf(buf, "LINE %c %15s %d", &L.key, L.name, &L.color) == 3 &&
                line_by_key(m, L.key) < 0) {
                L.key = (char)toupper((unsigned char)L.key);
                m->ln[m->nLines++] = L;
            } else fprintf(stderr, "%s:%d bad LINE\n", path, lineNo);

        } else if (strcmp(kw, "STATION") == 0) {
            char code[4];
            int x, y, lx, ly, n = 0;
            if (sscanf(buf, "STATION %3s %d %d %d %d %n", code, &x, &y, &lx, &ly, &n) != 5 || n == 0 ||
                metro_add_station(m, code, buf + n, x, y, lx, ly) < 0)
                fprintf(stderr, "%s:%d bad STATION\n", path, lineNo);

        } else if (strcmp(kw, "EDGE") == 0) {
            char key, a[4], b[4];
            int mins, li, u, v;
            if (sscanf(buf, "EDGE %c %3s %3s %d", &key, a, b, &mins) != 4 ||
                (li = line_by_key(m, key)) < 0 ||
                (u = hash_get(m, a)) < 0 || (v = hash_get(m, b)) < 0 ||
                metro_add_edge(m, li, u, v, mins) < 0)
                fprintf(stderr, "%s:%d bad EDGE\n", path, lineNo);

        } else if (strcmp(kw, "EXIT") == 0) {
            char a[4];
            int u;
            if (sscanf(buf, "EXIT %3s", a) == 1 && (u = hash_get(m, a)) >= 0)
                m->st[u].isExit = 1;
            else fprintf(stderr, "%s:%d bad EXIT\n", path, lineNo);
        }
    }
    fclose(f);
    metro_finalize(m);
    return m->nStations > 0;
}

void metro_free(Metro *m)
{
    int i;
    for (i = 0; i < MAX_STATIONS; i++) {
        AdjNode *a = m->adj[i];
        while (a) { AdjNode *n = a->next; free(a); a = n; }
        m->adj[i] = NULL;
    }
    for (i = 0; i < HASH_SIZE; i++) {
        HashEntry *e = m->table[i];
        while (e) { HashEntry *n = e->next; free(e); e = n; }
        m->table[i] = NULL;
    }
    for (i = 0; i < m->nLines; i++) seg_free(&m->delay[i]);
    trie_free(m->trie);
    m->trie = NULL;
}
