/* egsolve.c - solve given trees K times with different seeds (for studying
   how hard individual trees are).

   usage: egsolve [--mode eg|am] [--repeat K] < trees
   Input: one tree per line, the 0-based parents of vertices 1..n-1 (vertex 0
   is the root; parents must precede children).  Output per tree:
   n, min / median / max work over the K runs, and the parents.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "egsearch.h"

static int
cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

int
main(int argc, char **argv)
{
    int mode = MODE_EG, repeat = 20, i, n, k, par[EG_MAXN], lab[EG_MAXN];
    char line[1024], *p, *q;
    uint64_t works[1000];
    eg_limits lim;
    eg_stats st;

    for (i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = !strcmp(argv[++i], "am") ? MODE_AM : MODE_EG;
        else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = atoi(argv[++i]);
    }
    if (repeat < 1 || repeat > 1000) { fprintf(stderr, "repeat must be 1..1000\n"); return 2; }
    eg_default_limits(&lim, mode);

    while (fgets(line, sizeof(line), stdin))
    {
        n = 1;
        par[0] = -1;
        for (p = line; ; p = q)
        {
            long x = strtol(p, &q, 10);
            if (q == p) break;
            if (n >= EG_MAXN || x < 0 || x >= n) { fprintf(stderr, "bad tree line\n"); return 2; }
            par[n++] = (int)x;
        }
        if (n < 2) continue;
        for (k = 0; k < repeat; ++k)
        {
            if (eg_solve(mode, n, par, lab, 0x9E3779B97F4A7C15ULL * (uint64_t)(k + 1) ^ (uint64_t)n,
                         &lim, &st) != RES_SOLVED || !eg_check(mode, n, par, lab))
            {
                fprintf(stderr, "tree not solved\n");
                return 1;
            }
            works[k] = st.work;
        }
        qsort(works, (size_t)repeat, sizeof(works[0]), cmp_u64);
        printf("%d %llu %llu %llu :", n, (unsigned long long)works[0],
               (unsigned long long)works[repeat / 2], (unsigned long long)works[repeat - 1]);
        for (i = 1; i < n; ++i) printf(" %d", par[i]);
        printf("\n");
    }
    return 0;
}
