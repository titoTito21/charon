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
EXPORT int klatt_frame(double *state, const double *fp, const double *pv, const double *cur,
                       const double *nx, const double *shelves, int nshelf,
                       const double *table, double te, const double **cur_table,
                       const double *rnd, int rnd_len, int *rnd_pos,
                       int nper, int sub, double *out)
{
    double f0 = fp[0], frd = fp[1], creak = fp[2];
    double av0 = fp[3], av1 = fp[4], ah0 = fp[5], ah1 = fp[6], af0 = fp[7], af1 = fp[8];
    double t_sec = fp[9], flutter = fp[10], jitter = fp[11], shimmer = fp[12], tilt = fp[13];
    double shelf_g = fp[14], shelf_k = fp[15], nyq = fp[16], sr = fp[17], db1_open = fp[18];
    int cascade_f6 = fp[19] != 0.0;
    double noise_lp = fp[20];
    int par_on = fp[21] != 0.0;
    double pg2 = fp[22], pb2 = fp[23], pc2 = fp[24], pg3 = fp[25], pb3 = fp[26], pc3 = fp[27];
    double pg4 = fp[28], pb4 = fp[29], pc4 = fp[30], pg5 = fp[31], pb5 = fp[32], pc5 = fp[33];
    double pg6 = fp[34], pb6 = fp[35], pc6 = fp[36];
    double g2 = fp[37], g3 = fp[38], g4 = fp[39], g5 = fp[40], g6 = fp[41], gb = fp[42];

    double r1a = state[S_R1A], r1b = state[S_R1B], r2a = state[S_R2A], r2b = state[S_R2B];
    double r3a = state[S_R3A], r3b = state[S_R3B], r4a = state[S_R4A], r4b = state[S_R4B];
    double r5a = state[S_R5A], r5b = state[S_R5B], r6a = state[S_R6A], r6b = state[S_R6B];
    double npa = state[S_NPA], npb = state[S_NPB], nzx1 = state[S_NZX1], nzx2 = state[S_NZX2];
    double p2a = state[S_P2A], p2b = state[S_P2B], p3a = state[S_P3A], p3b = state[S_P3B];
    double p4a = state[S_P4A], p4b = state[S_P4B], p5a = state[S_P5A], p5b = state[S_P5B];
    double p6a = state[S_P6A], p6b = state[S_P6B];
    double tilt_y = state[S_TILT_Y], shelf_lp = state[S_SHELF_LP], nlp = state[S_NLP];
    double rad_prev = state[S_RAD_PREV], dc_x = state[S_DC_X], dc_y = state[S_DC_Y];
    double pos = state[S_POS], T0 = state[S_T0], Te = state[S_TE], scale = state[S_SCALE];
    double per_amp = state[S_PER_AMP];
    int odd = state[S_ODD] != 0.0;
    double *sst = state + S_SHELVES;
    const double *tab = *cur_table;
    int rp = *rnd_pos;

    double a1c = 0, b1c = 0, c1c = 0, a1o = 0, b1o = 0, c1o = 0, a2 = 0, b2 = 0, c2 = 0;
    double a3 = 0, b3 = 0, c3 = 0, a4 = 0, b4 = 0, c4 = 0, a5 = 0, b5 = 0, c5 = 0;
    double a6 = 1.0, b6 = 0, c6 = 0, anp = 0, bnp = 0, cnp = 0, anz = 0, bnz = 0, cnz = 0;
    int use6 = 0;
    double inv = 1.0 / nper;
    double p[16];
    int i, k;

    for (i = 0; i < nper; i++) {
        double w, av, ah, af, src, vs, noise, mod, x, y, z, s, r;
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
        w = i * inv;
        av = av0 + (av1 - av0) * w;
        ah = ah0 + (ah1 - ah0) * w;
        af = af0 + (af1 - af0) * w;

        pos += 1.0;
        if (pos >= T0) {
            double fl, jit, f;
            pos -= T0;
            fl = flutter * (sin(2 * M_PI * 12.7 * t_sec) + sin(2 * M_PI * 7.1 * t_sec)
                            + sin(2 * M_PI * 4.7 * t_sec)) / 3.0;
            jit = jitter + 0.10 * creak;
            if (rp >= rnd_len) return -1;
            f = f0 * (1.0 + 0.04 * fl + jit * (rnd[rp++] - 0.5));
            if (f < 40.0) f = 40.0;
            odd = !odd;
            if (creak > 0 && odd)
                f /= 1.0 + 0.25 * creak;
            T0 = sr / f;
            tab = table;
            Te = te * T0;
            scale = LF_N / T0;
            if (rp >= rnd_len) return -1;
            per_amp = 1.0 + (shimmer + 0.5 * creak) * (rnd[rp++] - 0.5);
            if (creak > 0 && odd)
                per_amp *= 1.0 - 0.35 * creak;
        }
        open_ph = pos < Te;
        idx = (int)(pos * scale);
        if (idx > LF_N - 1) idx = LF_N - 1;
        src = tab[idx] * av * per_amp;
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
        if (rp >= rnd_len) return -1;
        nlp = (1.0 - noise_lp) * (rnd[rp++] - 0.5) + noise_lp * nlp;
        noise = nlp;
        mod = (open_ph || av == 0.0) ? 1.0 : 0.4;
        x = vs + ah * noise * mod;

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
        dc_y = r - dc_x + 0.995 * dc_y;
        dc_x = r;
        out[i] = dc_y;
    }

    state[S_R1A] = r1a; state[S_R1B] = r1b; state[S_R2A] = r2a; state[S_R2B] = r2b;
    state[S_R3A] = r3a; state[S_R3B] = r3b; state[S_R4A] = r4a; state[S_R4B] = r4b;
    state[S_R5A] = r5a; state[S_R5B] = r5b; state[S_R6A] = r6a; state[S_R6B] = r6b;
    state[S_NPA] = npa; state[S_NPB] = npb; state[S_NZX1] = nzx1; state[S_NZX2] = nzx2;
    state[S_P2A] = p2a; state[S_P2B] = p2b; state[S_P3A] = p3a; state[S_P3B] = p3b;
    state[S_P4A] = p4a; state[S_P4B] = p4b; state[S_P5A] = p5a; state[S_P5B] = p5b;
    state[S_P6A] = p6a; state[S_P6B] = p6b;
    state[S_TILT_Y] = tilt_y; state[S_SHELF_LP] = shelf_lp; state[S_NLP] = nlp;
    state[S_RAD_PREV] = rad_prev; state[S_DC_X] = dc_x; state[S_DC_Y] = dc_y;
    state[S_POS] = pos; state[S_T0] = T0; state[S_TE] = Te; state[S_SCALE] = scale;
    state[S_PER_AMP] = per_amp; state[S_ODD] = odd ? 1.0 : 0.0;
    *cur_table = tab;
    *rnd_pos = rp;
    return nper;
}


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

/*
 * The 'clear' timbre: eSpeak NG's way of making the voicing (wavegen.c, PeaksToHarmspect and the
 * harmonic sum of Wavegen) - a sum of harmonics of F0 whose amplitudes are built from the formant peaks
 * with eSpeak's own peak shape, then squared.  Aspiration and frication still go through Klatt's cascade
 * and parallel branches.
 *
 * pk_shape2 and the algorithm are from eSpeak NG: Copyright (C) 2005 to 2013 Jonathan Duddington,
 * (C) 2015-2016 Reece H. Dunn; GNU General Public License version 3 or later (lib/native/COPYING).
 */
#define PEAKSHAPEW 256
static const unsigned char pk_shape2[PEAKSHAPEW+1] = {
	255, 254, 254, 254, 254, 254, 254, 254, 254, 254, 253, 253, 253, 253, 252, 252,
	252, 251, 251, 251, 250, 250, 249, 249, 248, 248, 247, 247, 246, 245, 245, 244,
	243, 243, 242, 241, 239, 237, 235, 233, 231, 229, 227, 225, 223, 221, 218, 216,
	213, 211, 208, 205, 203, 200, 197, 194, 191, 187, 184, 181, 178, 174, 171, 167,
	163, 160, 156, 152, 148, 144, 140, 136, 132, 127, 123, 119, 114, 110, 105, 100,
	 96,  94,  91,  88,  86,  83,  81,  78,  76,  74,  71,  69,  66,  64,  62,  60,
	 57,  55,  53,  51,  49,  47,  44,  42,  40,  38,  36,  34,  32,  30,  29,  27,
	 25,  23,  21,  19,  18,  16,  14,  12,  11,   9,   7,   6,   4,   3,   1,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0
};
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

EXPORT int klatt_render(int nframes, const double *frames, const double *g,
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
    int harm = g[KG_HARM] != 0.0, hk = 0, hloop = 0;
    double hwscale = g[KG_HWSCALE], hwbase = g[KG_HWBASE], htop = g[KG_HTOP], hscale = g[KG_HSCALE];
    double hslope = g[KG_HSLOPE];
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

    if (nshelf > 16) return -2;
#ifdef TVOX_FTZ
    /* decaying amplitudes and filter states turn subnormal, which costs a hundredfold per operation:
       flush them to zero while rendering, and put the caller's mode back afterwards */
    unsigned int ftz_mode = _MM_GET_FLUSH_ZERO_MODE();
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
    for (fn = 0; fn < nframes; fn++) {
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
                if (harm && hcount++ % 2 == 0) {
                    /* eSpeak NG's PeaksToHarmspect (wavegen.c), every 2 ms: each harmonic's amplitude is
                       the sum of the formant peaks' shapes (pk_shape2) at its frequency, times the peaks'
                       heights, and then SQUARED - so between the formants there is nothing at all, which
                       is what makes eSpeak clear.  A peak's height is the square root of the cascade's
                       gain at the formant (so the squared peak is that gain), its half-width
                       HWBASE + HWSCALE x the formant's bandwidth; the bass ramp below 1 kHz, the first
                       harmonic x 10/8 and the sign of the harmonics above 890 Hz are eSpeak's. */
                    double fh = sr / T0, top = htop < 0.475 * sr ? htop : 0.475 * sr;
                    double rb[7], rc[7], ra2[7], pf[5], ph[5], pw[5], ln_amp = hscale * fh / 100.0;
                    int nr = 0, K = (int)(top / fh), steps = 2 * sub, q;
                    if (K > H_MAX) K = H_MAX;
                    if (K < 1) K = 1;
                    rb[nr] = b1c; rc[nr] = c1c; ra2[nr++] = a1c * a1c;
                    rb[nr] = b2; rc[nr] = c2; ra2[nr++] = a2 * a2;
                    rb[nr] = b3; rc[nr] = c3; ra2[nr++] = a3 * a3;
                    rb[nr] = b4; rc[nr] = c4; ra2[nr++] = a4 * a4;
                    rb[nr] = b5; rc[nr] = c5; ra2[nr++] = a5 * a5;
                    rb[nr] = bnp; rc[nr] = cnp; ra2[nr++] = anp * anp;
                    if (use6) { rb[nr] = b6; rc[nr] = c6; ra2[nr++] = a6 * a6; }
                    for (q = 0; q < 5; q++) {
                        double f = p[2 * q] < nyq ? p[2 * q] : nyq, w = 2.0 * M_PI * f / sr;
                        double ck = cos(w), sk = sin(w), c2k = cos(2 * w), s2k = sin(2 * w), m2 = 1.0, re, im;
                        int r_;
                        for (r_ = 0; r_ < nr; r_++) {
                            re = 1.0 - rb[r_] * ck - rc[r_] * c2k;
                            im = rb[r_] * sk + rc[r_] * s2k;
                            m2 *= ra2[r_] / (re * re + im * im + 1e-30);
                        }
                        re = anz + bnz * ck + cnz * c2k;
                        im = bnz * sk + cnz * s2k;
                        m2 *= re * re + im * im;
                        pf[q] = f;
                        ph[q] = sqrt(sqrt(m2));               /* squared below: the cascade's gain */
                        pw[q] = hwbase + hwscale * p[2 * q + 1];
                    }
                    {
                    /* cos/sin of each harmonic by rotation, for the shelves (eSpeak's tone_adjust) */
                    double w0 = 2.0 * M_PI * fh / sr, cw0 = cos(w0), sw0 = sin(w0), ck = cw0, sk = sw0, t_;
                    for (k = 1; k <= H_MAX; k++) {
                        double tgt = 0.0;
                        if (k <= K) {
                            double f = k * fh, x = 0.0;
                            for (q = 0; q < 5; q++) {
                                double d = fabs(f - pf[q]);
                                if (d < pw[q]) {
                                    int ix = (int)(256.0 * d / pw[q]);
                                    x += pk_shape2[ix > 256 ? 256 : ix] * (1.0 / 255.0) * ph[q];
                                }
                            }
                            if (f < 1000.0)
                                x += ph[0] * (10.0 / 255.0) * (1.0 - f / 1000.0);   /* eSpeak's bass */
                            /* the source's slope: eSpeak keeps it in its phoneme data, Titan Voice's
                               formants carry none */
                            tgt = x * x * ln_amp * pow(f / 500.0, -hslope);
                            if (nshelf) {                                     /* eSpeak's tone_adjust */
                                double c2k = 2.0 * ck * ck - 1.0, s2k = 2.0 * sk * ck, m2 = 1.0;
                                for (q = 0; q < nshelf; q++) {
                                    const double *e = shelves + 5 * q;
                                    double nr_ = e[0] + e[1] * ck + e[2] * c2k, ni = e[1] * sk + e[2] * s2k;
                                    double dr = 1.0 + e[3] * ck + e[4] * c2k, di = e[3] * sk + e[4] * s2k;
                                    m2 *= (nr_ * nr_ + ni * ni) / (dr * dr + di * di + 1e-30);
                                }
                                tgt *= sqrt(m2);
                            }
                            if (k == 1) tgt *= 10.0 / 8.0;                   /* option_harmonic1 */
                            if (f > 890.0) tgt = -tgt;                       /* h_switch_sign */
                        }
                        hd[k] = (float)((tgt - ha[k]) / steps);
                        t_ = ck * cw0 - sk * sw0;
                        sk = sk * cw0 + ck * sw0;
                        ck = t_;
                    }
                    }
                    hloop = K > hk ? K : hk;
                    hk = K;
                }
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
    return w;
}


/*
 * pcm16 - float samples (gain applied) through the stream's soft limiter to 16-bit PCM:
 * transparent below `knee`, never above 1 (engine._limit and stream_pcm in one pass).
 */
EXPORT void pcm16(int n, const double *x, double gain, double knee, short *out)
{
    int i;
    for (i = 0; i < n; i++) {
        double v = x[i] * gain, a = fabs(v);
        if (a > knee)
            v = copysign(knee + (1.0 - knee) * tanh((a - knee) / (1.0 - knee)), v);
        if (v >= 1.0) out[i] = 32767;
        else if (v <= -1.0) out[i] = -32767;
        else out[i] = (short)(v * 32767.0);
    }
}


/*
 * dpcm4_encode - the compact databases' audio coder (titan_voice/repack.py): 4-bit DPCM, closed loop, a
 * step per block of `block` samples stored as one byte k (step = table[k], 256 integers the file
 * carries, so the decoder uses exactly the encoder's steps).  Each sample is coded
 * against the DECODER's reconstruction, so the decoder - numpy, a cumulative sum (concat._dpcm4_decode) -
 * meets no drift and needs no clamping: a code that would leave 16 bits is pulled towards zero here.
 * codes: one signed code -8..7 per sample (packed into nibbles by the caller).  Returns 0.
 */
EXPORT int dpcm4_encode(int n, const short *x, int block, const int *table, signed char *codes,
                        unsigned char *steps)
{
    long recon = 0;
    int b, i, nb = (n + block - 1) / block;
    for (b = 0; b < nb; b++) {
        int a = b * block, e = a + block < n ? a + block : n, k;
        long peak = 0, prev = recon;
        double step;
        for (i = a; i < e; i++) {             /* the largest step the block takes, from the source */
            long d = labs((long)x[i] - prev);
            if (d > peak) peak = d;
            prev = x[i];
        }
        for (k = 0; k < 255 && table[k] * 7.0 < (double)peak; k++)
            ;
        steps[b] = (unsigned char)k;
        step = table[k];
        for (i = a; i < e; i++) {
            long q = (long)floor(((double)x[i] - recon) / step + 0.5), r;
            if (q > 7) q = 7;
            if (q < -8) q = -8;
            r = recon + (long)(q * step);
            while (r > 32767) { q--; r = recon + (long)(q * step); }
            while (r < -32768) { q++; r = recon + (long)(q * step); }
            codes[i] = (signed char)q;
            recon = r;
        }
    }
    return 0;
}


/* dpcm4_decode - the decoder of dpcm4_encode, straight to mu-law bytes through lut[pcm + 32768]
   (concat._dpcm4_decode does the same in numpy without the library).  Returns 0. */
EXPORT int dpcm4_decode(int n, int block, const int *table, const unsigned char *steps,
                        const unsigned char *packed, const unsigned char *lut, unsigned char *out)
{
    long recon = 0;
    int i;
    for (i = 0; i < n; i++) {
        int nib = (i & 1) ? packed[i >> 1] >> 4 : packed[i >> 1] & 15;
        recon += (long)(nib > 7 ? nib - 16 : nib) * table[steps[i / block]];
        out[i] = lut[recon + 32768];
    }
    return 0;
}


/*
 * param_render - parametric.py's generated _render(frames, seed, table), for the whole utterance in one
 * call: the LPC (order-pole) filter over mixed excitation, every frame's SUB_MS coefficients and gains
 * worked out beforehand (coef_table, numpy), the random numbers drawn by Python from random.Random(seed).
 * Per frame: f0, voiced (0/1), lk, amp0, amp1, nh, tl.  A: frames x steps x (order+1); G: frames x steps.
 * lf: lf_table(rd) of the frames (one rd for all).  Returns the samples written, or -1 out of randoms.
 */
#define GP_MAX 1200

static int glottal_pulse(int n, const double *lf, double *out)
{
    /* parametric.glottal_pulse: second difference of LF flow sampled to n, unit rms */
    double flow[GP_MAX], d1[GP_MAX], e = 0.0;
    int k;
    if (n < 1) return 0;
    if (n > GP_MAX) n = GP_MAX;
    for (k = 0; k < n; k++) {
        int idx = (int)((double)(k * LF_N) / (double)n);
        if (idx > LF_N - 1) idx = LF_N - 1;
        flow[k] = lf[idx];
    }
    for (k = 0; k < n; k++)
        d1[k] = flow[k] - flow[k == 0 ? n - 1 : k - 1];
    for (k = 0; k < n; k++)
        out[k] = d1[k] - d1[k == 0 ? n - 1 : k - 1];
    for (k = 0; k < n; k++)
        e += out[k] * out[k];
    e = sqrt(e / n);
    if (e == 0.0) e = 1.0;
    for (k = 0; k < n; k++)
        out[k] = out[k] / e;
    return n;
}

EXPORT int param_render(int nframes, int nper, int sub, int steps, int order,
                        const double *f0s, const double *voiceds, const double *lks,
                        const double *amp0s, const double *amp1s, const double *nhs, const double *tls,
                        const double *A, const double *G, const double *lf,
                        const double *rnd, int rnd_len,
                        double c1a, double c2a, double c3a, double sr, double jitter, int shaped,
                        double *out)
{
    double yh[64] = {0};               /* y1 .. y_order */
    double lp_p = 0.0, lp_n = 0.0, dc_x = 0.0, dc_y = 0.0, pos = 0.0, T0 = sr / 100.0;
    double ap1x = 0, ap1y = 0, ap2x = 0, ap2y = 0, ap3x = 0, ap3y = 0, lp_t = 0.0;
    double gp[GP_MAX];
    int gp_len = 0, gi = 0, rp = 0, w = 0, fn, i, k;
    const double *a = A;
    double g = 1.0;
    double inv = 1.0 / nper;
    if (order > 63) return -2;

    for (fn = 0; fn < nframes; fn++) {
        double f0 = f0s[fn], lk = lks[fn], amp0 = amp0s[fn], amp1 = amp1s[fn], nh = nhs[fn], tl = tls[fn];
        int voiced = voiceds[fn] != 0.0;
        for (i = 0; i < nper; i++) {
            double amp, pulse, ap, q, noise, x, y, acc, f;
            if (i % sub == 0) {
                int s = i / sub;
                if (s >= steps) s = steps - 1;
                a = A + ((size_t)fn * steps + s) * (order + 1);
                g = G[(size_t)fn * steps + s];
            }
            amp = g * (amp0 + (amp1 - amp0) * i * inv);
            pulse = 0.0;
            pos += 1.0;
            if (pos >= T0) {
                pos -= T0;
                if (rp >= rnd_len) return -1;
                f = f0 * (1.0 + jitter * (rnd[rp++] - 0.5));
                T0 = sr / (f > 40.0 ? f : 40.0);
                if (voiced && shaped) {
                    gp_len = glottal_pulse((int)T0, lf, gp);
                    gi = 0;
                } else if (voiced) {
                    pulse = sqrt(T0);
                }
            }
            if (shaped && voiced && gi < gp_len) {
                pulse = gp[gi];
                gi += 1;
            }
            ap = -c1a * pulse + ap1x + c1a * ap1y;
            ap1x = pulse; ap1y = ap;
            q = -c2a * ap + ap2x + c2a * ap2y;
            ap2x = ap; ap2y = q;
            ap = -c3a * q + ap3x + c3a * ap3y;
            ap3x = q; ap3y = ap;
            if (tl != 0.0) {
                lp_t = ap + tl * (lp_t - ap);
                ap = lp_t;
            }
            if (rp >= rnd_len) return -1;
            noise = (rnd[rp++] - 0.5) * 3.4641;
            if (voiced) {
                lp_p = ap + lk * (lp_p - ap);
                lp_n = noise + lk * (lp_n - noise);
                x = lp_p + nh * (noise - lp_n);
            } else {
                x = noise;
            }
            x *= amp;
            acc = a[1] * yh[1];
            for (k = 2; k <= order; k++)
                acc += a[k] * yh[k];
            y = x - acc;
            for (k = order; k > 1; k--)
                yh[k] = yh[k - 1];
            yh[1] = y;
            dc_y = y - dc_x + 0.995 * dc_y;
            dc_x = y;
            out[w++] = dc_y;
        }
    }
    return w;
}
