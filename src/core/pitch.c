/* Phase-vocoder pitch shifter with a duration-preserving resampler. */
#include "pitch.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PV_N      1024          /* window / FFT size */
#define PV_HOP    128           /* analysis hop: 8x overlap */
#define PV_BINS   (PV_N / 2 + 1)
#define PV_TWO_PI 6.28318530717958647692

/* --------------------------------------------------------------- fft */

static void fft(float *re, float *im, int n, int inverse)
{
    /* bit-reversal permutation */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = (inverse ? PV_TWO_PI : -PV_TWO_PI) / len;
        float wr = (float)cos(ang), wi = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = a + len / 2;
                float vr = re[b] * cr - im[b] * ci;
                float vi = re[b] * ci + im[b] * cr;
                float ur = re[a], ui = im[a];
                re[a] = ur + vr; im[a] = ui + vi;
                re[b] = ur - vr; im[b] = ui - vi;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
    if (inverse) {
        float s = 1.0f / (float)n;
        for (int i = 0; i < n; i++) { re[i] *= s; im[i] *= s; }
    }
}

/* ------------------------------------------------------------ resampler */

/* Catmull-Rom interpolation at a fractional source position. */
static float cubic_at(const float *x, int len, double pos)
{
    if (pos < 0.0) pos = 0.0;
    if (pos > (double)(len - 1)) pos = (double)(len - 1);
    int i = (int)pos;
    double f = pos - (double)i;
    float y0 = x[i > 0 ? i - 1 : 0];
    float y1 = x[i];
    float y2 = x[i + 1 < len ? i + 1 : len - 1];
    float y3 = x[i + 2 < len ? i + 2 : len - 1];
    double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
    double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    double c = -0.5 * y0 + 0.5 * y2;
    return (float)(((a * f + b) * f + c) * f + y1);
}

static double wrap_pi(double p)
{
    const double pi = PV_TWO_PI / 2.0;
    while (p > pi) p -= PV_TWO_PI;
    while (p < -pi) p += PV_TWO_PI;
    return p;
}

/* ---------------------------------------------------------- pitch shift */

int pitch_shift(const float *in, int len, int rate, int semitones,
                float **out, int *out_len)
{
    (void)rate;
    if (!in || len <= 0 || !out || !out_len) return -1;
    *out = NULL;
    *out_len = 0;

    semitones = pitch_clamp(semitones);
    if (semitones == 0) {
        float *copy = malloc(sizeof(float) * (size_t)len);
        if (!copy) return -1;
        memcpy(copy, in, sizeof(float) * (size_t)len);
        *out = copy;
        *out_len = len;
        return 0;
    }

    /* Ratio r: the vocoder stretches by r, then reading at step r brings the
     * length back to `len` while the pitch moves by r. */
    double r = pow(2.0, semitones / 12.0);
    int hop_s = (int)lround(PV_HOP * r);
    if (hop_s < 8) hop_s = 8;
    if (hop_s > PV_N) hop_s = PV_N;

    int frames = (len + PV_HOP - 1) / PV_HOP + 1;
    long st_len_l = (long)(frames - 1) * hop_s + PV_N + PV_N;
    if (st_len_l > 64L * 1024L * 1024L) return -1;      /* absurd input */
    int st_len = (int)st_len_l;

    float *st = calloc((size_t)st_len, sizeof(float));
    float *wsum = calloc((size_t)st_len, sizeof(float));
    float *win = malloc(sizeof(float) * PV_N);
    float *re = malloc(sizeof(float) * PV_N);
    float *im = malloc(sizeof(float) * PV_N);
    float *mag = malloc(sizeof(float) * PV_BINS);
    double *ph = malloc(sizeof(double) * PV_BINS);
    double *sph = malloc(sizeof(double) * PV_BINS);
    double *last = calloc((size_t)PV_BINS, sizeof(double));
    double *sum = calloc((size_t)PV_BINS, sizeof(double));
    unsigned char *peak = malloc(PV_BINS);
    float *res = malloc(sizeof(float) * (size_t)len);
    int ok = st && wsum && win && re && im && mag && ph && sph && last && sum &&
             peak && res;
    if (!ok) {
        free(st); free(wsum); free(win); free(re); free(im); free(mag);
        free(ph); free(sph); free(last); free(sum); free(peak); free(res);
        return -1;
    }

    for (int i = 0; i < PV_N; i++)
        win[i] = (float)(0.5 - 0.5 * cos(PV_TWO_PI * i / (PV_N - 1)));

    for (int f = 0; f < frames; f++) {
        int off = f * PV_HOP;
        for (int i = 0; i < PV_N; i++) {
            int s = off + i;
            re[i] = (s < len ? in[s] : 0.0f) * win[i];
            im[i] = 0.0f;
        }
        fft(re, im, PV_N, 0);

        for (int k = 0; k < PV_BINS; k++) {
            float rr = re[k], ii = im[k];
            mag[k] = sqrtf(rr * rr + ii * ii);
            ph[k] = atan2((double)ii, (double)rr);
            peak[k] = 0;
        }

        /* Spectral peaks: the phase lock anchors for their region. */
        for (int k = 1; k < PV_BINS - 1; k++)
            if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1] && mag[k] > 1e-7f)
                peak[k] = 1;
        if (PV_BINS > 1 && mag[0] > 1e-7f) peak[0] = 1;

        /* Advance the synthesis phase at each peak and lock its region to it
         * (identity phase locking: keep the input's relative phases). The
         * deviation is the true frequency offset from the bin centre measured
         * over the analysis hop, so it has to be scaled to the synthesis hop -
         * omitting that scale bends the pitch as the stretch factor leaves 1. */
        const double hop_scale = (double)hop_s / (double)PV_HOP;
        int cur = -1;
        for (int k = 0; k < PV_BINS; k++) {
            if (peak[k]) {
                double expected = PV_TWO_PI * k * PV_HOP / PV_N;
                double dev = wrap_pi(ph[k] - last[k] - expected);
                sum[k] += PV_TWO_PI * k * hop_s / PV_N + dev * hop_scale;
                sph[k] = sum[k];
                cur = k;
            } else if (cur >= 0) {
                sph[k] = sum[cur] + (ph[k] - ph[cur]);
            } else {
                double expected = PV_TWO_PI * k * PV_HOP / PV_N;
                double dev = wrap_pi(ph[k] - last[k] - expected);
                sum[k] += PV_TWO_PI * k * hop_s / PV_N + dev * hop_scale;
                sph[k] = sum[k];
            }
            last[k] = ph[k];
        }

        for (int k = 0; k < PV_BINS; k++) {
            re[k] = (float)(mag[k] * cos(sph[k]));
            im[k] = (float)(mag[k] * sin(sph[k]));
        }
        for (int k = 1; k < PV_N / 2; k++) {
            re[PV_N - k] = re[k];
            im[PV_N - k] = -im[k];
        }
        im[0] = 0.0f;
        im[PV_N / 2] = 0.0f;

        fft(re, im, PV_N, 1);

        int so = f * hop_s;
        for (int i = 0; i < PV_N && so + i < st_len; i++) {
            st[so + i] += re[i] * win[i];
            wsum[so + i] += win[i] * win[i];
        }
    }

    /* Normalise the overlap-add. The floor keeps the window ramps at the very
     * start and end from amplifying the phase-vocoder noise floor (the exact
     * division is unstable where the coverage is tiny). */
    for (int i = 0; i < st_len; i++)
        st[i] /= (wsum[i] > 0.25f ? wsum[i] : 0.25f);

    for (int j = 0; j < len; j++)
        res[j] = cubic_at(st, st_len, (double)j * r);

    free(st); free(wsum); free(win); free(re); free(im); free(mag);
    free(ph); free(sph); free(last); free(sum); free(peak);

    *out = res;
    *out_len = len;
    return 0;
}

/* ---------------------------------------------------------------- cache */

void pitch_cache_init(PitchCache *c, int rate, int max_entries, size_t max_bytes)
{
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->rate = rate;
    c->cap = max_entries > 0 ? max_entries : 1;
    c->max_bytes = max_bytes;
    c->entry = calloc((size_t)c->cap, sizeof *c->entry);
    if (!c->entry) c->cap = 0;
}

void pitch_cache_free(PitchCache *c)
{
    if (!c) return;
    pitch_cache_clear(c);
    free(c->entry);
    c->entry = NULL;
    c->cap = 0;
}

void pitch_cache_clear(PitchCache *c)
{
    if (!c) return;
    for (int i = 0; i < c->count; i++) {
        free(c->entry[i].data);
        c->entry[i].data = NULL;
        c->entry[i].len = 0;
        c->entry[i].key[0] = 0;
    }
    c->count = 0;
    c->bytes = 0;
    c->overflow = 0;
}

const float *pitch_cache_get(PitchCache *c, const char *key, const float *src,
                             int len, int semitones, int *out_len)
{
    if (out_len) *out_len = 0;
    if (!c || !key || !src || len <= 0) return NULL;
    semitones = pitch_clamp(semitones);
    if (semitones == 0) {
        if (out_len) *out_len = len;
        return NULL;                       /* play the original sample */
    }

    for (int i = 0; i < c->count; i++)
        if (c->entry[i].semitones == semitones && !strcmp(c->entry[i].key, key)) {
            if (out_len) *out_len = c->entry[i].len;
            return c->entry[i].data;
        }

    if (c->count >= c->cap) { c->overflow = 1; return NULL; }
    size_t need = sizeof(float) * (size_t)len;
    if (c->bytes + need > c->max_bytes) { c->overflow = 1; return NULL; }

    float *data = NULL;
    int olen = 0;
    if (pitch_shift(src, len, c->rate, semitones, &data, &olen) != 0) {
        c->overflow = 1;
        return NULL;
    }

    PitchEntry *e = &c->entry[c->count++];
    snprintf(e->key, sizeof e->key, "%s", key);
    e->semitones = semitones;
    e->data = data;
    e->len = olen;
    c->bytes += need;
    if (out_len) *out_len = olen;
    return e->data;
}

/* -------------------------------------------------------------- selftest */

/* Dominant frequency by counting zero crossings over the middle of a buffer. */
static double zero_cross_rate(const float *x, int len, int rate)
{
    int first = len / 4, last = len - len / 4;
    int crosses = 0;
    for (int i = first + 1; i < last; i++)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) crosses++;
    return (double)crosses * 0.5 * (double)rate / (double)(last - first - 1);
}

int pitch_selftest(int rate)
{
    int fails = 0;
    int len = rate / 2;                       /* 0.5 s */
    float *in = malloc(sizeof(float) * (size_t)len);
    if (!in) return 1;
    for (int i = 0; i < len; i++)
        in[i] = 0.6f * (float)sin(PV_TWO_PI * 440.0 * i / rate);

    static const int shifts[] = { 12, -12, 24, -24, 7 };
    for (int s = 0; s < (int)(sizeof shifts / sizeof shifts[0]); s++) {
        int st = shifts[s];
        float *out = NULL;
        int olen = 0;
        if (pitch_shift(in, len, rate, st, &out, &olen) != 0 || !out) {
            printf("FAIL: pitch_shift(%d) returned nothing\n", st);
            fails++;
            continue;
        }
        double want = 440.0 * pow(2.0, st / 12.0);
        double got = zero_cross_rate(out, olen, rate);
        if (olen != len) {
            printf("FAIL: pitch %+d changed the length (%d -> %d)\n", st, len, olen);
            fails++;
        }
        if (fabs(got - want) > want * 0.03) {
            printf("FAIL: pitch %+d -> %.1f Hz, expected %.1f Hz\n", st, got, want);
            fails++;
        }
        float peak = 0.0f;
        for (int i = 0; i < olen; i++) {
            float v = out[i] < 0 ? -out[i] : out[i];
            if (v > peak) peak = v;
        }
        if (peak < 0.25f || peak > 1.2f) {
            printf("FAIL: pitch %+d peak %.3f (expected ~0.6)\n", st, peak);
            fails++;
        }
        free(out);
    }

    /* Cache: a hit reuses the buffer, the interval is part of the key. */
    PitchCache c;
    pitch_cache_init(&c, rate, 4, 8u * 1024u * 1024u);
    int l1 = 0, l2 = 0;
    const float *a = pitch_cache_get(&c, "k", in, len, 12, &l1);
    const float *b = pitch_cache_get(&c, "k", in, len, 12, &l2);
    const float *d = pitch_cache_get(&c, "k", in, len, 24, &l2);
    const float *z = pitch_cache_get(&c, "k", in, len, 0, &l2);
    if (!a || a != b || l1 != len) {
        printf("FAIL: pitch cache does not reuse an entry\n");
        fails++;
    }
    if (!d || d == a || c.count != 2) {
        printf("FAIL: pitch cache keys on the interval\n");
        fails++;
    }
    if (z != NULL) {
        printf("FAIL: pitch cache should defer a zero shift\n");
        fails++;
    }
    if (c.bytes != (size_t)len * sizeof(float) * 2) {
        printf("FAIL: pitch cache byte accounting\n");
        fails++;
    }
    pitch_cache_free(&c);

    free(in);
    return fails;
}
