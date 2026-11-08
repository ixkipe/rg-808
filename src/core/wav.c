#include "wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int rd_u32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static unsigned short rd_u16(const unsigned char *p)
{
    return (unsigned short)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

int wav_load(const char *path, int target_rate, WavAudio *out)
{
    out->data = NULL; out->frames = 0; out->rate = target_rate;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        fclose(f); return -2;
    }

    unsigned short fmt = 1, channels = 1, bits = 16;
    unsigned int rate = 44100;
    unsigned char *raw = NULL; size_t rawlen = 0;

    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        unsigned int size = rd_u32(ch + 4);
        if (!memcmp(ch, "fmt ", 4)) {
            unsigned char fb[40];
            unsigned int n = size < 40 ? size : 40;
            if (fread(fb, 1, n, f) != n) { free(raw); fclose(f); return -3; }
            fmt = rd_u16(fb); channels = rd_u16(fb + 2); rate = rd_u32(fb + 4); bits = rd_u16(fb + 14);
        } else if (!memcmp(ch, "data", 4)) {
            raw = malloc(size);
            if (!raw) { fclose(f); return -4; }
            rawlen = fread(raw, 1, size, f);
            break;                     /* done */
        } else {
            if (size & 1) size++;
            if (fseek(f, size, SEEK_CUR) != 0) break;
        }
    }
    fclose(f);

    if (!raw || channels == 0) { free(raw); return -5; }

    int bytes_per = bits / 8;
    long total_frames = (long)(rawlen / (channels * bytes_per));
    if (total_frames <= 0) { free(raw); return -6; }

    /* decode to mono float */
    float *mono = malloc(sizeof(float) * (size_t)total_frames);
    if (!mono) { free(raw); return -7; }
    for (long i = 0; i < total_frames; i++) {
        float acc = 0.0f;
        const unsigned char *p = raw + (size_t)i * channels * bytes_per;
        for (unsigned c = 0; c < channels; c++) {
            float s = 0.0f;
            const unsigned char *q = p + (size_t)c * bytes_per;
            switch (fmt) {
            case 1: /* PCM */
                if (bits == 8)        s = ((int)q[0] - 128) / 128.0f;
                else if (bits == 16)  s = (short)(q[0] | (q[1] << 8)) / 32768.0f;
                else if (bits == 24)  { int v = (int)(q[0] | (q[1] << 8) | (q[2] << 16));
                                        if (v & 0x800000) v -= 0x1000000;
                                        s = v / 8388608.0f; }
                else if (bits == 32)  s = (float)((int)q[0] | ((int)q[1] << 8) | ((int)q[2] << 16) | ((int)q[3] << 24)) / 2147483648.0f;
                break;
            case 3: /* float */
                if (bits == 32) { float v; memcpy(&v, q, 4); s = v; }
                else if (bits == 64) { double v; memcpy(&v, q, 8); s = (float)v; }
                break;
            default: break;
            }
            acc += s;
        }
        mono[i] = acc / (float)channels;
    }
    free(raw);

    if (rate == (unsigned)target_rate || total_frames < 2) {
        out->data = mono; out->frames = (int)total_frames; out->rate = (int)rate;
        return 0;
    }

    /* linear resample to target rate */
    double ratio = (double)target_rate / (double)rate;
    long n = (long)(total_frames * ratio);
    float *res = malloc(sizeof(float) * (size_t)(n + 1));
    if (!res) { free(mono); return -8; }
    for (long i = 0; i < n; i++) {
        double src = (double)i / ratio;
        long i0 = (long)src;
        if (i0 >= total_frames - 1) i0 = total_frames - 2;
        double fr = src - (double)i0;
        res[i] = (float)(mono[i0] * (1.0 - fr) + mono[i0 + 1] * fr);
    }
    free(mono);
    out->data = res; out->frames = (int)n; out->rate = target_rate;
    return 0;
}

static void put_u32le(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void write_wav_header(FILE *f, unsigned int data_bytes, int rate)
{
    unsigned char h[44] = {0};
    unsigned int riff = 36u + data_bytes;
    memcpy(h, "RIFF", 4); put_u32le(h + 4, riff);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_u32le(h + 16, 16);
    h[20] = 1;                       /* PCM */
    h[22] = 2;                       /* stereo */
    put_u32le(h + 24, (unsigned int)rate);
    put_u32le(h + 28, (unsigned int)rate * 4u);
    h[32] = 4;                       /* block align */
    h[34] = 16;                      /* bits per sample */
    memcpy(h + 36, "data", 4); put_u32le(h + 40, data_bytes);
    fwrite(h, 1, 44, f);
}

int wav_writer_open(WavWriter *w, const char *path, int rate)
{
    memset(w, 0, sizeof *w);
    w->rate = rate;
    w->f = fopen(path, "wb");
    if (!w->f) return -1;
    unsigned char zero[44] = {0};
    if (fwrite(zero, 1, 44, w->f) != 44) {
        fclose(w->f);
        w->f = NULL;
        return -1;
    }
    return 0;
}

int wav_writer_write(WavWriter *w, const float *interleaved, int frames)
{
    if (!w->f || frames < 0) return -1;
    while (frames > 0) {
        int n = frames > 1024 ? 1024 : frames;
        unsigned char out[4096];
        for (int i = 0; i < 2 * n; i++) {
            float x = interleaved[i];
            if (x > 1.0f) x = 1.0f;
            if (x < -1.0f) x = -1.0f;
            short s = (short)(x * 32767.0f);
            out[2 * i] = (unsigned char)(s & 0xff);
            out[2 * i + 1] = (unsigned char)(((unsigned short)s >> 8) & 0xff);
        }
        if (fwrite(out, 1, (size_t)2 * 2 * n, w->f) != (size_t)2 * 2 * n)
            return -1;
        w->frames += (unsigned int)n;
        interleaved += 2 * n;
        frames -= n;
    }
    return 0;
}

int wav_writer_close(WavWriter *w)
{
    if (!w->f) return -1;
    int ok = fflush(w->f) == 0;
    if (ok) ok = fseek(w->f, 0, SEEK_SET) == 0;
    if (ok) {
        write_wav_header(w->f, w->frames * 4u, w->rate);
        if (ferror(w->f)) ok = 0;
    }
    if (fclose(w->f) != 0) ok = 0;
    w->f = NULL;
    return ok ? 0 : -1;
}

int wav_write_stereo16(const char *path, const float *interleaved, int frames, int rate)
{
    WavWriter w;
    if (wav_writer_open(&w, path, rate) != 0) return -1;
    if (wav_writer_write(&w, interleaved, frames) != 0) {
        fclose(w.f);
        return -1;
    }
    return wav_writer_close(&w);
}
