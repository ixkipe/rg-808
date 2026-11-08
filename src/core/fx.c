/* RG-808 effects settings and persistence. */
#include "fx.h"
#include "pattern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const FX_DIV_NAMES[FX_DIV_COUNT] = {
    "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/16T", "4D", "8D", "16D"
};

static FxSettings fallback;
static int fallback_ready;

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void fx_defaults(FxSettings *f)
{
    memset(f, 0, sizeof *f);
    f->on = 1;                 /* chain ready; every stage starts bypassed */

    f->drive_on = 0;
    f->drive_type = FX_DRIVE_CRUSH;
    f->crush_bits = 8;
    f->crush_down = 4;
    f->drive_amount = 50;
    f->drive_mix = 50;
    f->comp_threshold = 55;
    f->comp_ratio = 4;
    f->comp_makeup = 30;

    f->mod_on = 0;
    f->mod_type = FX_MOD_FLANGER;
    f->mod_rate_x100 = 50;     /* 0.50 Hz */
    f->mod_depth = 50;
    f->mod_fb = 30;
    f->mod_mix = 50;

    f->delay_on = 0;
    f->delay_sync = 0;
    f->delay_ms = 250;
    f->delay_div = FX_DIV_8;
    f->delay_fb = 30;
    f->delay_mix = 30;

    f->reverb_on = 0;
    f->reverb_room = 50;
    f->reverb_damp = 50;
    f->reverb_mix = 30;
}

void fx_clamp(FxSettings *f)
{
    f->on = f->on ? 1 : 0;

    f->drive_on = f->drive_on ? 1 : 0;
    f->drive_type = clampi(f->drive_type, 0, FX_DRIVE_COUNT - 1);
    f->crush_bits = clampi(f->crush_bits, 4, 16);
    f->crush_down = clampi(f->crush_down, 1, 32);
    f->drive_amount = clampi(f->drive_amount, 0, 100);
    f->drive_mix = clampi(f->drive_mix, 0, 100);
    f->comp_threshold = clampi(f->comp_threshold, 0, 100);
    f->comp_ratio = clampi(f->comp_ratio, 1, 20);
    f->comp_makeup = clampi(f->comp_makeup, 0, 100);

    f->mod_on = f->mod_on ? 1 : 0;
    f->mod_type = clampi(f->mod_type, 0, FX_MOD_COUNT - 1);
    f->mod_rate_x100 = clampi(f->mod_rate_x100, 5, 500);
    f->mod_depth = clampi(f->mod_depth, 0, 100);
    f->mod_fb = clampi(f->mod_fb, 0, 90);
    f->mod_mix = clampi(f->mod_mix, 0, 100);

    f->delay_on = f->delay_on ? 1 : 0;
    f->delay_sync = f->delay_sync ? 1 : 0;
    f->delay_ms = clampi(f->delay_ms, 20, 4000);
    f->delay_div = clampi(f->delay_div, 0, FX_DIV_COUNT - 1);
    f->delay_fb = clampi(f->delay_fb, 0, 90);
    f->delay_mix = clampi(f->delay_mix, 0, 100);

    f->reverb_on = f->reverb_on ? 1 : 0;
    f->reverb_room = clampi(f->reverb_room, 0, 100);
    f->reverb_damp = clampi(f->reverb_damp, 0, 100);
    f->reverb_mix = clampi(f->reverb_mix, 0, 100);
}

void fx_set_fallback(const FxSettings *f)
{
    fallback = *f;
    fx_clamp(&fallback);
    fallback_ready = 1;
}

void fx_get_fallback(FxSettings *f)
{
    if (fallback_ready) *f = fallback;
    else fx_defaults(f);
}

void fx_apply(Engine *e, const FxSettings *f)
{
    FxSettings c = *f;
    fx_clamp(&c);
    e->fx.on = c.on;
    e->fx.drive_on = c.drive_on;
    e->fx.drive_type = c.drive_type;
    e->fx.crush_bits = c.crush_bits;
    e->fx.crush_down = c.crush_down;
    e->fx.drive_amount = c.drive_amount;
    e->fx.drive_mix = c.drive_mix;
    e->fx.comp_threshold = c.comp_threshold;
    e->fx.comp_ratio = c.comp_ratio;
    e->fx.comp_makeup = c.comp_makeup;
    e->fx.mod_on = c.mod_on;
    e->fx.mod_type = c.mod_type;
    e->fx.mod_rate_x100 = c.mod_rate_x100;
    e->fx.mod_depth = c.mod_depth;
    e->fx.mod_fb = c.mod_fb;
    e->fx.mod_mix = c.mod_mix;
    e->fx.delay_on = c.delay_on;
    e->fx.delay_sync = c.delay_sync;
    e->fx.delay_ms = c.delay_ms;
    e->fx.delay_div = c.delay_div;
    e->fx.delay_fb = c.delay_fb;
    e->fx.delay_mix = c.delay_mix;
    e->fx.reverb_on = c.reverb_on;
    e->fx.reverb_room = c.reverb_room;
    e->fx.reverb_damp = c.reverb_damp;
    e->fx.reverb_mix = c.reverb_mix;
}

void fx_capture(FxSettings *f, const Engine *e)
{
    f->on = e->fx.on;
    f->drive_on = e->fx.drive_on;
    f->drive_type = e->fx.drive_type;
    f->crush_bits = e->fx.crush_bits;
    f->crush_down = e->fx.crush_down;
    f->drive_amount = e->fx.drive_amount;
    f->drive_mix = e->fx.drive_mix;
    f->comp_threshold = e->fx.comp_threshold;
    f->comp_ratio = e->fx.comp_ratio;
    f->comp_makeup = e->fx.comp_makeup;
    f->mod_on = e->fx.mod_on;
    f->mod_type = e->fx.mod_type;
    f->mod_rate_x100 = e->fx.mod_rate_x100;
    f->mod_depth = e->fx.mod_depth;
    f->mod_fb = e->fx.mod_fb;
    f->mod_mix = e->fx.mod_mix;
    f->delay_on = e->fx.delay_on;
    f->delay_sync = e->fx.delay_sync;
    f->delay_ms = e->fx.delay_ms;
    f->delay_div = e->fx.delay_div;
    f->delay_fb = e->fx.delay_fb;
    f->delay_mix = e->fx.delay_mix;
    f->reverb_on = e->fx.reverb_on;
    f->reverb_room = e->fx.reverb_room;
    f->reverb_damp = e->fx.reverb_damp;
    f->reverb_mix = e->fx.reverb_mix;
}

int fx_delay_ms_for(int sync, int delay_ms, int div, int bpm_x100)
{
    static const double steps[FX_DIV_COUNT] = {
        4.0, 2.0, 1.0,                       /* 1/4, 1/8, 1/16 */
        8.0 / 3.0, 4.0 / 3.0, 2.0 / 3.0,     /* triplets */
        6.0, 3.0, 1.5                         /* dotted 4, 8, 16 */
    };
    if (!sync)
        return clampi(delay_ms, 20, 4000);

    int bpm = bpm_x100 < 100 ? 100 : bpm_x100;
    int d = clampi(div, 0, FX_DIV_COUNT - 1);
    double seconds = steps[d] * 60.0 / ((bpm / 100.0) * 4.0);
    int ms = (int)(seconds * 1000.0 + 0.5);
    if (ms < 1) ms = 1;
    if (ms > 8000) ms = 8000;
    return ms;
}

int fx_parse_line(FxSettings *f, const char *line)
{
    if (!strncmp(line, "FX ON ", 6)) {
        f->on = atoi(line + 6);
        return 1;
    }
    if (!strncmp(line, "FX DRIVE ", 9)) {
        int on, type, bits, down, amount, mix, thr, ratio, makeup;
        if (sscanf(line + 9, "%d %d %d %d %d %d %d %d %d",
                   &on, &type, &bits, &down, &amount, &mix,
                   &thr, &ratio, &makeup) == 9) {
            f->drive_on = on;
            f->drive_type = type;
            f->crush_bits = bits;
            f->crush_down = down;
            f->drive_amount = amount;
            f->drive_mix = mix;
            f->comp_threshold = thr;
            f->comp_ratio = ratio;
            f->comp_makeup = makeup;
        }
        return 1;
    }
    if (!strncmp(line, "FX MOD ", 7)) {
        int on, type, rate, depth, fb, mix;
        if (sscanf(line + 7, "%d %d %d %d %d %d",
                   &on, &type, &rate, &depth, &fb, &mix) == 6) {
            f->mod_on = on;
            f->mod_type = type;
            f->mod_rate_x100 = rate;
            f->mod_depth = depth;
            f->mod_fb = fb;
            f->mod_mix = mix;
        }
        return 1;
    }
    if (!strncmp(line, "FX DELAY ", 9)) {
        int on, sync, ms, div, fb, mix;
        int n = sscanf(line + 9, "%d %d %d %d %d %d",
                       &on, &sync, &ms, &div, &fb, &mix);
        if (n == 6) {
            f->delay_on = on;
            f->delay_sync = sync;
            f->delay_ms = ms;
            f->delay_div = div;
            f->delay_fb = fb;
            f->delay_mix = mix;
        } else if (sscanf(line + 9, "%d %d %d %d", &on, &ms, &fb, &mix) == 4) {
            /* M9 format: on ms feedback mix */
            f->delay_on = on;
            f->delay_sync = 0;
            f->delay_ms = ms;
            f->delay_fb = fb;
            f->delay_mix = mix;
        }
        return 1;
    }
    if (!strncmp(line, "FX REVERB ", 10)) {
        int on, room, damp, mix;
        if (sscanf(line + 10, "%d %d %d %d", &on, &room, &damp, &mix) == 4) {
            f->reverb_on = on;
            f->reverb_room = room;
            f->reverb_damp = damp;
            f->reverb_mix = mix;
        }
        return 1;
    }

    /* M9 names from before the drive/mod slots became selectable. */
    if (!strncmp(line, "FX CRUSH ", 9)) {
        int on, bits, down;
        if (sscanf(line + 9, "%d %d %d", &on, &bits, &down) == 3) {
            f->drive_on = on;
            f->drive_type = FX_DRIVE_CRUSH;
            f->crush_bits = bits;
            f->crush_down = down;
        }
        return 1;
    }
    if (!strncmp(line, "FX FLANGER ", 11)) {
        int on, rate, depth, fb, mix;
        if (sscanf(line + 11, "%d %d %d %d %d",
                   &on, &rate, &depth, &fb, &mix) == 5) {
            f->mod_on = on;
            f->mod_type = FX_MOD_FLANGER;
            f->mod_rate_x100 = rate;
            f->mod_depth = depth;
            f->mod_fb = fb;
            f->mod_mix = mix;
        }
        return 1;
    }
    if (!strncmp(line, "FX PHASER ", 10)) {
        int on, rate, depth, fb, mix;
        if (sscanf(line + 10, "%d %d %d %d %d",
                   &on, &rate, &depth, &fb, &mix) == 5) {
            f->mod_on = on;
            f->mod_type = FX_MOD_PHASER;
            f->mod_rate_x100 = rate;
            f->mod_depth = depth;
            f->mod_fb = fb;
            f->mod_mix = mix;
        }
        return 1;
    }
    return 0;
}

void fx_write_stream(FILE *fp, const FxSettings *f)
{
    FxSettings c = *f;
    fx_clamp(&c);
    fprintf(fp, "FX ON %d\n", c.on);
    fprintf(fp, "FX DRIVE %d %d %d %d %d %d %d %d %d\n",
            c.drive_on, c.drive_type, c.crush_bits, c.crush_down,
            c.drive_amount, c.drive_mix, c.comp_threshold,
            c.comp_ratio, c.comp_makeup);
    fprintf(fp, "FX MOD %d %d %d %d %d %d\n",
            c.mod_on, c.mod_type, c.mod_rate_x100,
            c.mod_depth, c.mod_fb, c.mod_mix);
    fprintf(fp, "FX DELAY %d %d %d %d %d %d\n",
            c.delay_on, c.delay_sync, c.delay_ms,
            c.delay_div, c.delay_fb, c.delay_mix);
    fprintf(fp, "FX REVERB %d %d %d %d\n",
            c.reverb_on, c.reverb_room, c.reverb_damp, c.reverb_mix);
}

int fx_load_file(FxSettings *f, const char *path)
{
    fx_defaults(f);
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[256];
    while (fgets(line, sizeof line, fp))
        fx_parse_line(f, line);
    fclose(fp);
    fx_clamp(f);
    return 0;
}

int fx_save_file(const FxSettings *f, const char *path)
{
    FxSettings c = *f;
    fx_clamp(&c);

    char buf[1024];
    int n = snprintf(buf, sizeof buf, "RG808FX2\n");
    if (n < 0) return -1;
    /* Format the body in memory so the shared atomic writer can be used. */
    char body[768];
    int m = snprintf(body, sizeof body,
                     "FX ON %d\n"
                     "FX DRIVE %d %d %d %d %d %d %d %d %d\n"
                     "FX MOD %d %d %d %d %d %d\n"
                     "FX DELAY %d %d %d %d %d %d\n"
                     "FX REVERB %d %d %d %d\n",
                     c.on,
                     c.drive_on, c.drive_type, c.crush_bits, c.crush_down,
                     c.drive_amount, c.drive_mix, c.comp_threshold,
                     c.comp_ratio, c.comp_makeup,
                     c.mod_on, c.mod_type, c.mod_rate_x100,
                     c.mod_depth, c.mod_fb, c.mod_mix,
                     c.delay_on, c.delay_sync, c.delay_ms,
                     c.delay_div, c.delay_fb, c.delay_mix,
                     c.reverb_on, c.reverb_room, c.reverb_damp, c.reverb_mix);
    if (m < 0 || m >= (int)sizeof body || n + m >= (int)sizeof buf) return -1;
    memcpy(buf + n, body, (size_t)m);
    n += m;
    return pattern_write_atomic(path, buf, (size_t)n);
}
