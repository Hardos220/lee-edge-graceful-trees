/* egverify.c - independent checker for certificates written by egrun.
   Shares no code with egrun/egsearch on purpose.  Two builds:

   egverify (plain):
       gentreeg -p -q N RES/MOD | egverify CERTFILE [--expect-count C]
     reads the parent arrays printed by the stand-alone gentreeg (one tree
     per line: par[1..n], par[1] = 0, 1-based).

   egcheck (compiled with -DWITH_GENTREEG, linked with gentreeg.c using
   -DOUTPROC=check_outproc -DGENTREEG_MAIN=gentreeg_main):
       egcheck N RES MOD CERTFILE|- [--expect-count C]
     generates the trees itself and reads the certificate from a file or,
     with "-", from standard input (binary), e.g. streamed by egrun --cert -.
     Nothing goes through text or the disk, so checking runs alongside the
     search at a few percent of its CPU time.

   For every tree: the record must be a bijection from the edges {j, par[j]}
   (j = 2..n, in gentreeg's numbering) onto 1..n-1, and
     mode 1 (edge-graceful): vertex sums pairwise distinct modulo n,
     mode 2 (antimagic):     vertex sums pairwise distinct;
   for mode 1 the antimagic property is checked as well.  The SHA-256 of all
   certificate bytes read (header included) is reported.

   Prints one JSON line; exit status 0 iff every tree has a valid labeling,
   the certificate has exactly one record per tree and the counts agree with
   the header (unless the header says the count is unknown) and --expect-count.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#define MAXV 32

/**************************************************************************/
/* SHA-256 (FIPS 180-4) */

typedef struct {
    uint32_t h[8];
    uint64_t len;
    unsigned char buf[64];
    size_t nbuf;
} sha256_t;

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR(x, k) (((x) >> (k)) | ((x) << (32 - (k))))

static void
sha_block(sha256_t *s, const unsigned char *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; ++i)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16
             | (uint32_t)p[4 * i + 2] << 8 | (uint32_t)p[4 * i + 3];
    for (i = 16; i < 64; ++i)
        w[i] = w[i - 16] + (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3))
             + w[i - 7] + (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10));
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
    e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (i = 0; i < 64; ++i)
    {
        t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void
sha_init(sha256_t *s)
{
    static const uint32_t h0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, h0, sizeof(h0));
    s->len = 0;
    s->nbuf = 0;
}

static void
sha_update(sha256_t *s, const unsigned char *p, size_t len)
{
    s->len += len;
    while (len > 0)
    {
        size_t k = 64 - s->nbuf;
        if (k > len) k = len;
        memcpy(s->buf + s->nbuf, p, k);
        s->nbuf += k; p += k; len -= k;
        if (s->nbuf == 64) { sha_block(s, s->buf); s->nbuf = 0; }
    }
}

static void
sha_final(sha256_t *s, char *hex)
{
    unsigned char pad[72];
    uint64_t bits = s->len * 8;
    size_t padlen = (s->nbuf < 56) ? 56 - s->nbuf : 120 - s->nbuf;
    int i;

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (i = 0; i < 8; ++i) pad[padlen + i] = (unsigned char)(bits >> (56 - 8 * i));
    sha_update(s, pad, padlen + 8);
    for (i = 0; i < 32; ++i)
        sprintf(hex + 2 * i, "%02x", (unsigned)((s->h[i / 4] >> (24 - 8 * (i % 4))) & 0xff));
}

/**************************************************************************/
/* certificate reading and the per-tree check */

static struct {
    FILE *cf;
    sha256_t sha;
    int n, mode, res, mod, recbytes, count_unknown;
    unsigned long long hcount, trees, good, bad, unsolved, am_bad, first_bad;
    unsigned long long records, rec_good, rec_bad, rec_empty;   /* mode 3: one record per internal vertex */
    int short_cert, bad_line;
} V;

static size_t
cert_read(unsigned char *b, size_t len)
{
    size_t got = fread(b, 1, len, V.cf);
    sha_update(&V.sha, b, got);
    return got;
}

static unsigned long long
le(const unsigned char *b, int len)
{
    unsigned long long x = 0;
    int i;
    for (i = len - 1; i >= 0; --i) x = (x << 8) | b[i];
    return x;
}

static int
read_header(void)
{
    unsigned char hdr[64];

    if (cert_read(hdr, 64) != 64 || memcmp(hdr, "EGCERT1", 8) != 0)
    {
        fprintf(stderr, "bad certificate header\n");
        return 0;
    }
    V.n = (int)le(hdr + 8, 4);
    V.mode = (int)le(hdr + 12, 4);
    V.res = (int)le(hdr + 16, 4);
    V.mod = (int)le(hdr + 20, 4);
    V.hcount = le(hdr + 24, 8);
    V.recbytes = (int)le(hdr + 32, 4);
    V.count_unknown = (int)(le(hdr + 36, 4) & 1);
    if (V.n < 2 || V.n > MAXV || (V.mode < 1 || V.mode > 3) || V.recbytes != (5 * (V.n - 1) + 7) / 8)
    {
        fprintf(stderr, "unsupported header values\n");
        return 0;
    }
    return 1;
}

/* Checks the next record against the tree par[1..n] (1-based, par[1] = 0).
   zero_at > 0 additionally requires the vertex sum at zero_at to be 0 mod n.
   Returns 0 valid, 1 invalid, 2 empty record (no labeling found), 3 certificate ended. */
static int
check_record(const long *par, int n, int zero_at)
{
    unsigned char rec[64];
    int lab[MAXV + 1], j, ok, zero, eg = (V.mode == 1 || V.mode == 3);
    long sum[MAXV + 1];
    char seen[MAXV * MAXV + 1];

    if (cert_read(rec, (size_t)V.recbytes) != (size_t)V.recbytes) return 3;

    /* unpack: edge (j, par[j]) for j = 2..n gets bits 5(j-2) .. 5(j-2)+4 */
    zero = 1;
    for (j = 2; j <= n; ++j)
    {
        int v = 0, b, pos = 5 * (j - 2);
        for (b = 0; b < 5; ++b)
            if (rec[(pos + b) / 8] & (1 << ((pos + b) % 8))) v |= 1 << b;
        lab[j] = v;
        if (v) zero = 0;
    }
    if (zero) return 2;

    ok = (par[1] == 0);
    memset(seen, 0, sizeof(seen));
    for (j = 1; j <= n; ++j) sum[j] = 0;
    for (j = 2; j <= n && ok; ++j)
    {
        if (par[j] < 1 || par[j] > n || par[j] == j) ok = 0;
        else if (lab[j] < 1 || lab[j] > n - 1 || seen[lab[j]]) ok = 0;
        else
        {
            seen[lab[j]] = 1;
            sum[j] += lab[j];
            sum[par[j]] += lab[j];
        }
    }
    if (ok && eg)
    {
        memset(seen, 0, sizeof(seen));
        for (j = 1; j <= n && ok; ++j)
        {
            if (seen[sum[j] % n]) ok = 0;
            seen[sum[j] % n] = 1;
        }
    }
    if (ok && zero_at > 0 && sum[zero_at] % n != 0) ok = 0;
    if (ok)
    {
        memset(seen, 0, sizeof(seen));
        for (j = 1; j <= n; ++j)
        {
            if (sum[j] > MAXV * MAXV || seen[sum[j]]) { ok = 0; if (eg) ++V.am_bad; break; }
            seen[sum[j]] = 1;
        }
    }
    return ok ? 0 : 1;
}

/* par[1..n] 1-based parent array with par[1] = 0.
   Modes 1, 2: one record per tree.  Mode 3: one record for every internal
   vertex j (degree >= 2, increasing j), an edge-graceful labeling with sum 0 at j. */
static void
check_tree(const long *par, int n)
{
    int j, r, deg[MAXV + 1], any_bad = 0, any_empty = 0;

    ++V.trees;
    if (V.short_cert) return;
    if (n != V.n) { V.bad_line = 1; return; }
    if (V.mode != 3)
    {
        r = check_record(par, n, 0);
        if (r == 3) { V.short_cert = 1; return; }
        if (r == 0) ++V.good;
        else if (r == 2) { ++V.unsolved; if (!V.first_bad) V.first_bad = V.trees; }
        else { ++V.bad; if (!V.first_bad) V.first_bad = V.trees; }
        return;
    }
    for (j = 1; j <= n; ++j) deg[j] = 0;
    for (j = 2; j <= n; ++j)
    {
        if (par[j] < 1 || par[j] > n) { V.bad_line = 1; return; }
        ++deg[j];
        ++deg[par[j]];
    }
    for (j = 1; j <= n; ++j)
    {
        if (deg[j] < 2) continue;
        r = check_record(par, n, j);
        if (r == 3) { V.short_cert = 1; return; }
        ++V.records;
        if (r == 0) ++V.rec_good;
        else if (r == 2) { ++V.rec_empty; any_empty = 1; }
        else { ++V.rec_bad; any_bad = 1; }
    }
    if (any_bad) { ++V.bad; if (!V.first_bad) V.first_bad = V.trees; }
    else if (any_empty) ++V.unsolved;
    else ++V.good;
}

static int
finish(const char *certname, int have_expect, unsigned long long expect)
{
    unsigned char rec[64];
    char hex[65];
    int extra, i, count_ok;

    extra = !V.short_cert && cert_read(rec, 1) == 1;
    while (extra && cert_read(rec, sizeof(rec)) > 0) {}      /* drain, keeps the pipe writer happy */
    sha_final(&V.sha, hex);
    count_ok = V.count_unknown || V.trees == V.hcount;

    printf("{\"cert\": \"");
    for (i = 0; certname[i]; ++i)                  /* JSON-escape the path */
    {
        if (certname[i] == '\\' || certname[i] == '"') putchar('\\');
        putchar(certname[i]);
    }
    printf("\", \"n\": %d, \"mode\": \"%s\", \"res\": %d, \"mod\": %d, "
           "\"trees\": %llu, \"header_count\": %llu, \"header_count_ok\": %s, "
           "\"valid\": %llu, \"invalid\": %llu, \"unsolved\": %llu, \"am_violations_in_eg\": %llu, "
           "\"first_problem\": %llu, \"certificate_too_short\": %s, \"trailing_data\": %s, "
           "\"expect_count_ok\": %s, \"records\": %llu, \"records_valid\": %llu, "
           "\"records_invalid\": %llu, \"records_empty\": %llu, \"sha256\": \"%s\"}\n",
           V.n, V.mode == 1 ? "eg" : V.mode == 2 ? "am" : "zr", V.res, V.mod, V.trees, V.hcount,
           count_ok ? "true" : "false", V.good, V.bad, V.unsolved, V.am_bad, V.first_bad,
           V.short_cert ? "true" : "false", extra ? "true" : "false",
           !have_expect ? "null" : (expect == V.trees ? "true" : "false"),
           V.records, V.rec_good, V.rec_bad, V.rec_empty, hex);

    if (V.bad || V.unsolved || V.short_cert || V.bad_line || extra || !count_ok
        || (have_expect && expect != V.trees) || V.good != V.trees)
        return 1;
    return 0;
}

static FILE *
open_cert(const char *name)
{
    FILE *f;
    if (!strcmp(name, "-"))
    {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        f = stdin;
    }
    else if ((f = fopen(name, "rb")) == NULL)
        return NULL;
    setvbuf(f, NULL, _IOFBF, 1 << 20);
    return f;
}

/**************************************************************************/

#ifdef WITH_GENTREEG

extern int gentreeg_main(int argc, char *argv[]);
void check_outproc(FILE *f, int *par, int n);

void
check_outproc(FILE *f, int *par, int n)
{
    long p[MAXV + 2];
    int j;

    (void)f;
    if (n > MAXV) { V.bad_line = 1; ++V.trees; return; }
    for (j = 1; j <= n; ++j) p[j] = par[j];
    check_tree(p, n);
}

int
main(int argc, char **argv)
{
    unsigned long long expect = 0;
    int have_expect = 0, i, n, res, mod;
    char nbuf[16], rmbuf[48], *gargv[5];

    if (argc < 5)
    {
        fprintf(stderr, "usage: egcheck N RES MOD CERTFILE|- [--expect-count C]\n");
        return 2;
    }
    n = atoi(argv[1]); res = atoi(argv[2]); mod = atoi(argv[3]);
    for (i = 5; i < argc; ++i)
        if (!strcmp(argv[i], "--expect-count") && i + 1 < argc)
        {
            expect = strtoull(argv[++i], NULL, 10);
            have_expect = 1;
        }
    sha_init(&V.sha);
    if ((V.cf = open_cert(argv[4])) == NULL) { fprintf(stderr, "cannot open %s\n", argv[4]); return 2; }
    if (!read_header()) return 2;
    if (V.n != n || V.res != res || V.mod != mod)
    {
        fprintf(stderr, "certificate header (n=%d %d/%d) does not match the arguments\n", V.n, V.res, V.mod);
        return 2;
    }
    snprintf(nbuf, sizeof(nbuf), "%d", n);
    snprintf(rmbuf, sizeof(rmbuf), "%d/%d", res, mod);
    gargv[0] = "gentreeg"; gargv[1] = "-q"; gargv[2] = nbuf; gargv[3] = rmbuf; gargv[4] = NULL;
    gentreeg_main(4, gargv);
    return finish(argv[4], have_expect, expect);
}

#else

static unsigned char inbuf[1 << 20];
static size_t inlen, inpos;

static int
next_char(void)
{
    if (inpos == inlen)
    {
        inlen = fread(inbuf, 1, sizeof(inbuf), stdin);
        inpos = 0;
        if (inlen == 0) return EOF;
    }
    return inbuf[inpos++];
}

/* Reads one line of integers; returns how many were read, -1 at EOF. */
static int
read_line(long *a, int maxa)
{
    int ch, k = 0, innum = 0, any = 0;
    long x = 0;

    for (;;)
    {
        ch = next_char();
        if (ch == EOF)
        {
            if (innum && k < maxa) a[k++] = x;
            return any ? k : -1;
        }
        any = 1;
        if (ch >= '0' && ch <= '9')
        {
            x = (innum ? x * 10 : 0) + (ch - '0');
            innum = 1;
        }
        else
        {
            if (innum) { if (k < maxa) a[k++] = x; else k = maxa + 1; }
            innum = 0;
            if (ch == '\n') return k;
        }
    }
}

int
main(int argc, char **argv)
{
    unsigned long long expect = 0;
    int have_expect = 0, i, k;
    long par[MAXV + 2];

    if (argc < 2)
    {
        fprintf(stderr, "usage: gentreeg -p -q N RES/MOD | egverify CERTFILE [--expect-count C]\n");
        return 2;
    }
    for (i = 2; i < argc; ++i)
        if (!strcmp(argv[i], "--expect-count") && i + 1 < argc)
        {
            expect = strtoull(argv[++i], NULL, 10);
            have_expect = 1;
        }
    sha_init(&V.sha);
    if ((V.cf = open_cert(argv[1])) == NULL) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    if (!read_header()) return 2;

    for (;;)
    {
        k = read_line(par + 1, V.n + 1);
        if (k < 0) break;
        if (k == 0) continue;                     /* blank line */
        if (k != V.n) { fprintf(stderr, "tree line with %d entries, expected %d\n", k, V.n); return 2; }
        check_tree(par, V.n);
    }
    return finish(argv[1], have_expect, expect);
}

#endif
