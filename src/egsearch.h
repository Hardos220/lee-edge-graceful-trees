/* egsearch.h - search for edge-graceful / antimagic labelings of trees.

   A tree on vertices 0..n-1 is given by par[v] (v = 1..n-1) with par[v] < v;
   vertex 0 is the root.  Edge v is {v, par[v]}.  A labeling assigns the
   labels 1..n-1 bijectively to the edges; the vertex sum s(v) is the sum of
   the labels of the edges at v.

     MODE_EG : s(v) mod n pairwise distinct      (edge-graceful, Lo 1985)
     MODE_AM : s(v) pairwise distinct integers   (antimagic, Hartsfield-Ringel)

   Edge-graceful implies antimagic.  A tree can be edge-graceful only if n is odd.
*/
#ifndef EGSEARCH_H
#define EGSEARCH_H

#include <stdint.h>

#define EG_MAXN 32              /* labels fit into 5 bits */

enum { MODE_EG = 1, MODE_AM = 2 };
enum { RES_UNSOLVED = 0, RES_SOLVED = 1, RES_IMPOSSIBLE = 2 };

typedef struct {
    int      s1_restarts;       /* stage 1: randomized DFS, Luby restarts */
    uint64_t s1_base;           /*   node budget unit of the Luby sequence */
    int      s2_tries;          /* stage 2: local search runs (0 = skip) */
    uint64_t s2_iters;          /*   iterations per run */
    int      s2_greedy;         /*   EG: start from a greedy dive (else random) */
    int      s2_noise;          /*   random-walk probability, percent */
    int      s2_move;           /*   EG: 0 best swap, 1 first improving, 2 targeted */
    uint64_t s3_nodes;          /* stage 3: complete DFS node budget (0 = unlimited) */
} eg_limits;

typedef struct {
    int      result;            /* RES_* */
    int      stage;             /* stage that decided the tree (1..3), 0 if undecided */
    uint64_t work;              /* DFS nodes + local-search iterations */
    uint32_t restarts;
} eg_stats;

void eg_default_limits(eg_limits *lim, int mode);

/* On RES_SOLVED, lab[v] for v = 1..n-1 is the label of edge {v, par[v]}.
   RES_IMPOSSIBLE means an exhaustive search proved that no labeling exists. */
int eg_solve(int mode, int n, const int *par, int *lab, uint64_t seed,
             const eg_limits *lim, eg_stats *st);

/* As eg_solve; with root_zero (MODE_EG only) the root, vertex 0, must get
   residue 0, i.e. all other vertex sums avoid 0.  To ask for residue 0 at an
   arbitrary vertex, re-root the tree there first. */
int eg_solve_ex(int mode, int n, const int *par, int *lab, uint64_t seed,
                const eg_limits *lim, eg_stats *st, int root_zero);

/* Plain re-check of a labeling (1 = valid). */
int eg_check(int mode, int n, const int *par, const int *lab);

#endif
