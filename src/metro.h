/*
 * METRO ESCAPE - a transit planner & emergency simulator
 * ------------------------------------------------------
 * Shared types and prototypes for every module.
 *
 *   graph.c     adjacency-list graph, hash table, file loader
 *   heap.c      binary min-heap (priority queue)
 *   trie.c      prefix tree for station autocomplete
 *   segtree.c   lazy segment tree for time-window delays
 *   dsu.c       union-find (disjoint set union)
 *   route.c     Dijkstra, A*, BFS, Yen's K-shortest, deadline escape
 *   analysis.c  fire/evacuation spread, Tarjan, Kruskal, islands
 *   render.c    ASCII map renderer + animation helpers
 *   anim.c      animated search race, train ride, fire escape, evac wave
 *   bench.c     900-station scale benchmark
 *   incidents.c incident stack (undo) + shared clock
 *   server.c    tiny HTTP/JSON server that powers the browser GUI
 *   main.c      menus, incident stack, guided demo
 */
#ifndef METRO_H
#define METRO_H

#define MAX_STATIONS 1024
#define MAX_LINES       8
#define MAX_EDGES    4096
#define MAX_PATH      256
#define MAX_INCIDENTS  32
#define NAME_LEN       32
#define DAY_MIN      1440            /* minutes in a day            */
#define NO_LINE   MAX_LINES          /* "not riding any line yet"   */
#define NL1      (MAX_LINES + 1)
#define NSTATE   (MAX_STATIONS * NL1)
#define INF      1000000000
#define HASH_SIZE     101
#define TRIE_ALPHA     37            /* a-z, 0-9, space             */
#define TRIE_IDS        8
#define ANY_EXIT      (-1)           /* route target: nearest open exit */

/* ------------------------------------------------------------------ */
/*  Core graph                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    char code[4];
    char name[NAME_LEN];
    int  x, y;           /* position on ASCII map  */
    int  lx, ly;         /* label offset           */
    int  isExit;
    int  lineMask;       /* bit i => served by line i */
} Station;

typedef struct {
    char key;            /* 'R' */
    char name[16];       /* "Red" */
    int  color;          /* ANSI SGR colour code */
} Line;

typedef struct { int u, v, line, minutes; } Edge;

typedef struct AdjNode { int edge, to; struct AdjNode *next; } AdjNode;

typedef struct HashEntry { char key[4]; int id; struct HashEntry *next; } HashEntry;

typedef struct TrieNode {
    struct TrieNode *child[TRIE_ALPHA];
    int ids[TRIE_IDS];   /* stations whose word ends here */
    int nids;
} TrieNode;

typedef struct { int n; int *mx; int *lz; } SegTree;

typedef struct { int key, val; } HeapItem;
typedef struct { HeapItem *data; int size, cap; } MinHeap;

typedef struct { int parent[MAX_STATIONS]; int rank[MAX_STATIONS]; } DSU;

typedef struct {
    int nStations, nLines, nEdges;
    Station st[MAX_STATIONS];
    Line    ln[MAX_LINES];
    Edge    ed[MAX_EDGES];
    AdjNode   *adj[MAX_STATIONS];
    HashEntry *table[HASH_SIZE];
    TrieNode  *trie;
    SegTree    delay[MAX_LINES];    /* per-line delay over the day */
    int  closed[MAX_STATIONS];
    int  transferPenalty;           /* minutes to change lines     */
    double minPerUnit;              /* A* heuristic scale          */
} Metro;

/* A route = sequence of stations and the edges between them. */
typedef struct {
    int found;
    int len;                 /* number of stations                 */
    int station[MAX_PATH];
    int edge[MAX_PATH];      /* edge[i]: station[i] -> station[i+1] */
    int arrive[MAX_PATH];    /* clock minute at station[i]          */
    int total;               /* minutes                             */
    int transfers;
    int explored;            /* states popped from the heap         */
} Route;

typedef struct { char bannedNode[MAX_STATIONS]; char bannedEdge[MAX_EDGES]; } Bans;

typedef enum { INC_CLOSE, INC_DELAY } IncType;
typedef struct {
    IncType type;
    int station, line, from, to, amount;
} Incident;

#define MAX_SPRITES 4
typedef struct {
    int  edgeMode[MAX_EDGES];        /* 0 normal, 1 highlight, 2 faded */
    int  stationColor[MAX_STATIONS]; /* 0 = default                    */
    char stationMark[MAX_STATIONS];  /* 0 = default                    */
    int  compact;                    /* frame for animation: no title/legend */
    /* spreading overlay (fire or evacuation wave) */
    const int *burn;                 /* minute each station is reached, or NULL */
    int    overlayPct;               /* spread speed: track minutes * pct/100   */
    int    overlayWave;              /* 0 = fire palette, 1 = rescue-wave palette */
    double overlayT;                 /* current time of the overlay */
    int    frame;                    /* for flicker */
    /* sprites (the train) drawn on top of everything */
    int  nSprites;
    int  spriteX[MAX_SPRITES], spriteY[MAX_SPRITES], spriteFg[MAX_SPRITES];
    char spriteCh[MAX_SPRITES];
} MapStyle;

/* ---------------- graph.c ---------------- */
void metro_init(Metro *m);
int  metro_add_station(Metro *m, const char *code, const char *name, int x, int y, int lx, int ly);
int  metro_add_edge(Metro *m, int line, int u, int v, int minutes);
void metro_finalize(Metro *m);
int  metro_load(Metro *m, const char *path);
void metro_free(Metro *m);
int  hash_get(Metro *m, const char *code);
void hash_stats(Metro *m, int *used, int *longest);
unsigned hash_bucket(const char *code);
int  line_by_key(Metro *m, char key);
double station_dist(const Metro *m, int a, int b);
int  edge_other(const Metro *m, int e, int from);

/* ---------------- heap.c ----------------- */
void     heap_init(MinHeap *h, int cap);
void     heap_free(MinHeap *h);
void     heap_push(MinHeap *h, int key, int val);
HeapItem heap_pop(MinHeap *h);

/* ---------------- trie.c ----------------- */
TrieNode *trie_new(void);
void      trie_insert(TrieNode *root, const char *word, int id);
int       trie_collect(TrieNode *root, const char *prefix, int out[], int max);
int       trie_count(const TrieNode *t);
int       trie_walk(TrieNode *root, const char *prefix, TrieNode **path, char *chars, int max);
char      trie_char(int idx);
void      trie_free(TrieNode *t);

/* ---------------- segtree.c -------------- */
void seg_init(SegTree *s, int n);
void seg_free(SegTree *s);
void seg_add(SegTree *s, int l, int r, int v);
int  seg_max(SegTree *s, int l, int r);

/* ---------------- dsu.c ------------------ */
void dsu_init(DSU *d, int n);
int  dsu_find(DSU *d, int x);
int  dsu_union(DSU *d, int a, int b);

/* ---------------- execution recorders (feed the GUI's "X-ray" views) ---------------- */
#define REC_STEPS   512
#define REC_ITEMS 40000
typedef struct {                     /* binary-heap contents after every pop */
    int nSteps, nItems;
    int popKey[REC_STEPS], popState[REC_STEPS], start[REC_STEPS], len[REC_STEPS];
    int item[REC_ITEMS][3];          /* key, state, stale? */
} HeapRecord;
enum { DFS_ENTER, DFS_BACK, DFS_RET, DFS_ART, DFS_BRIDGE };
#define REC_DFS 4096
typedef struct { int n; int ev[REC_DFS][4]; } DfsRecord;       /* type, a, b, c */
#define REC_KRU 256
typedef struct { int n; int edge[REC_KRU], ok[REC_KRU]; int parent[REC_KRU][64]; } KruskalRecord;
#define REC_SEG 1024
typedef struct { int n; int node[REC_SEG], full[REC_SEG]; } SegRecord;
extern int g_recHeap, g_recDfs, g_recKruskal, g_recSeg;
extern HeapRecord g_heapRec;
extern DfsRecord g_dfsRec;
extern KruskalRecord g_kruRec;
extern SegRecord g_segRec;

/* ---------------- route.c ---------------- */
extern int g_trace[NSTATE];          /* stations in the order the last search popped them */
extern int g_traceLen;
int  delay_at(Metro *m, int line, int clock);
int  edge_cost(Metro *m, int e, int curLine, int clock);
int  route_search(Metro *m, int src, int dst, int depart, int startLine,
                  int useAstar, const Bans *bans, Route *out);
int  route_escape(Metro *m, int src, int depart, const int *deadline, Route *out);
int  route_fewest_stops(Metro *m, int src, int dst, int depart, Route *out);
int  route_k_shortest(Metro *m, int src, int dst, int depart, int K, Route out[]);
void route_simulate(Metro *m, Route *r, int depart);

/* ---------------- analysis.c ------------- */
void spread(Metro *m, const int src[], int nsrc, int pct, int respectClosed,
            int time[], int origin[], int parentEdge[]);
void evacuate(Metro *m, int time[], int exitOf[], int parentEdge[]);
void find_critical(Metro *m, int isArt[], int isBridge[]);
int  kruskal_mst(Metro *m, int inMst[]);
int  find_islands(Metro *m, int comp[]);
int  stranded_if_closed(Metro *m, int s);

/* ---------------- render.c --------------- */
extern int g_color, g_anim, g_animMs;
void cprintf(int sgr, const char *fmt, ...);
void eol(void);
void sleep_ms(int ms);
int  anim_on(void);
void anim_begin(void);
void anim_home(void);
void anim_end(void);
void style_default(MapStyle *s);
void style_route(Metro *m, MapStyle *s, const Route *r);
void render_map(Metro *m, const MapStyle *s, const char *title);
void map_point(const Metro *m, int e, int from, double f, int *x, int *y);
double overlay_time_at(const Metro *m, const int *burn, int pct, int e, int from, double f);
void fmt_time(int minute, char out[6]);

/* ---------------- incidents.c ------------ */
extern int g_clock;                  /* simulation clock, minutes after midnight */
int  incident_push(Metro *m, Incident in);
int  incident_pop(Metro *m, Incident *out);
int  incident_count(void);
const Incident *incident_at(int i);

/* ---------------- anim.c ----------------- */
typedef struct {
    int    burn[MAX_STATIONS];       /* minute the fire reaches each station (INF = never) */
    Route  naive, smart;             /* nearest-exit route vs deadline-aware route */
    double naiveCaught;              /* minutes until the fire touches the naive train, INF = never */
    int    margin;                   /* smart route's smallest head start over the fire */
    int    shelter;                  /* if no escape: station the fire reaches last */
} FirePlan;
void fire_compute(Metro *m, int me, int fire, int pct, int depart, FirePlan *p);
void anim_search_race(Metro *m, int src, int dst, int depart, Route *dj, Route *as);
void anim_ride(Metro *m, const Route *r, const char *title);
int  fire_escape(Metro *m, int me, int fire, int pct, int depart);
int  find_fire_scenario(Metro *m, int pct, int depart, int *me, int *fire);
void anim_evac_wave(Metro *m, const int time[], const int pe[]);

/* ---------------- bench.c ---------------- */
typedef struct {
    int    stations, edges, states;
    double expD, expA, msD, msA;     /* avg states explored, total ms for 300 trips */
    int    mismatch;
    double msArray, msHeap;          /* 100 all-destination Dijkstras */
    int    sameDist;
    double msYen, msTarjan, msKruskal, msDsu, nsHash, nsTrie;
    int    nArt, mstCost;
} BenchResult;
double now_ms(void);
void bench_compute(BenchResult *r);
void run_benchmark(void);

/* ---------------- server.c --------------- */
int  server_run(Metro *m, int port, int openBrowser);

#endif
