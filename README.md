# Metro Escape — transit planner & emergency simulator (C engine + browser GUI)

A planner for **Nova City**, a fictional metro city with 5 lines, 23 stations and 32 tracks.
It plans journeys, handles live floods and rush-hour delays, and finds the network's weak points.
In its headline mode, a **fire spreads along the tracks in real time** and the planner has to outrun it.

Every data structure and algorithm is written by hand in **plain C99**, with no libraries.
The program has two faces that share the same engine:

| | How to start it | What you get |
|---|---|---|
| **Browser GUI** (best for demos) | `metro --gui` | A modern dark-theme map with glowing lines, animated trains, fire with embers, an 11-act story mode, and a live "C engine" log |
| **Terminal** | `metro` or `metro --demo` | The same features drawn as an animated ASCII map |

### How the GUI keeps the DSA in C

`metro --gui` starts a tiny HTTP server written in C ([src/server.c](src/server.c), Winsock, about 400 lines).
It opens your browser on `http://localhost:8080`. The page in [web/](web/) **only draws**: every route,
heap-pop trace, fire spread, Tarjan scan, MST and benchmark is computed by the C engine and sent as JSON.
The **"C engine · live calls"** panel on the map shows each call and how many microseconds it took in C.

```
 browser (web/app.js)  --GET /api/route?from=AIR&to=BCH-->  server.c --> route.c (Dijkstra, A*, heap)
       draws + animates  <------------- JSON (path, trace, µs) ---------
```

## Build & run

```bash
build.bat
```
```bash
metro --gui
```

- `build.bat` (or `make`) builds `metro.exe` with MinGW gcc. It links `ws2_32` for the server.
- `metro --gui` starts the engine and opens the GUI. Click **Story mode** (top right) for the 11-act demo.
  Use the arrow keys or Space to move between acts.
- `metro --demo` runs the same story in the terminal. Plain `metro` opens the terminal menu.

### X-ray mode: watch the data structures work

The C engine doesn't only return answers. It also **records its own execution**, and the GUI replays it
on a timeline you can scrub back and forth:

| Screen | What you see | Recorded in C by |
|---|---|---|
| Fastest route | The **binary min-heap** after every pop, drawn as a tree and as an array. Stale entries (lazy deletion) are greyed out | `rec_heap()` in `route.c` |
| Critical stations | **Tarjan's DFS** walking the map: `disc \| low` badges, tree edges, back edges, then critical stations and bridges, each with a one-line reason | `ev()` in `analysis.c` |
| Night maintenance | **Kruskal**: tracks tried cheapest-first, cycles rejected in red, stations coloured by their **union-find set** | `g_kruRec` in `analysis.c` |
| Incidents & delays | The **segment tree** itself: node maxima, lazy tags, and exactly which nodes the last O(log n) update touched | `rec_node()` in `segtree.c` |
| Station search | The **trie path** for your prefix, and the **hash table**'s 101 buckets with the chain your code lands in | `trie_walk()`, `hash_bucket()` |

**GUI tips**
- Click any station to route from or to it, start a fire there, or close it.
- Deep links open a screen directly: `http://localhost:8080/#fire`, `#route`, `#evacuate`, `#story`, ...
- The fire, evacuation and train rides have a timeline: drag it to scrub, or change the speed.
- It works offline. Fonts load from Google Fonts when online and fall back to Segoe UI otherwise.

| Flag | Effect |
|---|---|
| `--gui` | browser GUI (`--port N`, `--no-open`) |
| `--demo` | terminal story mode |
| `--fast` / `--slow` / `--noanim` | terminal animation speed |
| `--nocolor` | plain ASCII terminal |
| `--map FILE` | load a different city (works in both modes) |

Self-tests:
```bash
gcc -std=c99 -O2 -Isrc -o test_routes tests/test_routes.c src/graph.c src/heap.c src/trie.c src/segtree.c src/dsu.c src/route.c src/analysis.c src/render.c src/anim.c src/bench.c src/incidents.c -lm && test_routes
```
They check:
- A* = Dijkstra on all 529 station pairs, under 3 network conditions
- FIFO: leaving later never arrives earlier, over 47,610 timed queries
- the fire-escape route never enters a burning station, across 1,518 fire scenarios
- Yen's ordering
- the segment tree against brute force

## The story mode (11 acts in the GUI, 9 in the terminal)

| Act | What the audience sees | DSA on show |
|---|---|---|
| 1 | Airport → Beach. **Animated**: Dijkstra ripples out, then A* beams in with ~68% less work | binary heap, Dijkstra, A* |
| 2 | **A train rides the route** with a live clock | route timeline, line rasterisation |
| 3 | Rush hour on Green (+7 min, 08:00–10:00). Old plan 80 min → new plan 69 min | lazy segment tree, time-dependent edges |
| 4 | Docklands floods. Top 3 detours | Yen's K-shortest paths |
| 5 | Which stations could break the city, ranked by impact | Tarjan articulation points & bridges |
| 6 | All clear: incidents unwind | stack (undo), union-find |
| 7 | **FIRE at Fort. The tourist is at the Beach.** An ordinary planner heads for South Gate and the train is **CAUGHT**. Metro Escape takes a longer way round and **ESCAPES** with 4 min to spare | Dijkstra fire spread + deadline-constrained Dijkstra |
| 8 | **Rescue wave** sweeps the city from all 5 exits; the escape tree glows | multi-source Dijkstra |
| 9 | 900-station generated city: A* vs Dijkstra, **binary heap ~10× faster than an O(V²) array scan** | complexity in practice |

## DSA used

| Feature | Data structure / algorithm | File | Complexity |
|---|---|---|---|
| Network storage | **Graph – adjacency list** (undirected multigraph) | `graph.c` | O(V+E) space |
| Code → station lookup (`CEN`) | **Hash table** (djb2, separate chaining) | `graph.c` | O(1) avg |
| Autocomplete ("gat" → all Gates) | **Trie** over codes, names and every word | `trie.c` | O(prefix + matches) |
| Fastest route, transfer penalty | **Dijkstra** on a (station, line) *state graph* | `route.c` | O((V·L+E·L) log(V·L)) |
| Same route, less work | **A\*** with an admissible straight-line heuristic | `route.c` | ≤ Dijkstra |
| Priority queue everywhere | **Binary min-heap** (hand-written) | `heap.c` | O(log n) push/pop |
| Fewest stops | **BFS** with an array queue | `route.c` | O(V+E) |
| Top-K alternatives | **Yen's K-shortest loopless paths** + candidate heap | `route.c` | O(K·V·Dijkstra) |
| Delays per time window | **Lazy segment tree** over 1440 minutes, one per line | `segtree.c` | O(log n) |
| Undo incidents | **Stack** | `main.c` | O(1) |
| Fire spread & rescue wave | **Multi-source Dijkstra** (one generic `spread()`) | `analysis.c` | O(E log V) |
| Outrun the fire | **Deadline-constrained Dijkstra** to the nearest exit | `route.c` | O(E log V) |
| Single points of failure | **Tarjan** articulation points & bridges (DFS low-link) | `analysis.c` | O(V+E) |
| Night maintenance plan | **Kruskal's MST** with **merge sort** + **union-find** | `analysis.c`, `dsu.c` | O(E log E) |
| Is the network split? | **Union-find** (path compression + union by rank) | `dsu.c` | ~O(α(n)) |
| Terminal map, animations | **2D matrix** canvas + line rasterisation | `render.c`, `anim.c` | O(W·H) per frame |
| GUI API | HTTP/JSON server with a growable string buffer | `server.c` | - |

## Viva talking points

1. **The state-graph trick.** The cost of the next track depends on which line you are on,
   because changing lines costs a transfer walk. So the search runs on states
   `(station, line)`, not on stations. Real journey planners do this.
2. **Time-dependent edges and FIFO.** An edge costs `minutes + delay(line, clock)`, and the
   delay is a point query on that line's segment tree. A naive version breaks Dijkstra:
   leaving 1 minute later, after a delay window ends, could arrive *earlier*. The fix lets
   passengers wait out a delay: `cost = min over w of (w + delay(t+w))`. That makes arrival
   time non-decreasing in departure time (the FIFO property), which is exactly what keeps
   Dijkstra optimal. The tests check this over 47,610 queries.
3. **A\* heuristic.** `h = straight-line distance × fastest minutes-per-unit of any track`.
   No track is faster, so `h` never overestimates and A* returns Dijkstra's exact answer.
4. **Fire escape.** Step 1: a Dijkstra seeded at the fire gives `burn[v]`, the minute the
   fire reaches each station. Step 2: Dijkstra from you to *any* exit skips a move if you
   would arrive at, or leave, a station at or after `burn[v]`. Because of FIFO, arriving
   earlier is never worse, so the pruned Dijkstra is still exact. If any safe route
   exists, it finds the fastest one.
5. **Evacuation is the same algorithm as the fire**: `spread()` seeded at every exit at once.
6. **Yen's algorithm.** Each node of the previous best path becomes a "spur" in turn.
   Edges already used from the same root are banned, and a new shortest path is found from
   the spur. Root + spur goes into a min-heap of candidates.
7. **Tarjan.** Station `u` is an articulation point if a DFS child `v` has `low[v] ≥ disc[u]`.
   The impact of each one ("strands N stations") is counted with union-find and ranked
   using the heap.
8. **Heap vs array (Act 9).** Same Dijkstra, O(E log V) vs O(V²). On 900 stations the heap
   is roughly 8–14× faster (varies by machine), with identical distances.

## Try live in front of the examiner

In the GUI:
- **Fire escape** → **Surprise me**: the C engine picks a dramatic scenario. Or click two stations: one to start the fire, one for "I'm here".
- Click **Tower** → *Close this station*, then **Evacuate the city**: Hill Top is **TRAPPED**.
- **Incidents & delays** → add a delay. The segment-tree heatmap lights up, and **Fastest route** routes around it.

In the terminal: menu **9** (fire), **6** then **10** (close Tower, evacuate), **6** then **8** then **2** (delay, timeline, reroute).

## Customise the city

Everything is in [data/metro.txt](data/metro.txt): lines, stations with map coordinates,
tracks with travel times, and exits. Edit it and rerun; no recompiling is needed. The fire
mode's auto-picker finds a dramatic scenario on any map.

## Project layout

```
src/metro.h      shared types + prototypes
src/graph.c      adjacency list, hash table, loader
src/heap.c       binary min-heap
src/trie.c       trie autocomplete
src/segtree.c    lazy segment tree
src/dsu.c        union-find
src/route.c      Dijkstra, A*, BFS, Yen, deadline escape
src/analysis.c   spread (fire/evacuation), Tarjan, Kruskal, islands
src/render.c     ASCII map renderer + overlay + sprites
src/anim.c       search race, train ride, fire escape, rescue wave
src/bench.c      900-station benchmark
src/incidents.c incident stack (undo) + shared clock
src/server.c     HTTP/JSON server for the GUI
src/main.c       menus, boot log, terminal demo
web/             GUI: index.html, style.css, app.js (drawing only)
tests/           algorithm self-tests
data/metro.txt   the city
```
