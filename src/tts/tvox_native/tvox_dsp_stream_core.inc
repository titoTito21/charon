/* Generated from Titan Voice's tvox_dsp.c without the eSpeak NG-derived 'clear' timbre; MIT-0. */
/*
 * tvox_dsp - the per-sample loops of Titan Voice, for TVox.
 *
 * klatt_frame() is the body of klatt.synthesize_iter() for one non-silent
 * frame, line for line and in the same order of operations as the Python,
 * so the samples are the same.  The random numbers are drawn by Python from
 * the same random.Random(seed) and handed over, in the order the Python
 * loop would draw them.  Python keeps everything that is done once a frame;
 * this is only the 110 samples in between, which in pure Python were half
 * of the Klatt engine's time.
 *
 * Build: build.bat (MSVC x64) -> ..\tvox_dsp.dll; build.sh (gcc) -> ..\tvox_dsp.so
 */
#include <math.h>
#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE__)
#include <xmmintrin.h>
#define TVOX_FTZ 1
#endif

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LF_N 1024

/* state[] layout - kept in step with klatt.py's _NATIVE_STATE */
enum {
    S_R1A, S_R1B, S_R2A, S_R2B, S_R3A, S_R3B, S_R4A, S_R4B, S_R5A, S_R5B, S_R6A, S_R6B,
    S_NPA, S_NPB, S_NZX1, S_NZX2,
    S_P2A, S_P2B, S_P3A, S_P3B, S_P4A, S_P4B, S_P5A, S_P5B, S_P6A, S_P6B,
    S_TILT_Y, S_SHELF_LP, S_NLP, S_RAD_PREV, S_DC_X, S_DC_Y,
    S_POS, S_T0, S_TE, S_SCALE, S_PER_AMP, S_ODD,
    S_SHELVES /* 4 per source shelf: x1 x2 y1 y2 */
};

static void res(double f, double bw, double sr, double *a, double *b, double *c)
{
    double r = exp(-M_PI * bw / sr);
    *c = -r * r;
    *b = 2.0 * r * cos(2.0 * M_PI * f / sr);
    *a = 1.0 - *b - *c;
}

static void antires(double f, double bw, double sr, double *a, double *b, double *c)
{
    double ra, rb, rc;
    res(f, bw, sr, &ra, &rb, &rc);
    *a = 1.0 / ra;
    *b = -rb / ra;
    *c = -rc / ra;
}

/*
 * fp[]: f0, frd, creak, av0, av1, ah0, ah1, af0, af1, t_sec, flutter, jitter, shimmer, tilt,
 *       shelf_g, shelf_k, nyq, sr, db1_open, cascade_f6, noise_lp, par_on,
 *       pg2, pb2, pc2, pg3, pb3, pc3, pg4, pb4, pc4, pg5, pb5, pc5, pg6, pb6, pc6,
 *       g2, g3, g4, g5, g6, gb
 * tracks: pv[16], cur[16], nx[16]
 * shelves: nshelf x (e0 e1 e2 q1 q2)
 * table/te: lf_table(frd) of THIS frame; *cur_table: the table in use (from an earlier period)
 * rnd[rnd_len] from *rnd_pos on; returns the samples written (nper), or -1 when out of randoms
 */
/*
 * klatt_render - klatt.synthesize_iter() for a WHOLE utterance in one call: everything the Python does
 * once a frame (gains, the parallel branch's resonators, the glottal table of the frame's Rd) is done
 * here too, so a sentence costs one call instead of one call per 5 ms frame.  The random numbers come
 * from xorshift64* (klatt.XorShift is the same generator in Python, so the Python loop gives the same
 * samples).
 *
 * frames: nframes x KR_K doubles (KR_* below); g: KG_* globals; tables: ntab LF tables of LF_N doubles
 * for Rd 0.30, 0.35 ... (the frame's Rd rounded to 0.05, half to even as Python's round), tes[ntab] their
 * open-phase ends; shelves: nshelf x (e0 e1 e2 q1 q2).  The first `skip` frames must be silent: they
 * only advance the glottis and the noise, and are not written.  Returns the samples written.
 */
enum { KR_F0, KR_AV, KR_AH, KR_AF, KR_A2, KR_A3, KR_A4, KR_A5, KR_A6, KR_AB, KR_TRACK,
       KR_RD = KR_TRACK + 16, KR_CREAK, KR_K };
enum { KG_TILT, KG_FLUTTER, KG_JITTER, KG_SHIMMER, KG_SHELF_G, KG_SHELF_K, KG_SR, KG_DB1_OPEN, KG_F6,
       KG_NOISE_LP, KG_K_VOICE, KG_K_ASP, KG_K_FRIC, KG_FRAME_MS, KG_RD, KG_NPER, KG_SUB,
       KG_HARM, KG_HWSCALE, KG_HWBASE, KG_HTOP, KG_HSCALE, KG_HSLOPE, KG_SRC, KG_OQ, KG_K };

#define H_MAX 160


static double xs_next(unsigned long long *s)
{
    unsigned long long x = *s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return (double)((x * 0x2545F4914F6CDD1DULL) >> 11) * (1.0 / 9007199254740992.0);
}

static double db2lin(double db, double k)
{
    return db <= 0.0 ? 0.0 : k * pow(10.0, db / 20.0);
}

static void res_peak(double f, double bw, double sr, double *g, double *b, double *c)
{
    double a, th, re, im;
    res(f, bw, sr, &a, b, c);
    th = 2.0 * M_PI * f / sr;
    re = 1.0 - *b * cos(th) - *c * cos(2 * th);
    im = *b * sin(th) + *c * sin(2 * th);
    *g = hypot(re, im);
}

static int lf_index(double rd, int ntab)
{
    int i;
    if (rd > 2.7) rd = 2.7;
    if (rd < 0.3) rd = 0.3;
    i = (int)nearbyint(rd * 20.0) - 6;
    if (i < 0) i = 0;
    if (i >= ntab) i = ntab - 1;
    return i;
}

/* one sample of the harmonic voicing: rotate every oscillator, step its amplitude, sum - free to be
   reordered and vectorised (the LF path above keeps /fp:precise, so C and Python agree there) */
#ifdef _MSC_VER
#pragma float_control(precise, off, push)
#endif
static float harm_sample(int n, float *ha, const float *hd, float *hc, float *hs, const float *rcs,
                         const float *rsn)
{
    /* arrays from index 1; four partial sums, so the loop is not one long dependency chain */
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    float *a = ha + 1, *c = hc + 1, *s = hs + 1;
    const float *d = hd + 1, *rc = rcs + 1, *rs = rsn + 1;
    int k = 0;
    for (; k + 4 <= n; k += 4) {
        float c0 = c[k] * rc[k] - s[k] * rs[k], n0 = s[k] * rc[k] + c[k] * rs[k];
        float c1 = c[k + 1] * rc[k + 1] - s[k + 1] * rs[k + 1], n1 = s[k + 1] * rc[k + 1] + c[k + 1] * rs[k + 1];
        float c2 = c[k + 2] * rc[k + 2] - s[k + 2] * rs[k + 2], n2 = s[k + 2] * rc[k + 2] + c[k + 2] * rs[k + 2];
        float c3 = c[k + 3] * rc[k + 3] - s[k + 3] * rs[k + 3], n3 = s[k + 3] * rc[k + 3] + c[k + 3] * rs[k + 3];
        c[k] = c0; s[k] = n0; c[k + 1] = c1; s[k + 1] = n1;
        c[k + 2] = c2; s[k + 2] = n2; c[k + 3] = c3; s[k + 3] = n3;
        a[k] += d[k]; a[k + 1] += d[k + 1]; a[k + 2] += d[k + 2]; a[k + 3] += d[k + 3];
        s0 += a[k] * n0; s1 += a[k + 1] * n1; s2 += a[k + 2] * n2; s3 += a[k + 3] * n3;
    }
    for (; k < n; k++) {
        float cc = c[k] * rc[k] - s[k] * rs[k], nn = s[k] * rc[k] + c[k] * rs[k];
        c[k] = cc; s[k] = nn;
        a[k] += d[k];
        s0 += a[k] * nn;
    }
    return (s0 + s1) + (s2 + s3);
}
#ifdef _MSC_VER
#pragma float_control(pop)
#endif

typedef struct { int initialized,frame; unsigned long long rs; const double *tab;
double r1a,r1b,r2a,r2b,r3a,r3b,r4a,r4b,r5a,r5b,r6a,r6b,npa,npb,nzx1,nzx2,p2a,p2b,p3a,p3b,p4a,p4b,p5a,p5b,p6a,p6b,tilt_y,shelf_lp,nlp,rad_prev,dc_x,dc_y,T0,pos,Te,scale,per_amp,t_sec,av0,ah0,af0,prev_av,prev_ah,prev_af;
int odd,have_prev,have_prevp,hk,hloop,resync,hcount;
double sst[64];
double pv[16];
double p[16];
float ha[H_MAX + 1];
float hd[H_MAX + 1];
float hc[H_MAX + 1];
float hs[H_MAX + 1];
float rcs[H_MAX + 1];
float rsn[H_MAX + 1];
} RenderState;
static int render_frame(RenderState *ctx, int nframes, const double *frames, const double *g,
                        const double *tables, const double *tes, int ntab,
                        const double *shelves, int nshelf, int skip,
                        unsigned long long seed, double *out)
{
    double tilt = g[KG_TILT], flutter = g[KG_FLUTTER], jitter = g[KG_JITTER], shimmer = g[KG_SHIMMER];
    double shelf_g = g[KG_SHELF_G], shelf_k = g[KG_SHELF_K], sr = g[KG_SR], db1_open = g[KG_DB1_OPEN];
    int cascade_f6 = g[KG_F6] != 0.0;
    double noise_lp = g[KG_NOISE_LP], k_voice = g[KG_K_VOICE], k_asp = g[KG_K_ASP], k_fric = g[KG_K_FRIC];
    double frame_ms = g[KG_FRAME_MS];
    int nper = (int)g[KG_NPER], sub = (int)g[KG_SUB];
    double nyq = 0.45 * sr, inv = 1.0 / nper;
    unsigned long long rs = seed ? seed : 0x9E3779B97F4A7C15ULL;

    double r1a = 0, r1b = 0, r2a = 0, r2b = 0, r3a = 0, r3b = 0, r4a = 0, r4b = 0, r5a = 0, r5b = 0;
    double r6a = 0, r6b = 0, npa = 0, npb = 0, nzx1 = 0, nzx2 = 0;
    double p2a = 0, p2b = 0, p3a = 0, p3b = 0, p4a = 0, p4b = 0, p5a = 0, p5b = 0, p6a = 0, p6b = 0;
    double tilt_y = 0, shelf_lp = 0, nlp = 0, rad_prev = 0, dc_x = 0, dc_y = 0;
    double sst[64] = {0};
    int ti = lf_index(g[KG_RD], ntab);
    const double *tab = tables + (size_t)ti * LF_N;
    double T0 = sr / 100.0, pos = 0.0, Te = tes[ti] * T0, scale = LF_N / T0, per_amp = 1.0, t_sec = 0.0;
    int odd = 0, have_prev = 0, have_prevp = 0, w = 0, fn, i, k;
    double av0 = 0, ah0 = 0, af0 = 0, prev_av = 0, prev_ah = 0, prev_af = 0;
    double pv[16], p[16];
    int harm = 0, hk = 0, hloop = 0;   /* r385: the 'clear' timbre is not part of Charon */
    /* KG_SRC 1: the 'eloquence' timbre's glottal source (openevv, src/klatt/klatt_synth.c glottal_pulse,
       MIT licence; the published Klatt 1980 waveform IBM's Eloquence uses): a closed phase, then an open
       phase of KG_OQ of the period shaped frac * (1/3 - frac), here integrated to a flow (our output
       differentiates it, as the radiation) - so the period ends in an abrupt step at closure, the
       sharp excitation that makes Eloquence crisp and buzzy */
    int klg = g[KG_SRC] == 1.0;
    double oq = g[KG_OQ] > 0.05 ? (g[KG_OQ] < 0.95 ? g[KG_OQ] : 0.95) : 0.5;
    /* the harmonics as rotating oscillators (float, so the loop vectorises): amplitude and its step per
       sample, cos/sin of the phase, and the rotation per sample; re-aligned at every glottal period */
    float ha[H_MAX + 1] = {0}, hd[H_MAX + 1] = {0}, hc[H_MAX + 1] = {0}, hs[H_MAX + 1] = {0};
    float rcs[H_MAX + 1] = {0}, rsn[H_MAX + 1] = {0};
    int resync = 1, hcount = 0;
    (void)hk; (void)hcount;

if(ctx->initialized) {
r1a=ctx->r1a;
r1b=ctx->r1b;
r2a=ctx->r2a;
r2b=ctx->r2b;
r3a=ctx->r3a;
r3b=ctx->r3b;
r4a=ctx->r4a;
r4b=ctx->r4b;
r5a=ctx->r5a;
r5b=ctx->r5b;
r6a=ctx->r6a;
r6b=ctx->r6b;
npa=ctx->npa;
npb=ctx->npb;
nzx1=ctx->nzx1;
nzx2=ctx->nzx2;
p2a=ctx->p2a;
p2b=ctx->p2b;
p3a=ctx->p3a;
p3b=ctx->p3b;
p4a=ctx->p4a;
p4b=ctx->p4b;
p5a=ctx->p5a;
p5b=ctx->p5b;
p6a=ctx->p6a;
p6b=ctx->p6b;
tilt_y=ctx->tilt_y;
shelf_lp=ctx->shelf_lp;
nlp=ctx->nlp;
rad_prev=ctx->rad_prev;
dc_x=ctx->dc_x;
dc_y=ctx->dc_y;
T0=ctx->T0;
pos=ctx->pos;
Te=ctx->Te;
scale=ctx->scale;
per_amp=ctx->per_amp;
t_sec=ctx->t_sec;
av0=ctx->av0;
ah0=ctx->ah0;
af0=ctx->af0;
prev_av=ctx->prev_av;
prev_ah=ctx->prev_ah;
prev_af=ctx->prev_af;
odd=ctx->odd;
have_prev=ctx->have_prev;
have_prevp=ctx->have_prevp;
hk=ctx->hk;
hloop=ctx->hloop;
resync=ctx->resync;
hcount=ctx->hcount;
rs=ctx->rs;
tab=ctx->tab;
for(k=0;k<64;k++) sst[k]=ctx->sst[k];
for(k=0;k<16;k++) pv[k]=ctx->pv[k];
for(k=0;k<16;k++) p[k]=ctx->p[k];
for(k=0;k<H_MAX + 1;k++) ha[k]=ctx->ha[k];
for(k=0;k<H_MAX + 1;k++) hd[k]=ctx->hd[k];
for(k=0;k<H_MAX + 1;k++) hc[k]=ctx->hc[k];
for(k=0;k<H_MAX + 1;k++) hs[k]=ctx->hs[k];
for(k=0;k<H_MAX + 1;k++) rcs[k]=ctx->rcs[k];
for(k=0;k<H_MAX + 1;k++) rsn[k]=ctx->rsn[k];
}
    if (nshelf > 16) return -2;
#ifdef TVOX_FTZ
    /* decaying amplitudes and filter states turn subnormal, which costs a hundredfold per operation:
       flush them to zero while rendering, and put the caller's mode back afterwards */
    unsigned int ftz_mode = _MM_GET_FLUSH_ZERO_MODE();
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
    for (fn = ctx->frame; fn < nframes && fn < ctx->frame + 1; fn++) {
        const double *fr = frames + (size_t)fn * KR_K;
        const double *cur = fr + KR_TRACK;
        const double *nx = fn + 1 < nframes ? frames + (size_t)(fn + 1) * KR_K + KR_TRACK : cur;
        double f0 = fr[KR_F0], frd = fr[KR_RD], creak = fr[KR_CREAK];
        int fti = lf_index(frd, ntab);
        double av1, ah1, af1;
        int par_on;
        double pg2 = 0, pb2 = 0, pc2 = 0, pg3 = 0, pb3 = 0, pc3 = 0, pg4 = 0, pb4 = 0, pc4 = 0;
        double pg5 = 0, pb5 = 0, pc5 = 0, pg6 = 0, pb6 = 0, pc6 = 0;
        double g2 = 0, g3 = 0, g4 = 0, g5 = 0, g6 = 0, gb = 0;
        double a1c = 0, b1c = 0, c1c = 0, a1o = 0, b1o = 0, c1o = 0, a2 = 0, b2 = 0, c2 = 0;
        double a3 = 0, b3 = 0, c3 = 0, a4 = 0, b4 = 0, c4 = 0, a5 = 0, b5 = 0, c5 = 0;
        double a6 = 1.0, b6 = 0, c6 = 0, anp = 0, bnp = 0, cnp = 0, anz = 0, bnz = 0, cnz = 0;
        int use6 = 0;

        if (fn < skip) {
            for (k = 0; k < 16; k++) pv[k] = cur[k];
            have_prevp = 1;
            prev_av = prev_ah = prev_af = 0.0;
            have_prev = 1;
            for (i = 0; i < nper; i++) {
                pos += 1.0;
                if (pos >= T0) {
                    double fl, jit, f;
                    pos -= T0;
                    fl = flutter * (sin(2 * M_PI * 12.7 * t_sec) + sin(2 * M_PI * 7.1 * t_sec)
                                    + sin(2 * M_PI * 4.7 * t_sec)) / 3.0;
                    jit = jitter + 0.10 * creak;
                    f = f0 * (1.0 + 0.04 * fl + jit * (xs_next(&rs) - 0.5));
                    if (f < 40.0) f = 40.0;
                    odd = !odd;
                    if (creak > 0 && odd) f /= 1.0 + 0.25 * creak;
                    T0 = sr / f;
                    tab = tables + (size_t)fti * LF_N;
                    Te = tes[fti] * T0;
                    scale = LF_N / T0;
                    per_amp = 1.0 + (shimmer + 0.5 * creak) * (xs_next(&rs) - 0.5);
                    if (creak > 0 && odd) per_amp *= 1.0 - 0.35 * creak;
                }
                nlp = (1.0 - noise_lp) * (xs_next(&rs) - 0.5) + noise_lp * nlp;
            }
            t_sec += frame_ms / 1000.0;
            continue;
        }
        if (!have_prevp)
            for (k = 0; k < 16; k++) pv[k] = cur[k];
        av1 = db2lin(fr[KR_AV], k_voice);
        ah1 = db2lin(fr[KR_AH], k_asp);
        af1 = db2lin(fr[KR_AF], k_fric);
        if (!have_prev) {
            prev_av = av1; prev_ah = ah1; prev_af = af1;
            have_prev = 1;
        }
        av0 = prev_av; ah0 = prev_ah; af0 = prev_af;
        prev_av = av1; prev_ah = ah1; prev_af = af1;
        par_on = af0 > 0 || af1 > 0;
        if (par_on) {
            res_peak(fmin(cur[2], nyq), fmax(cur[3], 120), sr, &pg2, &pb2, &pc2);
            res_peak(fmin(cur[4], nyq), fmax(cur[5], 150), sr, &pg3, &pb3, &pc3);
            res_peak(fmin(cur[6], nyq), fmax(cur[7], 200), sr, &pg4, &pb4, &pc4);
            res_peak(fmin(cur[8], nyq), fmax(cur[9], 250), sr, &pg5, &pb5, &pc5);
            res_peak(fmin(cur[10], nyq), fmax(cur[11], 300), sr, &pg6, &pb6, &pc6);
            g2 = db2lin(fr[KR_A2], 1.0); g3 = db2lin(fr[KR_A3], 1.0); g4 = db2lin(fr[KR_A4], 1.0);
            g5 = db2lin(fr[KR_A5], 1.0); g6 = db2lin(fr[KR_A6], 1.0); gb = db2lin(fr[KR_AB], 1.0);
        } else {
            p2a = p2b = p3a = p3b = p4a = p4b = p5a = p5b = p6a = p6b = 0.0;
        }

        for (i = 0; i < nper; i++) {
            double wt, av, ah, af, src, vs, noise, mod, x, y, z, s, r;
            int open_ph, idx;
            if (i % sub == 0) {
                double ws = (i + 0.5 * sub) * inv - 0.5;
                if (ws < 0) {
                    for (k = 0; k < 16; k++)
                        p[k] = cur[k] + (pv[k] - cur[k]) * -ws;
                } else {
                    for (k = 0; k < 16; k++)
                        p[k] = cur[k] + (nx[k] - cur[k]) * ws;
                }
                res(p[0] < nyq ? p[0] : nyq, p[1], sr, &a1c, &b1c, &c1c);
                res(p[0] < nyq ? p[0] : nyq, p[1] + db1_open * frd, sr, &a1o, &b1o, &c1o);
                res(p[2] < nyq ? p[2] : nyq, p[3], sr, &a2, &b2, &c2);
                res(p[4] < nyq ? p[4] : nyq, p[5], sr, &a3, &b3, &c3);
                res(p[6] < nyq ? p[6] : nyq, p[7], sr, &a4, &b4, &c4);
                res(p[8] < nyq ? p[8] : nyq, p[9], sr, &a5, &b5, &c5);
                use6 = cascade_f6 && p[10] < nyq;
                if (use6)
                    res(p[10], p[11] > 400 ? p[11] : 400, sr, &a6, &b6, &c6);
                else {
                    a6 = 1.0; b6 = 0.0; c6 = 0.0;
                }
                res(p[12], p[13], sr, &anp, &bnp, &cnp);
                antires(p[14], p[15], sr, &anz, &bnz, &cnz);
            }
            wt = i * inv;
            av = av0 + (av1 - av0) * wt;
            ah = ah0 + (ah1 - ah0) * wt;
            af = af0 + (af1 - af0) * wt;

            pos += 1.0;
            if (pos >= T0) {
                double fl, jit, f;
                pos -= T0;
                fl = flutter * (sin(2 * M_PI * 12.7 * t_sec) + sin(2 * M_PI * 7.1 * t_sec)
                                + sin(2 * M_PI * 4.7 * t_sec)) / 3.0;
                jit = jitter + 0.10 * creak;
                f = f0 * (1.0 + 0.04 * fl + jit * (xs_next(&rs) - 0.5));
                if (f < 40.0) f = 40.0;
                odd = !odd;
                if (creak > 0 && odd) f /= 1.0 + 0.25 * creak;
                T0 = sr / f;
                tab = tables + (size_t)fti * LF_N;
                Te = tes[fti] * T0;
                scale = LF_N / T0;
                per_amp = 1.0 + (shimmer + 0.5 * creak) * (xs_next(&rs) - 0.5);
                if (creak > 0 && odd) per_amp *= 1.0 - 0.35 * creak;
                resync = 1;
            }
            if (klg) {
                double start = (1.0 - oq) * T0, x;
                open_ph = pos >= start;
                if (open_ph) {
                    x = (pos - start) / (oq * T0);
                    src = (x * x / 6.0 - x * x * x / 3.0) * (1.0 / 0.1728) * av * per_amp;
                } else
                    src = 0.0;
            } else {
                open_ph = pos < Te;
                idx = (int)(pos * scale);
                if (idx > LF_N - 1) idx = LF_N - 1;
                src = tab[idx] * av * per_amp;
            }
            tilt_y = src + tilt * (tilt_y - src);
            if (shelf_g != 0.0) {
                shelf_lp = tilt_y + shelf_k * (shelf_lp - tilt_y);
                tilt_y += shelf_g * (tilt_y - shelf_lp);
            }
            vs = tilt_y;
            for (k = 0; k < nshelf; k++) {
                const double *e = shelves + 5 * k;
                double *st = sst + 4 * k;
                double o = e[0] * vs + e[1] * st[0] + e[2] * st[1] - e[3] * st[2] - e[4] * st[3];
                st[1] = st[0]; st[0] = vs; st[3] = st[2]; st[2] = o;
                vs = o;
            }
            nlp = (1.0 - noise_lp) * (xs_next(&rs) - 0.5) + noise_lp * nlp;
            noise = nlp;
            mod = (open_ph || av == 0.0) ? 1.0 : 0.4;
            x = (harm ? 0.0 : vs) + ah * noise * mod;

            y = anp * x + bnp * npa + cnp * npb;
            npb = npa; npa = y;
            z = anz * y + bnz * nzx1 + cnz * nzx2;
            nzx2 = nzx1; nzx1 = y;
            if (use6) {
                z = a6 * z + b6 * r6a + c6 * r6b;
                r6b = r6a; r6a = z;
            }
            y = a5 * z + b5 * r5a + c5 * r5b;
            r5b = r5a; r5a = y;
            y = a4 * y + b4 * r4a + c4 * r4b;
            r4b = r4a; r4a = y;
            y = a3 * y + b3 * r3a + c3 * r3b;
            r3b = r3a; r3a = y;
            y = a2 * y + b2 * r2a + c2 * r2b;
            r2b = r2a; r2a = y;
            if (open_ph)
                y = a1o * y + b1o * r1a + c1o * r1b;
            else
                y = a1c * y + b1c * r1a + c1c * r1b;
            r1b = r1a; r1a = y;
            s = y;

            if (par_on) {
                double n = af * noise * mod, y2, y3, y4, y5, y6;
                y2 = pg2 * n + pb2 * p2a + pc2 * p2b; p2b = p2a; p2a = y2;
                y3 = pg3 * n + pb3 * p3a + pc3 * p3b; p3b = p3a; p3a = y3;
                y4 = pg4 * n + pb4 * p4a + pc4 * p4b; p4b = p4a; p4a = y4;
                y5 = pg5 * n + pb5 * p5a + pc5 * p5b; p5b = p5a; p5a = y5;
                y6 = pg6 * n + pb6 * p6a + pc6 * p6b; p6b = p6a; p6a = y6;
                s += g2 * y2 - g3 * y3 + g4 * y4 - g5 * y5 + g6 * y6 + gb * n;
            }

            r = s - rad_prev;
            rad_prev = s;
            if (harm) {
                float vsum = 0.0f;
                if (resync) {
                    /* a new period (or the first sound): the phases from pos, the rotation from T0 */
                    double th = 2.0 * M_PI * pos / T0, w = 2.0 * M_PI / T0;
                    double c1 = cos(th), s1 = sin(th), cr = cos(w), sr1 = sin(w);
                    double ckk = c1, skk = s1, crk = cr, srk = sr1, t;
                    for (k = 1; k <= H_MAX; k++) {
                        hc[k] = (float)ckk; hs[k] = (float)skk;
                        rcs[k] = (float)crk; rsn[k] = (float)srk;
                        t = ckk * c1 - skk * s1; skk = skk * c1 + ckk * s1; ckk = t;
                        t = crk * cr - srk * sr1; srk = srk * cr + crk * sr1; crk = t;
                    }
                    resync = 0;
                    for (k = 1; k <= hloop; k++) {
                        ha[k] += hd[k];
                        vsum += ha[k] * hs[k];
                    }
                } else {
                    vsum = harm_sample(hloop, ha, hd, hc, hs, rcs, rsn);
                }
                r += av * per_amp * vsum;
            }
            dc_y = r - dc_x + 0.995 * dc_y;
            dc_x = r;
            out[w++] = dc_y;
        }
        for (k = 0; k < 16; k++) pv[k] = cur[k];
        have_prevp = 1;
        t_sec += frame_ms / 1000.0;
    }
#ifdef TVOX_FTZ
    _MM_SET_FLUSH_ZERO_MODE(ftz_mode);
#endif
ctx->initialized=1;ctx->frame=fn;
ctx->r1a=r1a;
ctx->r1b=r1b;
ctx->r2a=r2a;
ctx->r2b=r2b;
ctx->r3a=r3a;
ctx->r3b=r3b;
ctx->r4a=r4a;
ctx->r4b=r4b;
ctx->r5a=r5a;
ctx->r5b=r5b;
ctx->r6a=r6a;
ctx->r6b=r6b;
ctx->npa=npa;
ctx->npb=npb;
ctx->nzx1=nzx1;
ctx->nzx2=nzx2;
ctx->p2a=p2a;
ctx->p2b=p2b;
ctx->p3a=p3a;
ctx->p3b=p3b;
ctx->p4a=p4a;
ctx->p4b=p4b;
ctx->p5a=p5a;
ctx->p5b=p5b;
ctx->p6a=p6a;
ctx->p6b=p6b;
ctx->tilt_y=tilt_y;
ctx->shelf_lp=shelf_lp;
ctx->nlp=nlp;
ctx->rad_prev=rad_prev;
ctx->dc_x=dc_x;
ctx->dc_y=dc_y;
ctx->T0=T0;
ctx->pos=pos;
ctx->Te=Te;
ctx->scale=scale;
ctx->per_amp=per_amp;
ctx->t_sec=t_sec;
ctx->av0=av0;
ctx->ah0=ah0;
ctx->af0=af0;
ctx->prev_av=prev_av;
ctx->prev_ah=prev_ah;
ctx->prev_af=prev_af;
ctx->odd=odd;
ctx->have_prev=have_prev;
ctx->have_prevp=have_prevp;
ctx->hk=hk;
ctx->hloop=hloop;
ctx->resync=resync;
ctx->hcount=hcount;
ctx->rs=rs;
ctx->tab=tab;
for(k=0;k<64;k++) ctx->sst[k]=sst[k];
for(k=0;k<16;k++) ctx->pv[k]=pv[k];
for(k=0;k<16;k++) ctx->p[k]=p[k];
for(k=0;k<H_MAX + 1;k++) ctx->ha[k]=ha[k];
for(k=0;k<H_MAX + 1;k++) ctx->hd[k]=hd[k];
for(k=0;k<H_MAX + 1;k++) ctx->hc[k]=hc[k];
for(k=0;k<H_MAX + 1;k++) ctx->hs[k]=hs[k];
for(k=0;k<H_MAX + 1;k++) ctx->rcs[k]=rcs[k];
for(k=0;k<H_MAX + 1;k++) ctx->rsn[k]=rsn[k];
    return w;
}

