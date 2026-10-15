/* egsearch.c - see egsearch.h.

   Stage 1/3: depth-first search that labels the edge above a vertex only when
   all edges below it are labeled, so every assignment fixes one vertex sum.
   The candidate labels of a vertex are computed with bit operations:
     EG: labels l with (P + l) mod n in the set R of unused residues
         = cyclic rotation of R by P, intersected with the unused labels U;
     AM: labels l with P + l not among the used sums (bit window at P).
   The root sum is fixed last.  For EG and odd n it is automatically the one
   missing residue (sum of all vertex sums = n(n-1) = 0 = sum of residues mod n).
   Variable choice: fewest candidates first (MRV).  Symmetry breaking: leaves
   with a common parent get increasing labels (swapping two sibling leaves'
   labels maps solutions to solutions).  The DFS is complete, so a top-level
   failure without budget exhaustion proves that no labeling exists.

   Stage 2: min-conflicts local search over label swaps.
*/
#include <string.h>
#include <limits.h>
#include "egsearch.h"

#define POPC(x) __builtin_popcount(x)
#define CTZ(x)  __builtin_ctz(x)

enum { FAIL = 0, OK = 1, ABORT = 2 };

static inline uint64_t
splitmix(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline uint32_t
below(uint64_t *s, uint32_t k)          /* uniform in 0..k-1 */
{
    return (uint32_t)(((splitmix(s) >> 32) * (uint64_t)k) >> 32);
}

static inline int
nth_bit(uint32_t x, int k)
{
    while (k-- > 0) x &= x - 1;
    return CTZ(x);
}

static uint64_t
luby(uint32_t i)                        /* 1,1,2,1,1,2,4,1,1,2,... (i >= 1) */
{
    uint32_t k;
    for (;;)
    {
        for (k = 1; ((1u << k) - 1) < i; ++k) {}
        if (i == (1u << k) - 1) return 1ull << (k - 1);
        i = i - (1u << (k - 1)) + 1;
    }
}

/**************************************************************************/

typedef struct {
    int mode, n;
    int root_zero;              /* EG: the root must get residue 0 (others avoid 0) */
    int par[EG_MAXN];
    int nch[EG_MAXN];
    int prevleaf[EG_MAXN], nextleaf[EG_MAXN];   /* chains of sibling leaves */
    /* search state */
    int P[EG_MAXN];             /* sum of labels below v (mod n for EG) */
    int remc[EG_MAXN];          /* unlabeled child edges of v */
    int lab[EG_MAXN];
    uint32_t U;                 /* unused labels, bits 1..n-1 */
    uint32_t R;                 /* EG: unused residues, bits 0..n-1 */
    uint32_t ready;             /* vertices whose edge may be labeled now */
    uint64_t used[10];          /* AM: used vertex sums (max sum 496 < 576) */
    uint64_t nodes, limit;
    uint64_t rng;
} ctx_t;

static void
ctx_setup(ctx_t *c, int mode, int n, const int *par)
{
    int v, p, last[EG_MAXN];

    c->mode = mode;
    c->n = n;
    c->par[0] = -1;
    for (v = 0; v < n; ++v) { c->nch[v] = 0; last[v] = -1; }
    for (v = 1; v < n; ++v) { c->par[v] = par[v]; ++c->nch[par[v]]; }
    for (v = 1; v < n; ++v)
    {
        c->prevleaf[v] = c->nextleaf[v] = -1;
        if (c->nch[v] == 0)
        {
            p = c->par[v];
            c->prevleaf[v] = last[p];
            if (last[p] >= 0) c->nextleaf[last[p]] = v;
            last[p] = v;
        }
    }
}

static void
ctx_reset(ctx_t *c)
{
    int v, n = c->n;

    c->U = (n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1) & ~1u;
    c->R = (n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1);
    if (c->root_zero) c->R &= ~1u;
    memset(c->used, 0, sizeof(c->used));
    c->ready = 0;
    for (v = 0; v < n; ++v)
    {
        c->P[v] = 0;
        c->remc[v] = c->nch[v];
        c->lab[v] = 0;
        if (v > 0 && c->nch[v] == 0 && c->prevleaf[v] < 0) c->ready |= 1u << v;
    }
}

static inline uint32_t
cand_of(const ctx_t *c, int v)
{
    uint32_t m;
    int p = c->P[v], pl;

    if (c->mode == MODE_EG)
    {
        uint64_t R = c->R;
        m = (uint32_t)((R >> p) | (R << (c->n - p))) & c->U;
    }
    else
    {
        int w = p >> 6, b = p & 63;
        uint64_t x = c->used[w] >> b;
        if (b) x |= c->used[w + 1] << (64 - b);
        m = c->U & ~(uint32_t)x;
    }
    pl = c->prevleaf[v];
    if (pl >= 0) m &= ~((2u << c->lab[pl]) - 1);     /* labels > lab[pl] */
    return m;
}

static int
dfs(ctx_t *c, int depth)
{
    uint32_t rd, bc, m;
    int v, k, best, bk, p, l, s, key, nl, oldp, np, rp, r;

    if (depth == c->n - 1)
    {
        p = c->P[0];
        if (c->mode == MODE_EG)
        {
            if (c->root_zero) return (c->R == 0 && p == 0) ? OK : FAIL;
            return c->R == (1u << p) ? OK : FAIL;
        }
        return (c->used[p >> 6] >> (p & 63)) & 1 ? FAIL : OK;
    }

    best = -1; bk = 99; bc = 0;
    for (rd = c->ready; rd; rd &= rd - 1)
    {
        v = CTZ(rd);
        m = cand_of(c, v);
        k = POPC(m);
        if (k < bk)
        {
            if (k == 0) return FAIL;
            bk = k; best = v; bc = m;
        }
    }

    v = best;
    p = c->par[v];
    while (bc)
    {
        if (++c->nodes > c->limit) return ABORT;
        l = nth_bit(bc, (int)below(&c->rng, (uint32_t)bk));
        bc &= ~(1u << l);
        --bk;

        s = c->P[v] + l;
        if (c->mode == MODE_EG)
        {
            key = s >= c->n ? s - c->n : s;
            c->R ^= 1u << key;
        }
        else
        {
            key = s;
            c->used[s >> 6] |= 1ull << (s & 63);
        }
        c->U ^= 1u << l;
        c->lab[v] = l;
        c->ready &= ~(1u << v);
        nl = c->nextleaf[v];
        if (nl >= 0) c->ready |= 1u << nl;
        oldp = c->P[p];
        np = oldp + l;
        if (c->mode == MODE_EG && np >= c->n) np -= c->n;
        c->P[p] = np;
        rp = (--c->remc[p] == 0 && p != 0);
        if (rp) c->ready |= 1u << p;

        r = dfs(c, depth + 1);
        if (r == OK) return OK;

        if (rp) c->ready &= ~(1u << p);
        ++c->remc[p];
        c->P[p] = oldp;
        if (nl >= 0) c->ready &= ~(1u << nl);
        c->ready |= 1u << v;
        c->U ^= 1u << l;
        if (c->mode == MODE_EG) c->R ^= 1u << key;
        else                    c->used[key >> 6] &= ~(1ull << (key & 63));
        if (r == ABORT) return ABORT;
    }
    return FAIL;
}

/**************************************************************************/
/* Stage 2: min-conflicts local search on label swaps. */

typedef struct {
    int mode, n;
    const int *par;
    int lab[EG_MAXN];
    int S[EG_MAXN];             /* integer vertex sums */
    int cnt[600];               /* multiplicity of each key */
    int cost;                   /* sum over keys of max(cnt-1, 0) */
    int inc[EG_MAXN][EG_MAXN], deg[EG_MAXN];
} ls_t;

static inline int
key_of(const ls_t *L, int s)
{
    return L->mode == MODE_EG ? s % L->n : s;
}

/* Cost change of swapping the labels of edges e and f; applied if apply. */
static int
swap_delta(ls_t *L, int e, int f, int apply)
{
    int vs[4], ch[4], m = 0, i, j, d, delta = 0, k;
    int x[4];

    d = L->lab[f] - L->lab[e];
    x[0] = e; x[1] = L->par[e]; x[2] = f; x[3] = L->par[f];
    for (j = 0; j < 4; ++j)
    {
        for (i = 0; i < m && vs[i] != x[j]; ++i) {}
        if (i == m) { vs[m] = x[j]; ch[m] = 0; ++m; }
        ch[i] += (j < 2 ? d : -d);
    }
    for (i = 0; i < m; ++i)
        if (ch[i])
        {
            k = key_of(L, L->S[vs[i]]);
            if (L->cnt[k] > 1) --delta;
            --L->cnt[k];
        }
    for (i = 0; i < m; ++i)
        if (ch[i])
        {
            k = key_of(L, L->S[vs[i]] + ch[i]);
            if (L->cnt[k] >= 1) ++delta;
            ++L->cnt[k];
        }
    if (apply)
    {
        for (i = 0; i < m; ++i) L->S[vs[i]] += ch[i];
        k = L->lab[e]; L->lab[e] = L->lab[f]; L->lab[f] = k;
        L->cost += delta;
    }
    else
    {
        for (i = 0; i < m; ++i)
            if (ch[i]) --L->cnt[key_of(L, L->S[vs[i]] + ch[i])];
        for (i = 0; i < m; ++i)
            if (ch[i]) ++L->cnt[key_of(L, L->S[vs[i]])];
    }
    return delta;
}

static int
local_search(int mode, int n, const int *par, int *lab, uint64_t *rng,
             uint64_t maxit, uint64_t *work)
{
    ls_t L;
    int v, e, f, k, t, nc, d, bestd, bestf, ties, conf[EG_MAXN];
    uint64_t it;

    if (n < 3) return 0;
    L.mode = mode; L.n = n; L.par = par;
    for (v = 0; v < n; ++v) { L.deg[v] = 0; L.S[v] = 0; }
    for (e = 1; e < n; ++e)
    {
        L.inc[e][L.deg[e]++] = e;
        L.inc[par[e]][L.deg[par[e]]++] = e;
        L.lab[e] = e;
    }
    for (e = n - 1; e > 1; --e)                     /* random permutation */
    {
        f = 1 + (int)below(rng, (uint32_t)e);
        t = L.lab[e]; L.lab[e] = L.lab[f]; L.lab[f] = t;
    }
    for (e = 1; e < n; ++e) { L.S[e] += L.lab[e]; L.S[par[e]] += L.lab[e]; }
    memset(L.cnt, 0, sizeof(L.cnt));
    L.cost = 0;
    for (v = 0; v < n; ++v)
    {
        k = key_of(&L, L.S[v]);
        if (L.cnt[k] >= 1) ++L.cost;
        ++L.cnt[k];
    }

    for (it = 0; it < maxit; ++it)
    {
        if (L.cost == 0)
        {
            for (e = 1; e < n; ++e) lab[e] = L.lab[e];
            *work += it;
            return 1;
        }
        nc = 0;
        for (v = 0; v < n; ++v)
            if (L.cnt[key_of(&L, L.S[v])] > 1) conf[nc++] = v;
        v = conf[below(rng, (uint32_t)nc)];
        e = L.inc[v][below(rng, (uint32_t)L.deg[v])];

        if (below(rng, 100) < 8)                    /* random walk step */
        {
            do f = 1 + (int)below(rng, (uint32_t)(n - 1)); while (f == e);
        }
        else
        {
            bestd = INT_MAX; bestf = -1; ties = 0;
            for (f = 1; f < n; ++f)
            {
                if (f == e) continue;
                d = swap_delta(&L, e, f, 0);
                if (d < bestd) { bestd = d; bestf = f; ties = 1; }
                else if (d == bestd && below(rng, (uint32_t)++ties) == 0) bestf = f;
            }
            f = bestf;
        }
        swap_delta(&L, e, f, 1);
    }
    *work += maxit;
    return 0;
}

/**************************************************************************/
/* Stage 2 for MODE_EG: the same min-conflicts search, specialised to sums
   kept modulo n (no division), optionally started from a greedy dive. */

typedef struct {
    int n;
    const int *par;
    int lab[EG_MAXN];
    int edge_of[EG_MAXN];       /* inverse of lab */
    int S[EG_MAXN];             /* vertex sums mod n */
    int cnt[EG_MAXN];
    int cost;
    int skip;                   /* vertex not counted (root with residue 0 forced), or -1 */
    int inc[EG_MAXN][EG_MAXN], deg[EG_MAXN];
} lse_t;

static inline int
addmod(int a, int b, int n)     /* a in [0,n), |b| < n */
{
    a += b;
    if (a >= n) a -= n;
    else if (a < 0) a += n;
    return a;
}

/* Vertices whose sums change when the labels of edges e and f are swapped. */
static inline int
swap_support(const lse_t *L, int e, int f, int d, int *vs, int *ch)
{
    int pe = L->par[e], pf = L->par[f];

    if (pe == pf) { vs[0] = e; ch[0] = d; vs[1] = f;  ch[1] = -d; return 2; }
    if (f == pe)  { vs[0] = e; ch[0] = d; vs[1] = pf; ch[1] = -d; return 2; }
    if (e == pf)  { vs[0] = pe; ch[0] = d; vs[1] = f; ch[1] = -d; return 2; }
    vs[0] = e; ch[0] = d; vs[1] = pe; ch[1] = d;
    vs[2] = f; ch[2] = -d; vs[3] = pf; ch[3] = -d;
    return 4;
}

static inline int
swap_eval(lse_t *L, int e, int f, int apply)
{
    int vs[4], ch[4], nr[4], m, i, r, delta = 0, n = L->n;

    m = swap_support(L, e, f, L->lab[f] - L->lab[e], vs, ch);
    for (i = 0; i < m; ++i)
    {
        nr[i] = addmod(L->S[vs[i]], ch[i], n);
        if (vs[i] == L->skip) continue;
        r = L->S[vs[i]];
        delta -= (L->cnt[r] > 1);
        --L->cnt[r];
    }
    for (i = 0; i < m; ++i)
    {
        if (vs[i] == L->skip) continue;
        delta += (L->cnt[nr[i]] >= 1);
        ++L->cnt[nr[i]];
    }
    if (apply)
    {
        for (i = 0; i < m; ++i) L->S[vs[i]] = nr[i];
        r = L->lab[e]; L->lab[e] = L->lab[f]; L->lab[f] = r;
        L->edge_of[L->lab[e]] = e;
        L->edge_of[L->lab[f]] = f;
        L->cost += delta;
    }
    else
    {
        for (i = 0; i < m; ++i) if (vs[i] != L->skip) --L->cnt[nr[i]];
        for (i = 0; i < m; ++i) if (vs[i] != L->skip) ++L->cnt[L->S[vs[i]]];
    }
    return delta;
}

/* One pass of the DFS without backtracking; a vertex without a conflict-free
   label gets a random unused label.  Leaves the labeling in c->lab. */
static void
greedy_dive(ctx_t *c)
{
    uint32_t rd, bc, m;
    int depth, v, k, bk, best, p, l, s, np;

    ctx_reset(c);
    for (depth = 0; depth < c->n - 1; ++depth)
    {
        best = -1; bk = 99; bc = 0;
        for (rd = c->ready; rd; rd &= rd - 1)
        {
            v = CTZ(rd);
            m = cand_of(c, v);
            k = POPC(m);
            if (k < bk) { bk = k; best = v; bc = m; if (k == 0) break; }
        }
        v = best;
        if (bk == 0) { bc = c->U; bk = POPC(bc); }
        l = nth_bit(bc, (int)below(&c->rng, (uint32_t)bk));
        p = c->par[v];
        s = c->P[v] + l;
        if (s >= c->n) s -= c->n;
        c->R &= ~(1u << s);
        c->U &= ~(1u << l);
        c->lab[v] = l;
        c->ready &= ~(1u << v);
        if (c->nextleaf[v] >= 0) c->ready |= 1u << c->nextleaf[v];
        np = c->P[p] + l;
        if (np >= c->n) np -= c->n;
        c->P[p] = np;
        if (--c->remc[p] == 0 && p != 0) c->ready |= 1u << p;
    }
}

/* move: 0 = best swap of a random edge at a conflicting vertex v,
         1 = first improving swap of that edge,
         2 = best swap among those that move v onto a free residue */
static int
local_search_eg(ctx_t *c, int *lab, uint64_t maxit, int greedy, int noise, int move,
                uint64_t *work)
{
    lse_t L;
    int n = c->n, v, e, f, t, nc, d, bestd, bestf, beste, ties, k, x, r, conf[EG_MAXN];
    uint32_t F, Y, labmask = (n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1) & ~1u;
    uint64_t it;

    if (n < 3) return 0;
    L.n = n;
    L.par = c->par;
    L.skip = c->root_zero ? 0 : -1;
    for (v = 0; v < n; ++v) { L.deg[v] = 0; L.S[v] = 0; L.cnt[v] = 0; }
    for (e = 1; e < n; ++e)
    {
        L.inc[e][L.deg[e]++] = e;
        L.inc[L.par[e]][L.deg[L.par[e]]++] = e;
    }
    if (greedy)
    {
        greedy_dive(c);
        for (e = 1; e < n; ++e) L.lab[e] = c->lab[e];
    }
    else
    {
        for (e = 1; e < n; ++e) L.lab[e] = e;
        for (e = n - 1; e > 1; --e)
        {
            f = 1 + (int)below(&c->rng, (uint32_t)e);
            t = L.lab[e]; L.lab[e] = L.lab[f]; L.lab[f] = t;
        }
    }
    for (e = 1; e < n; ++e)
    {
        L.edge_of[L.lab[e]] = e;
        L.S[e] = addmod(L.S[e], L.lab[e], n);
        L.S[L.par[e]] = addmod(L.S[L.par[e]], L.lab[e], n);
    }
    L.cost = 0;
    if (c->root_zero) L.cnt[0] = 1;     /* residue 0 is reserved for the root */
    for (v = 0; v < n; ++v)
    {
        if (v == L.skip) continue;
        L.cost += (L.cnt[L.S[v]] >= 1);
        ++L.cnt[L.S[v]];
    }

    for (it = 0; it < maxit; ++it)
    {
        if (L.cost == 0)
        {
            for (e = 1; e < n; ++e) lab[e] = L.lab[e];
            *work += it;
            return 1;
        }
        nc = 0;
        for (v = 0; v < n; ++v)
            if (v != L.skip && L.cnt[L.S[v]] > 1) conf[nc++] = v;
        v = conf[below(&c->rng, (uint32_t)nc)];
        e = L.inc[v][below(&c->rng, (uint32_t)L.deg[v])];

        if ((int)below(&c->rng, 100) < noise)
        {
            do f = 1 + (int)below(&c->rng, (uint32_t)(n - 1)); while (f == e);
        }
        else if (move == 2)
        {
            F = 0;
            for (r = 0; r < n; ++r)
                if (L.cnt[r] == 0) F |= 1u << r;
            bestd = INT_MAX; bestf = beste = -1; ties = 0;
            for (k = 0; k < L.deg[v]; ++k)
            {
                int ee = L.inc[v][k], sh;
                uint64_t FF = F;
                x = L.lab[ee];
                sh = addmod(L.S[v], -x, n);       /* bit y <-> residue S[v]-x+y */
                Y = (uint32_t)((FF >> sh) | (FF << (n - sh))) & labmask & ~(1u << x);
                for (; Y; Y &= Y - 1)
                {
                    f = L.edge_of[CTZ(Y)];
                    d = swap_eval(&L, ee, f, 0);
                    if (d < bestd) { bestd = d; bestf = f; beste = ee; ties = 1; }
                    else if (d == bestd && below(&c->rng, (uint32_t)++ties) == 0)
                    { bestf = f; beste = ee; }
                }
            }
            if (bestf < 0)
                do f = 1 + (int)below(&c->rng, (uint32_t)(n - 1)); while (f == e);
            else
            { e = beste; f = bestf; }
        }
        else if (move == 1)
        {
            /* first improving swap in random cyclic order, else a best one */
            bestd = INT_MAX; bestf = -1; ties = 0;
            f = 1 + (int)below(&c->rng, (uint32_t)(n - 1));
            for (t = 0; t < n - 1; ++t, f = (f == n - 1 ? 1 : f + 1))
            {
                if (f == e) continue;
                d = swap_eval(&L, e, f, 0);
                if (d < 0) { bestf = f; break; }
                if (d < bestd) { bestd = d; bestf = f; ties = 1; }
                else if (d == bestd && below(&c->rng, (uint32_t)++ties) == 0) bestf = f;
            }
            f = bestf;
        }
        else
        {
            bestd = INT_MAX; bestf = -1; ties = 0;
            for (f = 1; f < n; ++f)
            {
                if (f == e) continue;
                d = swap_eval(&L, e, f, 0);
                if (d < bestd) { bestd = d; bestf = f; ties = 1; }
                else if (d == bestd && below(&c->rng, (uint32_t)++ties) == 0) bestf = f;
            }
            f = bestf;
        }
        swap_eval(&L, e, f, 1);
    }
    *work += maxit;
    return 0;
}

/**************************************************************************/

/* Tuned on all trees of order 19 and on parts of orders 23-29 (README). */
void
eg_default_limits(eg_limits *lim, int mode)
{
    if (mode == MODE_EG)
    {
        lim->s1_restarts = 0;           /* the targeted local search is faster */
        lim->s1_base = 256;
        lim->s2_tries = 2000;
        lim->s2_iters = 2000;
        lim->s2_greedy = 1;
        lim->s2_noise = 0;
        lim->s2_move = 2;
    }
    else
    {
        lim->s1_restarts = 32;          /* antimagic: the DFS rarely backtracks */
        lim->s1_base = 256;
        lim->s2_tries = 50;
        lim->s2_iters = 20000;
        lim->s2_greedy = 0;
        lim->s2_noise = 8;
        lim->s2_move = 0;
    }
    lim->s3_nodes = 4000000000ULL;
}

int
eg_check(int mode, int n, const int *par, const int *lab)
{
    int v, e, k, S[EG_MAXN], seen[600];
    uint32_t used = 0;

    if (n < 1 || n > EG_MAXN) return 0;
    for (v = 0; v < n; ++v) S[v] = 0;
    for (e = 1; e < n; ++e)
    {
        if (par[e] < 0 || par[e] >= e) return 0;
        if (lab[e] < 1 || lab[e] > n - 1 || (used >> lab[e]) & 1) return 0;
        used |= 1u << lab[e];
        S[e] += lab[e];
        S[par[e]] += lab[e];
    }
    memset(seen, 0, sizeof(seen));
    for (v = 0; v < n; ++v)
    {
        k = mode == MODE_EG ? S[v] % n : S[v];
        if (seen[k]) return 0;
        seen[k] = 1;
    }
    return 1;
}

int
eg_solve(int mode, int n, const int *par, int *lab, uint64_t seed,
         const eg_limits *lim, eg_stats *st)
{
    return eg_solve_ex(mode, n, par, lab, seed, lim, st, 0);
}

int
eg_solve_ex(int mode, int n, const int *par, int *lab, uint64_t seed,
            const eg_limits *lim, eg_stats *st, int root_zero)
{
    ctx_t c;
    int i, r, v;

    st->result = RES_UNSOLVED;
    st->stage = 0;
    st->work = 0;
    st->restarts = 0;
    if (n <= 1) { st->result = RES_SOLVED; st->stage = 1; return RES_SOLVED; }

    ctx_setup(&c, mode, n, par);
    c.root_zero = (mode == MODE_EG && root_zero);
    c.rng = seed;

    for (i = 0; i < lim->s1_restarts; ++i)
    {
        ctx_reset(&c);
        c.nodes = 0;
        c.limit = lim->s1_base * luby((uint32_t)i + 1);
        r = dfs(&c, 0);
        st->work += c.nodes;
        ++st->restarts;
        if (r == OK)
        {
            for (v = 1; v < n; ++v) lab[v] = c.lab[v];
            st->result = RES_SOLVED; st->stage = 1;
            return RES_SOLVED;
        }
        if (r == FAIL)
        {
            st->result = RES_IMPOSSIBLE; st->stage = 1;
            return RES_IMPOSSIBLE;
        }
    }

    for (i = 0; i < lim->s2_tries; ++i)
        if (mode == MODE_EG
            ? local_search_eg(&c, lab, lim->s2_iters, lim->s2_greedy, lim->s2_noise,
                              lim->s2_move, &st->work)
            : local_search(mode, n, c.par, lab, &c.rng, lim->s2_iters, &st->work))
        {
            st->result = RES_SOLVED; st->stage = 2;
            return RES_SOLVED;
        }

    ctx_reset(&c);
    c.nodes = 0;
    c.limit = lim->s3_nodes ? lim->s3_nodes : UINT64_MAX;
    r = dfs(&c, 0);
    st->work += c.nodes;
    if (r == OK)
    {
        for (v = 1; v < n; ++v) lab[v] = c.lab[v];
        st->result = RES_SOLVED; st->stage = 3;
    }
    else if (r == FAIL)
    {
        st->result = RES_IMPOSSIBLE; st->stage = 3;
    }
    return st->result;
}
