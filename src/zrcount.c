/* zrcount: for every tree read from `gentreeg -p` on stdin, count the
   edge-graceful labelings that have each vertex as zero vertex, by
   enumerating all (n-1)! labelings (Heap's algorithm; vertex sums and the
   residue counts are updated incrementally after each swap).

   usage: gentreeg -p -q N | zrcount N          (odd N, N <= 15)
   output, one line per tree: the gentreeg line, " :", then for every vertex
   (in gentreeg order) the number of edge-graceful labelings with that zero
   vertex. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXN 16

static int n, m, eu[MAXN], ev[MAXN], lab[MAXN], s[MAXN], cnt[MAXN], dup;
static uint64_t zero[MAXN];

static void
move(int v, int d)
{
    int r = s[v];
    if (cnt[r]-- >= 2) dup--;
    r += d;
    if (r >= n) r -= n;
    else if (r < 0) r += n;
    s[v] = r;
    if (cnt[r]++ >= 1) dup++;
}

static void
swap_labels(int a, int b)
{
    int d = lab[b] - lab[a], t;
    move(eu[a], d);
    move(ev[a], d);
    move(eu[b], -d);
    move(ev[b], -d);
    t = lab[a]; lab[a] = lab[b]; lab[b] = t;
}

static void
record(void)
{
    int v;
    if (dup) return;
    for (v = 0; v < n; ++v)
        if (s[v] == 0) { zero[v]++; return; }
}

static void
count_tree(void)
{
    int c[MAXN] = {0}, e, v, i;

    memset(s, 0, sizeof s);
    memset(cnt, 0, sizeof cnt);
    memset(zero, 0, sizeof zero);
    for (e = 0; e < m; ++e)
    {
        lab[e] = e + 1;
        s[eu[e]] += e + 1;
        s[ev[e]] += e + 1;
    }
    dup = 0;
    for (v = 0; v < n; ++v)
    {
        s[v] %= n;
        if (cnt[s[v]]++ >= 1) dup++;
    }
    record();
    for (i = 1; i < m;)                       /* Heap's algorithm */
    {
        if (c[i] < i)
        {
            swap_labels(i % 2 == 0 ? 0 : c[i], i);
            record();
            c[i]++;
            i = 1;
        }
        else
            c[i++] = 0;
    }
}

int
main(int argc, char **argv)
{
    char line[512], copy[512], *tok;
    int par[MAXN], k, v;

    if (argc != 2 || (n = atoi(argv[1])) < 3 || n > 15 || n % 2 == 0)
    {
        fprintf(stderr, "usage: gentreeg -p -q N | zrcount N   (odd 3 <= N <= 15)\n");
        return 2;
    }
    m = n - 1;
    while (fgets(line, sizeof line, stdin))
    {
        line[strcspn(line, "\r\n")] = 0;
        strcpy(copy, line);
        for (k = 0, tok = strtok(copy, " "); tok && k < MAXN; tok = strtok(NULL, " "))
            par[k++] = atoi(tok);
        if (k == 0) continue;
        if (k != n)
        {
            fprintf(stderr, "zrcount: expected %d parents, got %d\n", n, k);
            return 1;
        }
        for (v = 1; v < n; ++v)               /* edge v-1 joins vertex v and its parent */
        {
            eu[v - 1] = v;
            ev[v - 1] = par[v] - 1;
        }
        count_tree();
        printf("%s :", line);
        for (v = 0; v < n; ++v) printf(" %llu", (unsigned long long)zero[v]);
        printf("\n");
    }
    return 0;
}
