/*
 * TRIE (prefix tree) for station autocomplete.
 * Every station is inserted under its code, its full name and every
 * word of its name, so "gat" finds North Gate, East Gate, ...
 *   insert  : O(L)
 *   lookup  : O(P + size of matching subtree)
 */
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "metro.h"

static int char_index(char c)
{
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    if (c == ' ')             return 36;
    return -1;                               /* ignore other chars */
}

TrieNode *trie_new(void)
{
    return (TrieNode *)calloc(1, sizeof(TrieNode));
}

void trie_insert(TrieNode *root, const char *word, int id)
{
    TrieNode *cur = root;
    int k;
    for (; *word; word++) {
        int c = char_index(*word);
        if (c < 0) continue;
        if (!cur->child[c]) cur->child[c] = trie_new();
        cur = cur->child[c];
    }
    for (k = 0; k < cur->nids; k++)
        if (cur->ids[k] == id) return;       /* already stored */
    if (cur->nids < TRIE_IDS) cur->ids[cur->nids++] = id;
}

/* DFS the subtree, gathering unique station ids */
static void collect(TrieNode *t, int out[], int *n, int max, char seen[])
{
    int k;
    if (!t || *n >= max) return;                 /* stop once we have enough */
    for (k = 0; k < t->nids; k++) {
        int id = t->ids[k];
        if (!seen[id] && *n < max) { seen[id] = 1; out[(*n)++] = id; }
    }
    for (k = 0; k < TRIE_ALPHA; k++) collect(t->child[k], out, n, max, seen);
}

int trie_collect(TrieNode *root, const char *prefix, int out[], int max)
{
    static char seen[MAX_STATIONS];
    TrieNode *cur = root;
    int n = 0, i, j;
    memset(seen, 0, sizeof seen);
    for (; *prefix && cur; prefix++) {
        int c = char_index(*prefix);
        if (c < 0) continue;
        cur = cur->child[c];
    }
    collect(cur, out, &n, max, seen);
    for (i = 1; i < n; i++) {                /* insertion sort by id */
        int v = out[i];
        for (j = i - 1; j >= 0 && out[j] > v; j--) out[j + 1] = out[j];
        out[j + 1] = v;
    }
    return n;
}

char trie_char(int idx)
{
    return idx < 26 ? (char)('a' + idx) : idx < 36 ? (char)('0' + idx - 26) : ' ';
}

/* nodes visited while reading the prefix: path[0] = root; returns count (0 if prefix absent) */
int trie_walk(TrieNode *root, const char *prefix, TrieNode **path, char *chars, int max)
{
    int n = 0;
    TrieNode *cur = root;
    path[n] = cur; chars[n] = 0; n++;
    for (; *prefix && n < max; prefix++) {
        int c = char_index(*prefix);
        if (c < 0) continue;
        cur = cur->child[c];
        if (!cur) return 0;
        path[n] = cur; chars[n] = trie_char(c); n++;
    }
    return n;
}

int trie_count(const TrieNode *t)
{
    int k, n = 1;
    if (!t) return 0;
    for (k = 0; k < TRIE_ALPHA; k++) n += trie_count(t->child[k]);
    return n;
}

void trie_free(TrieNode *t)
{
    int k;
    if (!t) return;
    for (k = 0; k < TRIE_ALPHA; k++) trie_free(t->child[k]);
    free(t);
}
