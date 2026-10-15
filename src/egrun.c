/* egrun.c - enumerate all trees of order n (part res/mod) with gentreeg and
   find an edge-graceful (MODE_EG) or antimagic (MODE_AM) labeling of each.

   Linked with nauty's gentreeg.c compiled with
       -DOUTPROC=eg_outproc -DGENTREEG_MAIN=gentreeg_main

   usage: egrun N RES MOD [--mode eg|am] [--cert FILE|-] [--count C] [--hard FILE] [--hard-work W]
                [--summary FILE] [--salt S] [--sample K]
                [--s1-restarts K] [--s1-base B] [--s2-tries K]
                [--s2-iters I] [--s2-greedy 0|1] [--s2-noise PCT] [--s2-move 0|1|2] [--s3-nodes M]

   Certificate file: 64-byte header, then one record per tree in gentreeg
   order.  Record = labels of the edges {j, par[j]}, j = 2..n (gentreeg's
   1-based numbering), 5 bits each, LSB first.  A record of zeros marks a
   tree for which no labeling was found.  The --hard file lists unsolved
   trees, trees solved only by stage 3, and trees that needed >= W units of
   work (default 10^6).

   Header (little endian): "EGCERT1\0", u32 n, u32 mode, u32 res, u32 mod,
   u64 count, u32 recbytes, u32 flags, 24 zero bytes.

   --cert - streams the certificate to standard output (for "egcheck ... -").
   With --count C (the number of trees of the part, e.g. from gentreeg -u)
   the header is exact from the start, so the stream is byte-for-byte the
   file egrun would write; without it flags bit 0 marks the count as unknown.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "egsearch.h"

extern int gentreeg_main(int argc, char *argv[]);
void eg_outproc(FILE *f, int *par1, int n);

#define NHIST 64

static struct {
    int mode, n, res, mod, sample, recbytes;
    FILE *cert, *hard;
    const char *cert_path, *hard_path, *summary_path;
    uint64_t salt, hard_work, count;
    int have_count, cert_stdout;
    int zr;                     /* mode zr: EG labeling with residue 0 at each internal vertex */
    uint64_t pairs, pairs_solved, pairs_impossible, pairs_unsolved;
    int repeat, top, ntop;      /* hardness mode: K solves per tree, keep the T hardest */
    const char *top_path;
    double repeat_mean_sum;
    struct { double mean; uint64_t idx; int d2; int par[EG_MAXN]; } *tops;
    eg_limits lim;
    uint64_t trees, solved, impossible, unsolved, stage[4];
    uint64_t work_total, work_max, restarts_total;
    uint64_t hist[NHIST];
    uint64_t d2_count[EG_MAXN + 1], d2_work[EG_MAXN + 1], d2_max[EG_MAXN + 1];
    int hardest_par[EG_MAXN];
    double t0;
} G;

static void
die(const char *msg)
{
    fprintf(stderr, ">E egrun: %s\n", msg);
    exit(2);
}

static double
now(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static uint64_t
mix64(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void
put32(unsigned char *b, uint32_t x)
{
    int i;
    for (i = 0; i < 4; ++i) b[i] = (unsigned char)(x >> (8 * i));
}

static void
put64(unsigned char *b, uint64_t x)
{
    int i;
    for (i = 0; i < 8; ++i) b[i] = (unsigned char)(x >> (8 * i));
}

/* With a known count (--count, or the final rewrite of a file) the header is
   exact; a stream without --count says so in flags bit 0. */
static void
write_header(uint64_t count, int count_unknown)
{
    unsigned char h[64];
    memset(h, 0, sizeof(h));
    memcpy(h, "EGCERT1", 8);
    put32(h + 8, (uint32_t)G.n);
    put32(h + 12, (uint32_t)(G.zr ? 3 : G.mode));
    put32(h + 16, (uint32_t)G.res);
    put32(h + 20, (uint32_t)G.mod);
    put64(h + 24, count);
    put32(h + 32, (uint32_t)G.recbytes);
    put32(h + 36, count_unknown ? 1u : 0u);
    if ((!G.cert_stdout && fseek(G.cert, 0, SEEK_SET) != 0) || fwrite(h, 1, 64, G.cert) != 64)
        die("cannot write certificate header");
}

static void
write_record(int n, const int *lab)
{
    unsigned char rec[32];
    int v, b, bit;

    if (!G.cert) return;
    memset(rec, 0, sizeof(rec));
    for (v = 1, bit = 0; v < n; ++v, bit += 5)
        for (b = 0; b < 5; ++b)
            if ((lab[v] >> b) & 1) rec[(bit + b) >> 3] |= (unsigned char)(1u << ((bit + b) & 7));
    if (fwrite(rec, 1, (size_t)G.recbytes, G.cert) != (size_t)G.recbytes)
        die("cannot write certificate record");
}

/* Renumber the tree from a new root (BFS order, so parents precede children). */
static void
reroot(int n, const int *par, int root, int *npar, int *oldof)
{
    int adj[EG_MAXN][EG_MAXN], na[EG_MAXN], newof[EG_MAXN], q, h, u, w, i;

    for (u = 0; u < n; ++u) { na[u] = 0; newof[u] = -1; }
    for (u = 1; u < n; ++u) { adj[u][na[u]++] = par[u]; adj[par[u]][na[par[u]]++] = u; }
    oldof[0] = root; newof[root] = 0; npar[0] = -1; q = 1;
    for (h = 0; h < q; ++h)
    {
        u = oldof[h];
        for (i = 0; i < na[u]; ++i)
        {
            w = adj[u][i];
            if (newof[w] < 0) { newof[w] = q; oldof[q] = w; npar[q] = h; ++q; }
        }
    }
}

static void
log_hard(uint64_t idx, const char *status, int stage, uint64_t w, int d2, int n, const int *par, int vertex)
{
    int v;
    if (!G.hard) return;
    fprintf(G.hard, "%llu %s %d %llu %d", (unsigned long long)idx, status, stage, (unsigned long long)w, d2);
    if (vertex >= 0) fprintf(G.hard, " v=%d", vertex);
    fprintf(G.hard, " :");
    for (v = 1; v < n; ++v) fprintf(G.hard, " %d", par[v]);
    fprintf(G.hard, "\n");
    fflush(G.hard);
}

static void
account(uint64_t w, int d2, int n, const int *par)
{
    int h, v;
    G.work_total += w;
    if (w > G.work_max || G.trees == 1)
    {
        G.work_max = w;
        for (v = 0; v < n; ++v) G.hardest_par[v] = par[v];
    }
    for (h = 0; h < NHIST - 1 && (w + 1) >> (h + 1); ++h) {}
    ++G.hist[h];
    ++G.d2_count[d2];
    G.d2_work[d2] += w;
    if (w > G.d2_max[d2]) G.d2_max[d2] = w;
}

/* Mode zr: for every internal vertex v (in increasing order) an edge-graceful
   labeling whose vertex sum at v is 0 mod n; one certificate record each. */
static void
zr_tree(uint64_t idx, int n, const int *par, const int *deg, int d2, uint64_t seed)
{
    int v, c, a, b, e, r, lab[EG_MAXN], nlab[EG_MAXN], npar[EG_MAXN], oldof[EG_MAXN];
    int all_ok = 1, any_impossible = 0, sum;
    uint64_t wsum = 0;
    eg_stats st;

    for (v = 0; v < n; ++v)
    {
        if (deg[v] < 2) continue;
        reroot(n, par, v, npar, oldof);
        r = eg_solve_ex(MODE_EG, n, npar, nlab, mix64(seed ^ (0x9E3779B97F4A7C15ULL * (uint64_t)(v + 1))),
                        &G.lim, &st, 1);
        ++G.pairs;
        wsum += st.work;
        G.restarts_total += st.restarts;
        if (r == RES_SOLVED)
        {
            for (c = 1; c < n; ++c)
            {
                a = oldof[c]; b = oldof[npar[c]];
                e = (par[a] == b) ? a : b;          /* original edge = its lower endpoint */
                lab[e] = nlab[c];
            }
            for (sum = 0, e = 1; e < n; ++e)
                if (e == v || par[e] == v) sum += lab[e];
            if (!eg_check(MODE_EG, n, par, lab) || sum % n != 0) die("internal check failed (zr)");
            ++G.pairs_solved;
            if (st.stage >= 3 || st.work >= G.hard_work)
                log_hard(idx, "solved", st.stage, st.work, d2, n, par, v);
        }
        else
        {
            all_ok = 0;
            if (r == RES_IMPOSSIBLE) { ++G.pairs_impossible; any_impossible = 1; }
            else ++G.pairs_unsolved;
            for (e = 1; e < n; ++e) lab[e] = 0;
            log_hard(idx, r == RES_IMPOSSIBLE ? "IMPOSSIBLE" : "unsolved", st.stage, st.work, d2, n, par, v);
        }
        write_record(n, lab);
    }
    account(wsum, d2, n, par);
    if (all_ok) { ++G.solved; ++G.stage[1]; }
    else if (any_impossible) ++G.impossible;
    else ++G.unsolved;
}

static void
top_insert(double mean, uint64_t idx, int d2, int n, const int *par)
{
    int i, k = -1;
    if (G.ntop < G.top) k = G.ntop++;
    else
    {
        for (i = 0; i < G.top; ++i)
            if (k < 0 || G.tops[i].mean < G.tops[k].mean) k = i;
        if (G.tops[k].mean >= mean) return;
    }
    G.tops[k].mean = mean;
    G.tops[k].idx = idx;
    G.tops[k].d2 = d2;
    for (i = 0; i < n; ++i) G.tops[k].par[i] = par[i];
}

void
eg_outproc(FILE *f, int *par1, int n)
{
    int par[EG_MAXN], lab[EG_MAXN], deg[EG_MAXN];
    uint64_t idx, seed, w;
    eg_stats st;
    int v, d2, r;

    (void)f;
    idx = G.trees++;
    if (G.sample > 1 && idx % (uint64_t)G.sample != 0) return;
    if (n > EG_MAXN) die("tree too large");

    par[0] = -1;
    for (v = 0; v < n; ++v) deg[v] = 0;
    for (v = 1; v < n; ++v)
    {
        par[v] = par1[v + 1] - 1;
        if (par[v] < 0 || par[v] >= v) die("unexpected parent array order");
        ++deg[v];
        ++deg[par[v]];
    }
    d2 = 0;
    for (v = 0; v < n; ++v) d2 += (deg[v] == 2);

    seed = mix64(mix64(mix64(mix64(G.salt ^ (uint64_t)n) ^ (uint64_t)G.res)
                       ^ (uint64_t)G.mod) ^ idx);
    if (G.zr) { zr_tree(idx, n, par, deg, d2, seed); return; }
    r = eg_solve(G.mode, n, par, lab, seed, &G.lim, &st);

    w = st.work;
    G.restarts_total += st.restarts;
    account(w, d2, n, par);
    if (G.repeat > 1)
    {
        int k, lab2[EG_MAXN];
        eg_stats s2;
        uint64_t tot = w;
        for (k = 1; k < G.repeat; ++k)
        {
            eg_solve(G.mode, n, par, lab2, mix64(seed + 0x9E3779B97F4A7C15ULL * (uint64_t)k), &G.lim, &s2);
            tot += s2.work;
        }
        G.repeat_mean_sum += (double)tot / G.repeat;
        if (G.top > 0) top_insert((double)tot / G.repeat, idx, d2, n, par);
    }

    if (r == RES_SOLVED)
    {
        if (!eg_check(G.mode, n, par, lab)) die("internal check failed");
        ++G.solved;
        ++G.stage[st.stage];
    }
    else
    {
        if (r == RES_IMPOSSIBLE) ++G.impossible; else ++G.unsolved;
        for (v = 1; v < n; ++v) lab[v] = 0;
    }

    if (r != RES_SOLVED || st.stage >= 3 || w >= G.hard_work)
        log_hard(idx, r == RES_SOLVED ? "solved" : r == RES_IMPOSSIBLE ? "IMPOSSIBLE" : "unsolved",
                 st.stage, w, d2, n, par, -1);
    write_record(n, lab);
}

static void
write_summary(double wall)
{
    char tmp[1024];
    FILE *f;
    int i, last;

    if (!G.summary_path) f = stdout;
    else
    {
        snprintf(tmp, sizeof(tmp), "%s.tmp", G.summary_path);
        if ((f = fopen(tmp, "w")) == NULL) die("cannot write summary");
    }
    fprintf(f, "{\n  \"n\": %d, \"mode\": \"%s\", \"res\": %d, \"mod\": %d,\n",
            G.n, G.zr ? "zr" : G.mode == MODE_EG ? "eg" : "am", G.res, G.mod);
    fprintf(f, "  \"trees\": %llu, \"sample\": %d, \"salt\": %llu,\n",
            (unsigned long long)G.trees, G.sample, (unsigned long long)G.salt);
    fprintf(f, "  \"solved\": %llu, \"impossible\": %llu, \"unsolved\": %llu,\n",
            (unsigned long long)G.solved, (unsigned long long)G.impossible,
            (unsigned long long)G.unsolved);
    if (G.zr)
        fprintf(f, "  \"zr_pairs\": %llu, \"zr_pairs_solved\": %llu, \"zr_pairs_impossible\": %llu, "
                   "\"zr_pairs_unsolved\": %llu,\n", (unsigned long long)G.pairs,
                (unsigned long long)G.pairs_solved, (unsigned long long)G.pairs_impossible,
                (unsigned long long)G.pairs_unsolved);
    if (G.repeat > 1)
        fprintf(f, "  \"repeat\": %d, \"repeat_mean_work\": %.3f,\n", G.repeat,
                G.trees ? G.repeat_mean_sum / (double)(G.trees / (uint64_t)G.sample) : 0.0);
    fprintf(f, "  \"solved_by_stage\": [%llu, %llu, %llu],\n",
            (unsigned long long)G.stage[1], (unsigned long long)G.stage[2],
            (unsigned long long)G.stage[3]);
    fprintf(f, "  \"work_total\": %llu, \"work_max\": %llu, \"restarts_total\": %llu,\n",
            (unsigned long long)G.work_total, (unsigned long long)G.work_max,
            (unsigned long long)G.restarts_total);
    fprintf(f, "  \"limits\": {\"s1_restarts\": %d, \"s1_base\": %llu, \"s2_tries\": %d, "
               "\"s2_iters\": %llu, \"s2_greedy\": %d, \"s2_noise\": %d, \"s2_move\": %d, \"s3_nodes\": %llu},\n",
            G.lim.s1_restarts, (unsigned long long)G.lim.s1_base, G.lim.s2_tries,
            (unsigned long long)G.lim.s2_iters, G.lim.s2_greedy, G.lim.s2_noise, G.lim.s2_move,
            (unsigned long long)G.lim.s3_nodes);
    for (last = NHIST - 1; last > 0 && G.hist[last] == 0; --last) {}
    fprintf(f, "  \"hist_log2_work\": [");
    for (i = 0; i <= last; ++i) fprintf(f, "%s%llu", i ? ", " : "", (unsigned long long)G.hist[i]);
    fprintf(f, "],\n  \"by_deg2\": {");
    for (i = 0, last = 0; i <= EG_MAXN; ++i)
        if (G.d2_count[i])
        {
            fprintf(f, "%s\"%d\": [%llu, %llu, %llu]", last ? ", " : "", i,
                    (unsigned long long)G.d2_count[i], (unsigned long long)G.d2_work[i],
                    (unsigned long long)G.d2_max[i]);
            last = 1;
        }
    fprintf(f, "},\n  \"hardest_par\": [");
    for (i = 1; i < G.n; ++i) fprintf(f, "%s%d", i > 1 ? ", " : "", G.hardest_par[i]);
    fprintf(f, "],\n  \"wall_sec\": %.3f\n}\n", wall);

    if (G.summary_path)
    {
        if (fclose(f) != 0) die("cannot close summary");
        remove(G.summary_path);
        if (rename(tmp, G.summary_path) != 0) die("cannot rename summary");
    }
}

static uint64_t
argnum(int argc, char **argv, int *i)
{
    if (*i + 1 >= argc) die("missing option value");
    return strtoull(argv[++*i], NULL, 10);
}

int
main(int argc, char **argv)
{
    char nbuf[16], rmbuf[48], *gargv[5];
    int i;

    if (argc < 4)
    {
        fprintf(stderr, "usage: egrun N RES MOD [--mode eg|am] [--cert F] [--hard F] "
                        "[--summary F] [--salt S] [--sample K] [--s1-restarts K] "
                        "[--s1-base B] [--s2-tries K] [--s2-iters I] [--s2-greedy 0|1] [--s2-noise PCT] [--s2-move 0|1|2] [--s3-nodes M]\n");
        return 2;
    }
    memset(&G, 0, sizeof(G));
    G.n = atoi(argv[1]);
    G.res = atoi(argv[2]);
    G.mod = atoi(argv[3]);
    G.mode = (G.n % 2 == 1) ? MODE_EG : MODE_AM;
    G.salt = 1;
    G.sample = 1;
    G.hard_work = 1000000;
    for (i = 4; i + 1 < argc; ++i)          /* defaults depend on the mode */
        if (!strcmp(argv[i], "--mode")) G.mode = !strcmp(argv[i + 1], "am") ? MODE_AM : MODE_EG;
    eg_default_limits(&G.lim, G.mode);

    for (i = 4; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--mode"))
        {
            if (i + 1 >= argc) die("missing mode");
            ++i;
            if (!strcmp(argv[i], "eg")) G.mode = MODE_EG;
            else if (!strcmp(argv[i], "zr")) { G.mode = MODE_EG; G.zr = 1; }
            else if (!strcmp(argv[i], "am")) G.mode = MODE_AM;
            else die("mode must be eg or am");
        }
        else if (!strcmp(argv[i], "--cert"))    { if (++i >= argc) die("missing file"); G.cert_path = argv[i]; }
        else if (!strcmp(argv[i], "--hard"))    { if (++i >= argc) die("missing file"); G.hard_path = argv[i]; }
        else if (!strcmp(argv[i], "--summary")) { if (++i >= argc) die("missing file"); G.summary_path = argv[i]; }
        else if (!strcmp(argv[i], "--salt"))        G.salt = argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--sample"))      G.sample = (int)argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--hard-work"))   G.hard_work = argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--count"))       { G.count = argnum(argc, argv, &i); G.have_count = 1; }
        else if (!strcmp(argv[i], "--repeat"))      G.repeat = (int)argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--top"))         G.top = (int)argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--top-file"))    { if (++i >= argc) die("missing file"); G.top_path = argv[i]; }
        else if (!strcmp(argv[i], "--s1-restarts")) G.lim.s1_restarts = (int)argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--s1-base"))     G.lim.s1_base = argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--s2-tries"))    G.lim.s2_tries = (int)argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--s2-iters"))    G.lim.s2_iters = argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--s3-nodes"))    G.lim.s3_nodes = argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--s2-greedy"))   G.lim.s2_greedy = (int)argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--s2-noise"))    G.lim.s2_noise = (int)argnum(argc, argv, &i);
        else if (!strcmp(argv[i], "--s2-move"))     G.lim.s2_move = (int)argnum(argc, argv, &i);
        else die("unknown option");
    }
    if (G.n < 1 || G.n > EG_MAXN) die("n out of range");
    if (G.mod < 1 || G.res < 0 || G.res >= G.mod) die("bad res/mod");
    if (G.sample < 1) die("bad sample");
    if (G.cert_path && G.sample > 1) die("--cert cannot be combined with --sample");
    if (G.zr && G.n % 2 == 0) die("mode zr needs odd n");
    if (G.zr && (G.repeat > 1 || G.top)) die("mode zr cannot be combined with --repeat/--top");
    if (G.top > 0)
    {
        if (G.repeat < 2 || !G.top_path) die("--top needs --repeat K (K >= 2) and --top-file");
        if ((G.tops = calloc((size_t)G.top, sizeof(*G.tops))) == NULL) die("out of memory");
    }
    G.recbytes = (5 * (G.n - 1) + 7) / 8;
    if (G.recbytes == 0) G.recbytes = 1;

    if (G.cert_path && !strcmp(G.cert_path, "-"))
    {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        G.cert = stdout;
        G.cert_stdout = 1;
        setvbuf(G.cert, NULL, _IOFBF, 1 << 20);
        write_header(G.have_count ? G.count : 0, !G.have_count);
    }
    else if (G.cert_path)
    {
        if ((G.cert = fopen(G.cert_path, "wb")) == NULL) die("cannot open certificate file");
        setvbuf(G.cert, NULL, _IOFBF, 1 << 20);
        write_header(0, 0);
    }
    if (G.hard_path && (G.hard = fopen(G.hard_path, "w")) == NULL) die("cannot open hard file");

    snprintf(nbuf, sizeof(nbuf), "%d", G.n);
    snprintf(rmbuf, sizeof(rmbuf), "%d/%d", G.res, G.mod);
    gargv[0] = "gentreeg"; gargv[1] = "-q"; gargv[2] = nbuf; gargv[3] = rmbuf; gargv[4] = NULL;

    G.t0 = now();
    gentreeg_main(4, gargv);

    if (G.cert)
    {
        if (!G.cert_stdout) write_header(G.trees, 0);
        if (fflush(G.cert) != 0 || (!G.cert_stdout && fclose(G.cert) != 0))
            die("cannot close certificate file");
    }
    if (G.have_count && G.count != G.trees) die("--count differs from the number of trees");
    if (G.hard && fclose(G.hard) != 0) die("cannot close hard file");
    write_summary(now() - G.t0);
    if (G.top > 0)
    {
        FILE *tf;
        int i, j, v;
        if ((tf = fopen(G.top_path, "w")) == NULL) die("cannot write top file");
        for (i = 0; i < G.ntop; ++i)               /* selection sort, hardest first */
        {
            int k = i;
            for (j = i + 1; j < G.ntop; ++j) if (G.tops[j].mean > G.tops[k].mean) k = j;
            if (k != i) { __typeof__(*G.tops) t = G.tops[i]; G.tops[i] = G.tops[k]; G.tops[k] = t; }
            fprintf(tf, "%.2f %d %llu :", G.tops[i].mean, G.tops[i].d2, (unsigned long long)G.tops[i].idx);
            for (v = 1; v < G.n; ++v) fprintf(tf, " %d", G.tops[i].par[v]);
            fprintf(tf, "\n");
        }
        fclose(tf);
    }
    return (G.unsolved || G.impossible) ? 1 : 0;
}
