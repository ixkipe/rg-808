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

int wav_write_stereo16(const char *path, const float *interleaved, int frames, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    unsigned int data = (unsigned)frames * 4u;
    unsigned int riff = 36u + data;
    unsigned char h[44];
    memcpy(h, "RIFF", 4); h[4]=riff; h[5]=riff>>8; h[6]=riff>>16; h[7]=riff>>24;
    memcpy(h+8, "WAVEfmt ", 8);
    h[16]=16; h[20]=1; h[22]=2; h[24]=rate; h[25]=rate>>8; h[26]=rate>>16; h[27]=rate>>24;
    unsigned int br = (unsigned)rate*4; h[28]=br; h[29]=br>>8; h[30]=br>>16; h[31]=br>>24;
    h[32]=4; h[34]=16;
    memcpy(h+36, "data", 4); h[40]=data; h[41]=data>>8; h[42]=data>>16; h[43]=data>>24;
    fwrite(h, 1, 44, f);
    for (long i = 0; i < (long)frames * 2; i++) {
        float x = interleaved[i];
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        short s = (short)(x * 32767.0f);
        unsigned char b[2] = { (unsigned char)(s & 0xff), (unsigned char)(((unsigned short)s >> 8) & 0xff) };
        fwrite(b, 1, 2, f);
    }
    fclose(f);
    return 0;
}
