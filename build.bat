@echo off
REM Build Metro Escape with MinGW gcc
gcc -std=c99 -O2 -Wall -Wextra -o metro.exe src\main.c src\graph.c src\heap.c src\trie.c src\segtree.c src\dsu.c src\route.c src\analysis.c src\render.c src\anim.c src\bench.c src\incidents.c src\server.c -lm -lws2_32
if %errorlevel%==0 echo Built metro.exe  -  run:  metro --gui   (browser)   or   metro --demo   (terminal)
