"""Builds report/Metro_Escape_Report.docx and .odt from one content list."""
import os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "report")
os.makedirs(OUT, exist_ok=True)
SHOT = os.path.join(ROOT, "screenshots", "gui-story.png")
PROJ = ROOT

# ---------------------------------------------------------------- content
# blocks: ("title",t) ("sub",t) ("h1",t) ("h2",t) ("p",t) ("b",[items]) ("n",[items])
#         ("cmd",[lines]) ("t",[header],[rows]) ("img",path,caption) ("note",t) ("pb",)
C = []
A = C.append

A(("title", "Metro Escape"))
A(("sub", "Transit planner & emergency simulator - DSA Lab Project"))
A(("sub", "C engine (hand-written data structures) + browser GUI + terminal UI"))
A(("img", SHOT, "Figure 1. The browser GUI in story mode. Every route and simulation is computed by the C engine."))
A(("pb",))

A(("h1", "1. What the project is"))
A(("p", "Metro Escape plans journeys on the metro network of a fictional city, Nova City (5 lines, 23 stations, 32 tracks). "
        "It handles live disruptions (station closures and time-window delays), finds the network's weak points, plans a "
        "night-maintenance schedule and an evacuation, and - in its headline mode - outruns a fire that spreads along the "
        "tracks in real time."))
A(("p", "Every data structure and algorithm is written by hand in plain C99 with no libraries. The same C engine has two faces:"))
A(("b", ["Browser GUI (metro --gui): a modern animated map. The C program runs a small local web server; the page only draws what C sends back.",
         "Terminal UI (metro, metro --demo): the same features drawn as an animated, coloured ASCII map."]))
A(("t", ["Data structure / algorithm", "Used for", "Complexity"], [
    ["Graph (adjacency list)", "Storing the metro network", "O(V+E) space"],
    ["Hash table (djb2, chaining)", "Station code -> station id", "O(1) average"],
    ["Trie", "Autocomplete over codes, names and words", "O(prefix + matches)"],
    ["Binary min-heap", "Priority queue for every Dijkstra / A*", "O(log n) push/pop"],
    ["Dijkstra on a (station, line) state graph", "Fastest route with transfer penalties", "O((V+E) log V)"],
    ["A* search", "Same route, fewer states explored", "<= Dijkstra"],
    ["BFS with a queue", "Fewest-stops route", "O(V+E)"],
    ["Yen's K-shortest paths", "Top-K alternative routes", "O(K V Dijkstra)"],
    ["Lazy segment tree", "Delays for a time window per line", "O(log n) update/query"],
    ["Stack", "Incident log with undo", "O(1)"],
    ["Multi-source Dijkstra", "Fire spread and evacuation", "O(E log V)"],
    ["Deadline-constrained Dijkstra", "Escaping the fire", "O(E log V)"],
    ["Tarjan's DFS (low-link)", "Critical stations and bridges", "O(V+E)"],
    ["Kruskal + merge sort + union-find", "Minimum set of tracks (maintenance)", "O(E log E)"],
    ["Union-find (DSU)", "Connected components / islands", "~O(alpha(n))"],
]))

# ---------------------------------------------------------------- commands
A(("pb",))
A(("h1", "2. Every command you need, and where to run it"))
A(("h2", "2.1 Where to run the commands"))
A(("p", "Open a terminal in the project folder. All commands in this report are run from that folder:"))
A(("cmd", [PROJ]))
A(("b", ["Easiest: open the folder in File Explorer, right-click an empty area and choose 'Open in Terminal' (Windows Terminal / PowerShell).",
         "Or open PowerShell and type the cd command below.",
         "Windows Terminal maximised is recommended for the terminal version (colours + animations need at least 80x32)."]))
A(("cmd", [f'cd "{PROJ}"']))
A(("note", "PowerShell vs Command Prompt: in PowerShell a program in the current folder must be started with .\\ in front "
           "(for example .\\metro --gui). In the old Command Prompt (cmd.exe) you can type just metro --gui. "
           "This report shows the PowerShell form."))

A(("h2", "2.2 Requirements (one time)"))
A(("b", ["MinGW gcc (already installed on this laptop at C:\\MinGW\\bin). Check with: gcc --version",
         "A modern browser (Chrome or Edge) for the GUI.",
         "Nothing else - no libraries, no internet needed (fonts fall back to Segoe UI when offline)."]))
A(("cmd", ["gcc --version"]))

A(("h2", "2.3 Build the program"))
A(("p", "Compile all C files into metro.exe. Run this once, and again after changing any .c file:"))
A(("cmd", [".\\build.bat"]))
A(("p", "Expected output: 'Built metro.exe - run: metro --gui (browser) or metro --demo (terminal)'. The equivalent manual command is:"))
A(("cmd", ["gcc -std=c99 -O2 -Wall -Wextra -o metro.exe src\\main.c src\\graph.c src\\heap.c src\\trie.c src\\segtree.c src\\dsu.c src\\route.c src\\analysis.c src\\render.c src\\anim.c src\\bench.c src\\incidents.c src\\server.c -lm -lws2_32"]))
A(("p", "If you have make: mingw32-make (build), mingw32-make gui, mingw32-make demo."))

A(("h2", "2.4 The browser GUI (best for the demo)"))
A(("cmd", [".\\metro --gui"]))
A(("b", ["The C engine starts a local server and opens http://localhost:8080/ in your browser automatically.",
         "Keep the terminal window open - it IS the engine. Stop it with Ctrl+C.",
         "If the browser does not open, type http://localhost:8080/ into it yourself.",
         "If port 8080 is busy, the program tries 8081, 8082 ... and prints the address it used."]))
A(("t", ["Command", "What it does"], [
    [".\\metro --gui", "Start engine + open the GUI"],
    [".\\metro --gui --port 9000", "Use a different port (http://localhost:9000/)"],
    [".\\metro --gui --no-open", "Start the engine without opening a browser"],
    [".\\metro --gui --map data\\metro.txt", "Load a specific city file"],
]))
A(("h2", "2.5 Direct links into the GUI (deep links)"))
A(("p", "While metro --gui is running, these addresses open a screen directly - useful for jumping around during a viva:"))
A(("t", ["Address", "Opens"], [
    ["http://localhost:8080/#story", "Story mode (11-act guided demo)"],
    ["http://localhost:8080/#route", "Fastest route: Dijkstra vs A* race + heap X-ray"],
    ["http://localhost:8080/#alternatives", "Yen's top-K alternative routes"],
    ["http://localhost:8080/#fewest", "Fewest stops (BFS)"],
    ["http://localhost:8080/#search", "Trie path + hash table X-ray"],
    ["http://localhost:8080/#incidents", "Closures, delays, segment-tree X-ray"],
    ["http://localhost:8080/#fire", "Fire escape"],
    ["http://localhost:8080/#evacuate", "Evacuation wave"],
    ["http://localhost:8080/#critical", "Tarjan DFS replay"],
    ["http://localhost:8080/#islands", "Union-find islands"],
    ["http://localhost:8080/#mst", "Kruskal + union-find replay"],
    ["http://localhost:8080/#bench", "900-station benchmark"],
]))
A(("h2", "2.6 Using the GUI"))
A(("b", ["Story mode button (top right): runs the guided demo. Next / Previous buttons, or Right-arrow / Space / Left-arrow; Esc closes it.",
         "Left sidebar: every feature, grouped as Plan, Disrupt, Emergency, Lab.",
         "Click any station on the map: route from / to it, start a fire there, mark 'I'm here', or close it.",
         "Station boxes autocomplete as you type (the trie in C answers each keystroke).",
         "Timeline bar under the map (fire, evacuation, ride, X-ray replays): play/pause, drag to scrub, speed 0.5x/1x/2x.",
         "Clock (top bar): click to change the simulation time; delays depend on the clock.",
         "'C engine - live calls' panel: every request to the C engine and how many microseconds it took."]))

A(("h2", "2.7 The terminal version"))
A(("cmd", [".\\metro --demo"]))
A(("p", "Runs the 9-act story in the terminal; press Enter between acts. For the interactive menu:"))
A(("cmd", [".\\metro"]))
A(("t", ["Menu key", "Feature", "DSA"], [
    ["1", "Network map", "graph rendering"],
    ["2", "Fastest route (+ ride along)", "Dijkstra vs A*, binary heap"],
    ["3", "Alternative routes", "Yen's K-shortest"],
    ["4", "Fewest stops", "BFS"],
    ["5", "Station search", "trie"],
    ["6", "Report incident (close / delay)", "segment tree range update"],
    ["7", "Undo last incident", "stack"],
    ["8", "Delay timeline", "segment tree range max"],
    ["9", "FIRE ESCAPE", "deadline-constrained Dijkstra"],
    ["10", "Evacuate the city", "multi-source Dijkstra"],
    ["11", "Critical stations", "Tarjan"],
    ["12", "Network islands", "union-find"],
    ["13", "Night maintenance plan", "Kruskal"],
    ["14", "Scale benchmark", "heap vs array, A* vs Dijkstra"],
    ["15", "Settings (clock, transfer penalty, animation)", "-"],
    ["d / g / 0", "Guided demo / open GUI / quit", "-"],
]))
A(("t", ["Terminal flag", "Effect"], [
    ["--fast  /  --slow", "Animation speed"],
    ["--noanim", "Static maps, no animation"],
    ["--nocolor", "Plain ASCII (old consoles, printing)"],
    ["--map FILE", "Load another city"],
]))

A(("h2", "2.8 Run the self-tests"))
A(("p", "Proves the algorithms are correct (A* = Dijkstra on every pair, FIFO property, fire-escape safety, Yen ordering, segment tree vs brute force):"))
A(("cmd", ["gcc -std=c99 -O2 -Isrc -o test_routes.exe tests\\test_routes.c src\\graph.c src\\heap.c src\\trie.c src\\segtree.c src\\dsu.c src\\route.c src\\analysis.c src\\render.c src\\anim.c src\\bench.c src\\incidents.c -lm",
           ".\\test_routes"]))
A(("p", "Expected last line: ALL TESTS PASSED."))

A(("h2", "2.9 Recommended demo sequence (about 6 minutes)"))
A(("n", ['Open a terminal in the project folder and run .\\build.bat (do this before the viva).',
         'Run .\\metro --gui and maximise the browser.',
         'Click "Story mode" and walk through the 11 acts with the Next button, explaining each caption (see section 5).',
         'Close story mode and do live tricks for the examiner (section 2.10).',
         'Optionally show the terminal version: Ctrl+C the server, then .\\metro --demo --fast.',
         'Show correctness: run the self-tests (section 2.8).']))
A(("h2", "2.10 Live tricks for the examiner"))
A(("b", ["Fire escape -> 'Surprise me': the C engine searches all (fire, you) pairs for a dramatic scenario.",
         "Click Tower -> 'Close this station', then Evacuate the city: Hill Top is TRAPPED (no exit reachable).",
         "Incidents -> add a delay: the segment-tree X-ray shows exactly which nodes the O(log n) update touched; Fastest route then avoids it.",
         "Station search: type 'gat' (trie finds all Gates) then 'CEN' (hash table, bucket shown).",
         "Lab -> Scale benchmark: binary heap vs O(V^2) array Dijkstra on a 900-station city."]))
A(("h2", "2.11 Troubleshooting"))
A(("t", ["Problem", "Fix"], [
    ["'metro' is not recognized", "In PowerShell type .\\metro (with .\\). Make sure you are in the project folder and have built it."],
    ["'gcc' is not recognized", "Add C:\\MinGW\\bin to PATH, or run build.bat from a terminal where gcc works."],
    ["Could not load the metro map", "Run from the project folder (it needs data\\metro.txt)."],
    ["Can't find web/index.html", "Run metro --gui from the project folder (it needs the web\\ folder)."],
    ["Browser shows 'can't connect'", "The engine window was closed or stopped - run .\\metro --gui again."],
    ["Port already in use", "It moves to the next port automatically; or use --port 9000."],
    ["Strange symbols in terminal", "Use Windows Terminal, or add --nocolor."],
    ["GUI layout cramped", "Maximise the browser; it needs at least 1100 px width."],
]))

# ---------------------------------------------------------------- how it works
A(("pb",))
A(("h1", "3. How it all works"))
A(("h2", "3.1 Architecture"))
A(("p", "The project is one C program with an engine and two front ends:"))
A(("b", ["Engine (C): graph, hash table, trie, heap, segment trees, union-find and all algorithms (src\\*.c).",
         "Terminal front end (C): renders an ASCII map into a 2D character matrix and animates it in place (render.c, anim.c, main.c).",
         "GUI front end: server.c is a tiny HTTP server (Winsock, single-threaded, 127.0.0.1 only). The browser asks it "
         "for JSON - for example GET /api/route?from=AIR&to=BCH - and web\\app.js just draws and animates the answer."]))
A(("p", "Flow of one request: browser click -> GET /api/route -> server.c parses the query -> route.c runs Dijkstra and A* "
        "(with the binary heap) -> server.c turns the Route structs, heap-pop traces and timings into JSON -> the browser animates it."))
A(("h2", "3.2 Files"))
A(("t", ["File", "Responsibility"], [
    ["data\\metro.txt", "The city: LINE, STATION (with map x,y), EDGE (minutes), EXIT. Editable, no recompiling."],
    ["src\\metro.h", "All structs (Metro, Station, Edge, Route, SegTree, ...) and prototypes."],
    ["src\\graph.c", "Adjacency-list graph, djb2 hash table, file loader, A* heuristic scale."],
    ["src\\heap.c", "Binary min-heap: push = sift-up, pop = sift-down."],
    ["src\\trie.c", "Trie insert / prefix collect / path walk."],
    ["src\\segtree.c", "Lazy segment tree: range add, range max."],
    ["src\\dsu.c", "Union-find with path compression and union by rank."],
    ["src\\route.c", "Dijkstra, A*, deadline escape, BFS, Yen; FIFO-safe edge cost; heap recorder."],
    ["src\\analysis.c", "spread() (fire/evacuation), Tarjan, Kruskal + merge sort, islands; DFS & Kruskal recorders."],
    ["src\\incidents.c", "Incident stack (push applies, pop undoes) and the shared clock."],
    ["src\\server.c", "HTTP/JSON API for the GUI, growable string buffer, static file serving."],
    ["src\\render.c, src\\anim.c", "Terminal map renderer and terminal animations."],
    ["src\\bench.c", "Generates a 900-station grid city and times the algorithms."],
    ["src\\main.c", "Command-line flags, terminal menus, boot log, terminal demo."],
    ["web\\index.html, style.css, app.js", "GUI layout, styling, drawing and animation only."],
    ["tests\\test_routes.c", "Correctness tests."],
]))

A(("h2", "3.3 Loading the city"))
A(("p", "metro_load() reads data\\metro.txt line by line. Each STATION is added to the station array and to the hash table "
        "(code -> id). Each EDGE is added to both endpoints' adjacency lists (undirected). After loading, metro_finalize() "
        "inserts every code, full name and every word of each name into the trie, computes the A* heuristic scale, and "
        "creates one segment tree (1440 minutes) per line."))

A(("h2", "3.4 Fastest route: Dijkstra on a (station, line) state graph"))
A(("p", "A plain station graph cannot charge for changing lines, because the cost of the next track depends on which line "
        "you arrived on. So the search runs on states (station, line): 23 stations x (5 lines + 1 'not yet boarded') = 207 states. "
        "Moving along a track of a different line adds the transfer penalty (4 minutes)."))
A(("p", "Dijkstra pops the cheapest state from the binary min-heap, marks it done and relaxes its neighbours. It uses lazy "
        "deletion: instead of decrease-key, a cheaper copy is pushed and stale copies are skipped when popped. The X-ray in "
        "the GUI shows these stale entries greyed out."))
A(("h2", "3.5 Time-dependent delays and the FIFO fix"))
A(("p", "A track's cost is minutes + delay(line, current time), where the delay is a point query on that line's segment tree. "
        "A naive version breaks Dijkstra: leaving one minute later (after a delay window ends) could arrive earlier. "
        "The fix lets a passenger wait out a delay: cost = min over w of (w + delay(t + w)). Arrival time then never "
        "decreases when departure time increases (the FIFO property), which is exactly what keeps Dijkstra optimal. "
        "The tests check this over 47,610 timed queries."))
A(("h2", "3.6 A* search"))
A(("p", "A* orders the heap by f = g + h, where h = straight-line distance x the fastest minutes-per-unit of any track. "
        "No track is faster than that, so h never overestimates (admissible and consistent) and A* returns Dijkstra's exact "
        "answer while exploring far fewer states (68% fewer on Airport -> Beach)."))
A(("h2", "3.7 Fewest stops (BFS) and alternatives (Yen)"))
A(("p", "BFS with an array queue finds the route with the fewest stops - not always the fastest. Yen's algorithm finds the K "
        "best loopless routes: each node of the previous best path becomes a spur node, tracks already used from the same "
        "root are banned, a new shortest path is grown from the spur, and root + spur goes into a min-heap of candidates."))
A(("h2", "3.8 Incidents: stack + lazy segment tree"))
A(("p", "Closing a station sets closed[s]. A delay '+7 min on Green from 08:00 to 10:00' is one range update on Green's "
        "segment tree: fully covered nodes just get a lazy tag (children untouched), so only O(log n) nodes change. "
        "Each incident is pushed on a stack; Undo pops it and applies the exact opposite (reopen / subtract the delay)."))
A(("h2", "3.9 Fire escape: deadline-constrained Dijkstra"))
A(("n", ["Fire spread: Dijkstra seeded at the fire station, with each track taking (minutes x speed%) - gives burn[v], the minute the fire reaches each station.",
         "Ordinary plan (Plan A): Dijkstra to the nearest exit, ignoring the fire.",
         "Metro Escape (Plan B): Dijkstra over (station, line) states to ANY exit, pruning every move that would arrive at a station, or leave it after a transfer, at or after burn[v].",
         "Because of FIFO, arriving earlier is never worse, so this pruned Dijkstra is still exact: if any safe route exists, it finds the fastest one; otherwise it reports the best shelter."]))
A(("h2", "3.10 Evacuation"))
A(("p", "The same spread() function, seeded at all five exits at once (multi-source Dijkstra). Every station gets its nearest "
        "exit and evacuation time; the parent pointers form the escape tree drawn in cyan. Unreachable stations are TRAPPED."))
A(("h2", "3.11 Critical stations: Tarjan's algorithm"))
A(("p", "One DFS assigns disc[u] (visit order) and low[u] (the earliest station reachable from u's subtree using one back edge). "
        "A non-root u is an articulation point if some child v has low[v] >= disc[u]; the root is one if it has two or more DFS "
        "children. A track (u,v) is a bridge if low[v] > disc[u]. The impact of each critical station is measured with union-find "
        "(stations stranded if it closes) and ranked with the heap."))
A(("h2", "3.12 Night maintenance: Kruskal + union-find"))
A(("p", "Tracks are merge-sorted by minutes and taken cheapest-first; union-find rejects a track whose ends are already in "
        "the same set (it would form a cycle). The result is the minimum spanning tree: the cheapest set of tracks that keeps "
        "every station connected; all others can close for repairs (about 40% of track-minutes)."))
A(("h2", "3.13 Search: trie and hash table"))
A(("p", "Typing a prefix walks the trie one node per letter (O(prefix length)) and collects all station ids in that subtree. "
        "An exact 3-letter code is answered by the hash table: djb2(code) mod 101 selects a bucket, and the short chain is scanned."))
A(("h2", "3.14 X-ray mode (execution recorders)"))
A(("p", "To make the data structures visible, the C code records its own execution when the server asks for it:"))
A(("t", ["Recorder (C)", "What it captures", "Shown in the GUI as"], [
    ["rec_heap() in route.c", "The heap array after every pop, with stale flags", "Heap tree + array, step by step"],
    ["ev() in analysis.c", "DFS enter / back edge / return / articulation / bridge", "disc|low badges and DFS replay on the map"],
    ["g_kruRec in analysis.c", "Each edge tried, accepted or rejected, DSU parents", "Kruskal replay, stations coloured by set"],
    ["rec_node() in segtree.c", "Nodes visited by the last range update", "Segment tree viewer with touched nodes"],
    ["trie_walk(), hash_bucket()", "Trie path and hash bucket of the query", "Trie path diagram + 101-bucket grid"],
]))
A(("h2", "3.15 The HTTP API"))
A(("t", ["Endpoint", "Computes (in C)"], [
    ["/api/network", "Stations, lines, tracks, incidents, engine statistics"],
    ["/api/route?from&to", "Dijkstra + A*: paths, pop traces, heap snapshots, microsecond timings"],
    ["/api/alternatives?from&to&k", "Yen's K shortest routes"],
    ["/api/fewest?from&to", "BFS route and fastest route"],
    ["/api/simulate?from&edges", "Re-time a given path under current delays"],
    ["/api/fire?fire&me&pct", "Fire spread, Plan A, Plan B, margin, shelter"],
    ["/api/evacuate", "Evacuation times, nearest exit, escape tree"],
    ["/api/critical", "Articulation points, bridges, impact, DFS events"],
    ["/api/mst", "Kruskal result and replay steps"],
    ["/api/islands", "Union-find components"],
    ["/api/search?q", "Trie matches, trie path, hash bucket"],
    ["/api/hash", "All hash buckets"],
    ["/api/timeline", "Hourly maximum delay per line (segment tree range-max)"],
    ["/api/segtree?line", "Top segment-tree nodes and touched nodes"],
    ["/api/incident, /api/undo, /api/reset, /api/settings", "Change the network state (incident stack, clock)"],
    ["/api/bench", "900-station benchmark"],
]))
A(("h2", "3.16 Testing"))
A(("b", ["Segment tree vs brute force: 2,000 random range updates and queries.",
         "A* equals Dijkstra on all 529 station pairs, under normal, delayed and closed networks.",
         "FIFO: 529 pairs x 90 departure times - leaving later never arrives earlier.",
         "Fire escape: 1,518 scenarios - the escape route never enters a burning station and is never worse than a safe naive plan.",
         "Yen: first route equals the shortest; routes come out in non-decreasing order."]))

# ---------------------------------------------------------------- story script
A(("pb",))
A(("h1", "4. Story mode: what to say in each act"))
A(("t", ["Act", "On screen", "Talking point"], [
    ["1 Meet Nova City", "Map, engine boot log", "Graph from a text file; hash table and trie built at load time."],
    ["2 Watch the algorithms think", "Dijkstra ripple, then A* beam; heap X-ray", "Replayed from C's real heap-pop order; A* does ~68% less work for the same answer; stale heap entries = lazy deletion."],
    ["3 Ride along", "Train moving with a clock", "The route has arrival times per station; transfers add a walk."],
    ["4 Rush hour", "Segment tree X-ray, heatmap", "One O(log n) range update; lazy tags on fully covered nodes."],
    ["5 The planner adapts", "New route avoiding Green", "Edge costs are live segment-tree queries; FIFO waiting keeps Dijkstra exact."],
    ["6 Flood at Docklands", "Three alternative routes", "Yen's algorithm: spur paths + candidate heap."],
    ["7 What could break the city?", "Tarjan DFS replay with disc|low", "low[v] >= disc[u] means u is critical; impact via union-find."],
    ["8 Night maintenance", "Kruskal replay, colour-merging sets", "Merge sort + union-find; cycles rejected."],
    ["9 FIRE", "Fire spreading, two trains", "Plan A gets caught; Plan B = deadline-constrained Dijkstra escapes with a safety margin."],
    ["10 Evacuate everyone", "Rescue wave, escape tree", "Multi-source Dijkstra - the same code as the fire."],
    ["11 Does it scale?", "Benchmark bars", "900 stations: binary heap ~10x faster than O(V^2) array scan; identical answers."],
]))

# ---------------------------------------------------------------- viva
A(("h1", "5. Likely viva questions"))
for q, a in [
    ("Why a (station, line) state graph instead of a station graph?", "The cost of a track depends on the line you arrived on (transfer penalty). Making the line part of the state turns that into an ordinary Dijkstra."),
    ("Why is A* guaranteed to give the same answer as Dijkstra?", "Its heuristic (straight-line distance x the fastest minutes-per-unit of any track) never overestimates and is consistent, so the first time the target is popped its cost is optimal."),
    ("What is lazy deletion in the heap?", "Instead of decrease-key we push a new cheaper copy and skip outdated copies when they are popped. It keeps the heap simple and still O(E log V)."),
    ("Why did delays need a special fix?", "With time windows, leaving later could arrive earlier (non-FIFO), which breaks Dijkstra. Allowing waiting makes arrival non-decreasing in departure time, restoring optimality."),
    ("Why a lazy segment tree for delays?", "Delays are added to whole time ranges and queried at single minutes or ranges. Lazy propagation makes both O(log n) instead of O(range length)."),
    ("How does the fire escape stay optimal?", "Fire times come from Dijkstra. The escape search only drops moves that arrive at or after the fire; with FIFO, earliest arrival dominates, so pruning never removes the best safe route."),
    ("Why union-find in Kruskal?", "To test in near-constant time whether a track's two ends are already connected, i.e. whether adding it would form a cycle."),
    ("Is the JavaScript doing the algorithms?", "No. app.js only draws. Every result, trace and timing comes from the C engine over /api/...; the 'C engine - live calls' panel shows each call."),
]:
    A(("h2", q))
    A(("p", a))

# ================================================================= DOCX
from docx import Document
from docx.shared import Pt, RGBColor, Cm, Inches
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml.ns import qn
from docx.oxml import OxmlElement

ACC = RGBColor(0x1F, 0x5F, 0xA8)
doc = Document()
sec = doc.sections[0]
sec.page_width, sec.page_height = Cm(21), Cm(29.7)
for side in ("left_margin", "right_margin"): setattr(sec, side, Cm(2.2))
sec.top_margin = sec.bottom_margin = Cm(2)
st = doc.styles["Normal"]; st.font.name = "Calibri"; st.font.size = Pt(10.5)
for name, size in (("Heading 1", 18), ("Heading 2", 13)):
    s = doc.styles[name]; s.font.name = "Calibri"; s.font.size = Pt(size); s.font.color.rgb = ACC; s.font.bold = True

def shade(el, hexcolor):
    pPr = el.get_or_add_pPr() if hasattr(el, "get_or_add_pPr") else el
    shd = OxmlElement("w:shd"); shd.set(qn("w:val"), "clear"); shd.set(qn("w:color"), "auto"); shd.set(qn("w:fill"), hexcolor)
    pPr.append(shd)

def cell_shade(cell, hexcolor):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd"); shd.set(qn("w:val"), "clear"); shd.set(qn("w:color"), "auto"); shd.set(qn("w:fill"), hexcolor)
    tcPr.append(shd)

# page numbers in footer
fp = sec.footer.paragraphs[0]; fp.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = fp.add_run("Metro Escape - DSA Lab Project   |   page ")
for t, txt in (("begin", None), (None, "PAGE"), ("end", None)):
    if t:
        fc = OxmlElement("w:fldChar"); fc.set(qn("w:fldCharType"), t); rr = fp.add_run(); rr._r.append(fc)
    else:
        it = OxmlElement("w:instrText"); it.set(qn("xml:space"), "preserve"); it.text = txt; rr = fp.add_run(); rr._r.append(it)

for blk in C:
    k = blk[0]
    if k == "title":
        p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER; p.paragraph_format.space_before = Pt(60)
        r = p.add_run(blk[1]); r.font.size = Pt(34); r.bold = True; r.font.color.rgb = ACC
    elif k == "sub":
        p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = p.add_run(blk[1]); r.font.size = Pt(13); r.font.color.rgb = RGBColor(0x55, 0x60, 0x70)
    elif k == "h1": doc.add_heading(blk[1], level=1)
    elif k == "h2": doc.add_heading(blk[1], level=2)
    elif k == "p": doc.add_paragraph(blk[1])
    elif k == "b":
        for it in blk[1]: doc.add_paragraph(it, style="List Bullet")
    elif k == "n":
        for it in blk[1]: doc.add_paragraph(it, style="List Number")
    elif k == "cmd":
        for line in blk[1]:
            p = doc.add_paragraph(); p.paragraph_format.left_indent = Cm(0.4)
            p.paragraph_format.space_after = Pt(2)
            r = p.add_run(line); r.font.name = "Consolas"; r.font.size = Pt(9.5); r._element.rPr.rFonts.set(qn("w:eastAsia"), "Consolas")
            shade(p._p, "EEF2F7")
        doc.add_paragraph().paragraph_format.space_after = Pt(0)
    elif k == "note":
        p = doc.add_paragraph(); shade(p._p, "FFF6DD"); r = p.add_run("Note: "); r.bold = True; p.add_run(blk[1])
    elif k == "t":
        hdr, rows = blk[1], blk[2]
        tb = doc.add_table(rows=1, cols=len(hdr)); tb.style = "Table Grid"; tb.alignment = WD_TABLE_ALIGNMENT.CENTER
        for i, h in enumerate(hdr):
            c = tb.rows[0].cells[i]; c.text = ""; rr = c.paragraphs[0].add_run(h); rr.bold = True
            rr.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF); cell_shade(c, "1F5FA8")
        for ri, row in enumerate(rows):
            cells = tb.add_row().cells
            for i, v in enumerate(row):
                cells[i].text = v
                for pp in cells[i].paragraphs:
                    for rr in pp.runs:
                        rr.font.size = Pt(9.5)
                        if v.startswith((".\\", "http", "/api", "src\\", "data\\", "web\\", "tests\\", "--", "rec_", "ev(", "g_kru", "trie_")): rr.font.name = "Consolas"
                if ri % 2: cell_shade(cells[i], "F3F6FA")
        doc.add_paragraph()
    elif k == "img":
        if os.path.exists(blk[1]):
            doc.add_picture(blk[1], width=Cm(16.5)); doc.paragraphs[-1].alignment = WD_ALIGN_PARAGRAPH.CENTER
            p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
            r = p.add_run(blk[2]); r.italic = True; r.font.size = Pt(9)
    elif k == "pb": doc.add_page_break()
docx_path = os.path.join(OUT, "Metro_Escape_Report.docx")
doc.save(docx_path)

# ================================================================= ODT
from odf.opendocument import OpenDocumentText
from odf.style import (Style, TextProperties, ParagraphProperties, TableColumnProperties,
                       TableCellProperties, GraphicProperties, TableProperties, ListLevelProperties)
from odf.text import P, H, List, ListItem, ListStyle, ListLevelStyleBullet, ListLevelStyleNumber, Span
from odf.table import Table, TableColumn, TableRow, TableCell
from odf.draw import Frame, Image as DImage

od = OpenDocumentText()
def ps(name, **kw):
    s = Style(name=name, family="paragraph")
    tp = {k: v for k, v in kw.items() if k in ("fontsize", "fontweight", "color", "fontfamily", "fontstyle")}
    pp = {k: v for k, v in kw.items() if k in ("textalign", "margintop", "marginbottom", "marginleft", "backgroundcolor", "padding")}
    if tp: s.addElement(TextProperties(**tp))
    if pp: s.addElement(ParagraphProperties(**pp))
    od.styles.addElement(s); return s
sN = ps("Body", fontsize="10.5pt", fontfamily="Calibri", marginbottom="0.15cm")
sT = ps("TitleX", fontsize="34pt", fontweight="bold", color="#1F5FA8", textalign="center", margintop="3cm", fontfamily="Calibri")
sS = ps("SubX", fontsize="13pt", color="#556070", textalign="center", fontfamily="Calibri")
sH1 = ps("H1X", fontsize="18pt", fontweight="bold", color="#1F5FA8", margintop="0.5cm", marginbottom="0.25cm", fontfamily="Calibri")
sH2 = ps("H2X", fontsize="13pt", fontweight="bold", color="#1F5FA8", margintop="0.35cm", marginbottom="0.15cm", fontfamily="Calibri")
sC = ps("CodeX", fontsize="9.5pt", fontfamily="Consolas", backgroundcolor="#EEF2F7", marginleft="0.4cm", padding="0.08cm", marginbottom="0.05cm")
sNote = ps("NoteX", fontsize="10.5pt", backgroundcolor="#FFF6DD", padding="0.12cm", fontfamily="Calibri")
sCap = ps("CapX", fontsize="9pt", fontstyle="italic", textalign="center")
sCell = ps("CellX", fontsize="9.5pt", fontfamily="Calibri")
sCellH = ps("CellHX", fontsize="9.5pt", fontweight="bold", color="#FFFFFF", fontfamily="Calibri")
sBreak = Style(name="BreakX", family="paragraph"); sBreak.addElement(ParagraphProperties(breakbefore="page")); od.automaticstyles.addElement(sBreak)
hdrCell = Style(name="HdrCell", family="table-cell"); hdrCell.addElement(TableCellProperties(backgroundcolor="#1F5FA8", border="0.5pt solid #9AA8BC", padding="0.08cm")); od.automaticstyles.addElement(hdrCell)
cellA = Style(name="CellA", family="table-cell"); cellA.addElement(TableCellProperties(border="0.5pt solid #9AA8BC", padding="0.08cm")); od.automaticstyles.addElement(cellA)
cellB = Style(name="CellB", family="table-cell"); cellB.addElement(TableCellProperties(backgroundcolor="#F3F6FA", border="0.5pt solid #9AA8BC", padding="0.08cm")); od.automaticstyles.addElement(cellB)
tStyle = Style(name="TblX", family="table"); tStyle.addElement(TableProperties(width="16.6cm", align="center")); od.automaticstyles.addElement(tStyle)

bl = ListStyle(name="BulX"); b1 = ListLevelStyleBullet(level=1, bulletchar="•"); b1.addElement(ListLevelProperties(spacebefore="0.3cm", minlabelwidth="0.5cm")); bl.addElement(b1); od.automaticstyles.addElement(bl)
nl = ListStyle(name="NumX"); n1 = ListLevelStyleNumber(level=1, numformat="1", numsuffix="."); n1.addElement(ListLevelProperties(spacebefore="0.3cm", minlabelwidth="0.6cm")); nl.addElement(n1); od.automaticstyles.addElement(nl)

pending_break = False
def add(el):
    od.text.addElement(el)
def para(style, text):
    global pending_break
    p = P(stylename=style, text=text)
    if pending_break:
        # wrap: put an empty break paragraph before
        od.text.addElement(P(stylename=sBreak, text="")); pending_break = False
    return p
for blk in C:
    k = blk[0]
    if k == "title": add(para(sT, blk[1]))
    elif k == "sub": add(para(sS, blk[1]))
    elif k == "h1": add(para(sH1, blk[1]))
    elif k == "h2": add(para(sH2, blk[1]))
    elif k == "p": add(para(sN, blk[1]))
    elif k in ("b", "n"):
        if pending_break: od.text.addElement(P(stylename=sBreak, text="")); pending_break = False
        L = List(stylename=bl if k == "b" else nl)
        for it in blk[1]:
            li = ListItem(); li.addElement(P(stylename=sN, text=it)); L.addElement(li)
        add(L)
    elif k == "cmd":
        for line in blk[1]: add(para(sC, line))
        add(P(stylename=sN, text=""))
    elif k == "note":
        p = para(sNote, ""); p.addText("Note: " + blk[1]); add(p)
    elif k == "t":
        hdr, rows = blk[1], blk[2]
        tb = Table(name="T%d" % id(blk), stylename=tStyle)
        tb.addElement(TableColumn(numbercolumnsrepeated=len(hdr)))
        tr = TableRow()
        for h in hdr:
            tc = TableCell(stylename=hdrCell); tc.addElement(P(stylename=sCellH, text=h)); tr.addElement(tc)
        tb.addElement(tr)
        for ri, row in enumerate(rows):
            tr = TableRow()
            for v in row:
                tc = TableCell(stylename=cellB if ri % 2 else cellA); tc.addElement(P(stylename=sCell, text=v)); tr.addElement(tc)
            tb.addElement(tr)
        add(tb); add(P(stylename=sN, text=""))
    elif k == "img":
        if os.path.exists(blk[1]):
            href = od.addPicture(blk[1])
            p = para(sCap, "")
            fr = Frame(width="16.5cm", height="9.86cm", anchortype="as-char")
            fr.addElement(DImage(href=href)); p.addElement(fr); add(p)
            add(P(stylename=sCap, text=blk[2]))
    elif k == "pb": pending_break = True
odt_path = os.path.join(OUT, "Metro_Escape_Report.odt")
od.save(odt_path)
print(docx_path, os.path.getsize(docx_path))
print(odt_path, os.path.getsize(odt_path))
