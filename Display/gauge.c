/* 1960 Ford F-100 speedometer, amber night-lit, after the reference photo
 * (Dash Reference.jpg): spun-aluminum center dome, dark warm band with
 * rotated 0-100 numerals, fine ticks, odometer drum window, off-state
 * turn arrows, thin orange lance needle under a chrome hub.
 *
 * The face renders at build time (tools/genface.c) into flash; only the
 * needle sprite is drawn at runtime. Rendering cost here is free. */

#include "gauge.h"
#include <math.h>

/* ---- palette ------------------------------------------------------------
 * One CLUT serves both layers. AL44 (needle sprite) expands its 4-bit
 * index to {L,L}, so multiples of 17 belong to the sprite; the face (L8)
 * uses everything else. */
enum {
    C_BLACK    = 0,
    C_BAND     = 1,   /* warm bronze band                  */
    C_BAND_DK  = 2,   /* band, outer/darker                */
    SPN_CORE   = 3,   /* sprite nibble: needle core        */
    C_SEAM     = 4,   /* dark seams / recesses             */
    C_CREAM    = 5,   /* markings                          */
    C_CREAM_HI = 6,   /* markings, highlighted             */
    C_ARROW    = 7,   /* unlit turn arrows                 */
    C_ODO_CELL = 8,   /* odometer drum, amber backlit      */
    SPN_EDGE   = 9,   /* sprite nibble: needle dark edge   */
    C_ODO_DIG  = 10,  /* odometer digits                   */
    C_ODO_FRM  = 11,  /* odometer chrome frame             */
    C_ODO_BG   = 12,  /* odometer recess                   */
    C_HUB_HI   = 13,  /* hub chrome, lit side              */
    C_HUB_MID  = 14,
    C_HUB_LO   = 15,
    C_DOME_RIM = 16,  /* bright ring at dome edge          */
};
static const uint8_t DOME_RAMP[12]  = {20,21,22,23,24,25,26,27,28,29,30,31};
static const uint8_t BEZEL_RAMP[5]  = {40,41,42,43,44};

const uint32_t Gauge_CLUT[256] = {
    [C_BLACK]    = 0x000000,
    [C_BAND]     = 0x54381A,
    [C_BAND_DK]  = 0x3A2712,
    [C_SEAM]     = 0x221506,
    [C_CREAM]    = 0xF3D492,
    [C_CREAM_HI] = 0xFFE7B0,
    [C_ARROW]    = 0x1C1206,
    [C_ODO_CELL] = 0xDD9838,
    [C_ODO_DIG]  = 0x201302,
    [C_ODO_FRM]  = 0xD9B87A,
    [C_ODO_BG]   = 0x120C04,
    [C_HUB_HI]   = 0xF6E0B0,
    [C_HUB_MID]  = 0xC9A263,
    [C_HUB_LO]   = 0x8A6A38,
    [C_DOME_RIM] = 0xF0C57E,
    /* spun dome ramp, dark to bright amber metal */
    [20] = 0x6E4A22, [21] = 0x7A5428, [22] = 0x875E2E, [23] = 0x946935,
    [24] = 0xA1743B, [25] = 0xAE7F42, [26] = 0xBB8A49, [27] = 0xC79550,
    [28] = 0xD4A057, [29] = 0xE0AB5E, [30] = 0xE7B468, [31] = 0xEDBE72,
    /* bezel chrome ramp, warm */
    [40] = 0x7A5C30, [41] = 0x9A7A44, [42] = 0xBC9858, [43] = 0xD9B672, [44] = 0xEDCB8B,
    /* sprite colors at {L,L} addresses (AL44 lookup) + plain-index mirrors
     * so the host preview and L8 paths agree */
    [SPN_CORE]      = 0xF56528, [SPN_CORE * 17] = 0xF56528,
    [SPN_EDGE]      = 0x99330E, [SPN_EDGE * 17] = 0x99330E,
};

/* ---- geometry ---- */
#define SWEEP_START_DEG 150.0f   /* 0 MPH, screen-space angle (y down) */
#define SWEEP_DEG       240.0f
#define MPH_MAX         100.0f

static int W = 720, H = 720;
static uint8_t *FB;

/* Undo log for the host-preview needle path (firmware needle is a sprite) */
#define UNDO_MAX 6000
static uint32_t undo_off[UNDO_MAX];
static uint8_t  undo_val[UNDO_MAX];
static uint32_t undo_n;
static int      recording;
static uint32_t dirty_lo, dirty_hi;

static inline void mark_dirty(uint32_t idx) {
    if (idx < dirty_lo) dirty_lo = idx;
    if (idx > dirty_hi) dirty_hi = idx;
}

static inline void px(int x, int y, uint8_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) {
        uint32_t idx = (uint32_t)(y * W + x);
        if (recording) {
            if (undo_n < UNDO_MAX) {
                undo_off[undo_n] = idx;
                undo_val[undo_n] = FB[idx];
                undo_n++;
            }
            mark_dirty(idx);
        }
        FB[idx] = c;
    }
}

static void draw_seg(float x0, float y0, float x1, float y1, float w, uint8_t c) {
    float hw = w * 0.5f;
    int minx = (int)fminf(x0, x1) - (int)hw - 1, maxx = (int)fmaxf(x0, x1) + (int)hw + 1;
    int miny = (int)fminf(y0, y1) - (int)hw - 1, maxy = (int)fmaxf(y0, y1) + (int)hw + 1;
    float vx = x1 - x0, vy = y1 - y0, len2 = vx*vx + vy*vy;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            float t = len2 > 0 ? ((x - x0)*vx + (y - y0)*vy) / len2 : 0;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            float dx = x - (x0 + t*vx), dy = y - (y0 + t*vy);
            if (dx*dx + dy*dy <= hw*hw) px(x, y, c);
        }
}

/* ---- 5x7 font (digits only used on this face) ---- */
static const uint8_t FONT[13][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
};

static int glyph_index(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch == 'M') return 10;
    if (ch == 'P') return 11;
    if (ch == 'H') return 12;
    return -1;
}

static int text_width(const char *s, int scale) {
    int n = 0; while (s[n]) n++;
    return n * 5 * scale + (n - 1) * scale;
}

static void draw_text_center(int cx, int cy, const char *s, int scale, uint8_t c) {
    int x = cx - text_width(s, scale) / 2, y = cy - 7 * scale / 2;
    for (; *s; s++, x += 6 * scale) {
        int gi = glyph_index(*s);
        if (gi < 0) continue;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (FONT[gi][row] & (0x10 >> col))
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            px(x + col*scale + sx, y + row*scale + sy, c);
    }
}

/* rotated text: glyphs swept along the arc, tops pointing at the hub —
 * rasterized by inverse-sampling so there are no holes */
static void draw_text_rotated(float cx, float cy, const char *s, int scale, float rot, uint8_t c) {
    int n = 0; while (s[n]) n++;
    float tw = (float)(n * 5 * scale + (n - 1) * scale), th = 7.0f * scale;
    float cr = cosf(rot), sr = sinf(rot);
    int ext = (int)(sqrtf(tw*tw + th*th) / 2.0f) + 2;
    for (int oy = -ext; oy <= ext; oy++)
        for (int ox = -ext; ox <= ext; ox++) {
            /* map output offset back into unrotated text space */
            float ux =  ox*cr + oy*sr + tw/2.0f;
            float uy = -ox*sr + oy*cr + th/2.0f;
            if (ux < 0 || uy < 0 || ux >= tw || uy >= th) continue;
            int gi6 = (int)(ux / (6*scale));
            float gx = ux - gi6 * 6.0f * scale;
            if (gx >= 5.0f * scale) continue;      /* inter-glyph gap */
            int gidx = glyph_index(s[gi6]);
            if (gidx < 0) continue;
            int col = (int)(gx / scale), row = (int)(uy / scale);
            if (row > 6) continue;
            if (FONT[gidx][row] & (0x10 >> col))
                px((int)(cx + ox), (int)(cy + oy), c);
        }
}

static float mph_angle(float mph) {
    return (SWEEP_START_DEG + SWEEP_DEG * (mph / MPH_MAX)) * (3.14159265f / 180.0f);
}

/* ---- the face ---- */
void Gauge_RenderFace(uint8_t *fb, int w, int h) {
    FB = fb; W = w; H = h;
    undo_n = 0; recording = 0;
    const float cx = w / 2.0f, cy = h / 2.0f;

    /* base shading pass: bezel, band, spun dome, hub — per pixel */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float dx = x - cx, dy = y - cy;
            float r = sqrtf(dx*dx + dy*dy);
            uint8_t c;
            if (r > 359.0f)      c = C_BLACK;
            else if (r > 339.0f) {                       /* chrome bezel */
                float t = 1.0f - fabsf((r - 349.5f) / 10.0f);  /* crown */
                t *= 0.75f + 0.25f * (-dy / (r + 1.0f));       /* lit from top */
                int i = (int)(t * 4.99f);
                c = BEZEL_RAMP[i < 0 ? 0 : (i > 4 ? 4 : i)];
            }
            else if (r > 334.0f) c = C_SEAM;             /* bezel-band seam  */
            else if (r > 226.0f) {                       /* markings band    */
                c = (r > 326.0f) ? C_BAND_DK : C_BAND;
            }
            else if (r > 218.0f) c = C_DOME_RIM;         /* dome edge ring   */
            else if (r > 42.0f) {                        /* spun dome        */
                float a = atan2f(dy, dx);
                /* anisotropic spun-metal lobes + concentric brushing */
                float lobe = 0.72f + 0.28f * fabsf(cosf(a + 1.9f));
                float ring = 0.975f + 0.025f * sinf(r * 0.9f);
                float streak = 1.0f + 0.030f * sinf(a * 141.0f) * (0.3f + 0.7f * r / 218.0f);
                float edge = 1.05f - 0.30f * (r / 218.0f) * (r / 218.0f);
                float t = lobe * ring * streak * edge;
                int i = (int)(t * 11.99f);
                c = DOME_RAMP[i < 0 ? 0 : (i > 11 ? 11 : i)];
            }
            else if (r > 36.0f)  c = C_SEAM;             /* hub seat         */
            else {                                       /* chrome hub ball  */
                float d = sqrtf((dx + 10)*(dx + 10) + (dy + 12)*(dy + 12));
                c = d < 16.0f ? C_HUB_HI : (d < 30.0f ? C_HUB_MID : C_HUB_LO);
            }
            fb[y * w + x] = c;
        }

    /* ticks at the band's outer edge (as on the reference), minors every
     * 2 MPH, majors every 10, plus the little square dots inboard */
    for (int v = 0; v <= (int)MPH_MAX; v += 2) {
        float a = mph_angle((float)v), ca = cosf(a), sa = sinf(a);
        int major = (v % 10) == 0;
        float r0 = major ? 302.0f : 308.0f, r1 = major ? 330.0f : 324.0f;
        float wd = major ? 5.0f : 2.0f;
        draw_seg(cx + ca*r0, cy + sa*r0, cx + ca*r1, cy + sa*r1, wd,
                 major ? C_CREAM_HI : C_CREAM);
    }
    for (int v = 3; v < (int)MPH_MAX; v += 10)      /* square dots at +3/+7 */
        for (int d = 0; d < 2; d++) {
            float a = mph_angle((float)(v + d*4));
            int sx = (int)(cx + cosf(a)*288.0f), sy = (int)(cy + sinf(a)*288.0f);
            for (int yy = -3; yy <= 3; yy++)
                for (int xx = -3; xx <= 3; xx++) px(sx+xx, sy+yy, C_CREAM);
        }

    /* numerals 0..100, rotated so tops face the hub */
    for (int v = 0; v <= (int)MPH_MAX; v += 10) {
        float a = mph_angle((float)v);
        char label[4]; int n = 0;
        if (v >= 100) label[n++] = '1';
        if (v >= 10)  label[n++] = (char)('0' + (v / 10) % 10);
        label[n++] = '0'; label[n] = 0;
        if (v == 0) { label[0] = '0'; label[1] = 0; }
        draw_text_rotated(cx + cosf(a) * 258.0f, cy + sinf(a) * 258.0f,
                          label, 3, a + 3.14159265f / 2.0f, C_CREAM_HI);
    }

    /* odometer drum window below the hub, frozen at the photo's mileage */
    {
        const int ox0 = 285, oy0 = 452, cw = 30, ch2 = 44, ncell = 5;
        const char *digits = "47914";
        int ox1 = ox0 + ncell * cw, oy1 = oy0 + ch2;
        for (int y = oy0 - 4; y < oy1 + 4; y++)          /* chrome frame */
            for (int x = ox0 - 4; x < ox1 + 4; x++)
                px(x, y, C_ODO_FRM);
        for (int y = oy0 - 2; y < oy1 + 2; y++)          /* recess */
            for (int x = ox0 - 2; x < ox1 + 2; x++)
                px(x, y, C_ODO_BG);
        for (int i = 0; i < ncell; i++) {
            for (int y = oy0; y < oy1; y++)              /* amber-lit drum */
                for (int x = ox0 + i*cw + 1; x < ox0 + (i+1)*cw - 1; x++)
                    px(x, y, C_ODO_CELL);
            char d[2] = { digits[i], 0 };
            draw_text_center(ox0 + i*cw + cw/2, oy0 + ch2/2, d, 4, C_ODO_DIG);
        }
    }

    /* unlit turn arrows flanking the top, as on the cluster panel */
    for (int side = 0; side < 2; side++) {
        int dir = side ? 1 : -1;                 /* -1 left, +1 right */
        int ax = (int)cx + dir * 196, ay = 104;  /* outward-pointing tip */
        for (int i = 0; i < 26; i++)             /* widens toward center */
            for (int j = -i * 2 / 3; j <= i * 2 / 3; j++)
                px(ax - dir * i, ay + j, C_ARROW);
    }
}

/* ---- host-preview needle (firmware uses the sprite path below) ---- */
void Gauge_SetNeedle(float mph, uint32_t *dirty_first, uint32_t *dirty_last) {
    const int cx = W / 2, cy = H / 2;
    dirty_lo = UINT32_MAX; dirty_hi = 0;
    while (undo_n > 0) {
        undo_n--;
        FB[undo_off[undo_n]] = undo_val[undo_n];
        mark_dirty(undo_off[undo_n]);
    }
    recording = 1;
    {
        float a = mph_angle(mph), ca = cosf(a), sa = sinf(a);
        draw_seg(cx + ca*37.0f, cy + sa*37.0f, cx + ca*170.0f, cy + sa*170.0f, 9.0f, SPN_EDGE);
        draw_seg(cx + ca*170.0f, cy + sa*170.0f, cx + ca*276.0f, cy + sa*276.0f, 4.5f, SPN_EDGE);
        draw_seg(cx + ca*38.0f, cy + sa*38.0f, cx + ca*170.0f, cy + sa*170.0f, 6.2f, SPN_CORE);
        draw_seg(cx + ca*170.0f, cy + sa*170.0f, cx + ca*272.0f, cy + sa*272.0f, 2.2f, SPN_CORE);
    }
    recording = 0;
    if (dirty_first) *dirty_first = (dirty_lo == UINT32_MAX) ? 0 : dirty_lo;
    if (dirty_last)  *dirty_last  = dirty_hi;
}

void Gauge_RenderSpeedo(uint8_t *fb, int w, int h, float mph) {
    Gauge_RenderFace(fb, w, h);
    Gauge_SetNeedle(mph, 0, 0);
}

/* ---- needle sprite (AL44) for LTDC layer-2 compositing ----
 *
 * Rasterization walks ALONG the needle spine painting perpendicular
 * spans, so cost is proportional to the needle's actual area and flat
 * across all angles. (The previous distance-field rasterizer walked the
 * bounding box, which is ~7x larger at diagonal angles — the cause of
 * the sluggish needle 45 degrees off center at 64 MHz.)
 *
 * Erasing re-walks the PREVIOUS needle shape writing transparent, so no
 * box clear is needed either; the caller passes the mph the buffer
 * currently holds (negative = buffer is known-zeroed). */

static void needle_span(uint8_t *buf, float x0, float y0, float x1, float y1,
                        float wd, uint8_t val) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1e-3f) return;
    float ux = dx / len, uy = dy / len;    /* spine direction  */
    float nx = -uy, ny = ux;               /* span direction   */
    float hw = wd * 0.5f;
    for (float s = 0.0f; s <= len; s += 0.6f)
        for (float t = -hw; t <= hw; t += 0.6f) {
            int x = (int)(x0 + ux*s + nx*t + 0.5f);
            int y = (int)(y0 + uy*s + ny*t + 0.5f);
            if ((unsigned)x < GAUGE_SPR_MAX && (unsigned)y < GAUGE_SPR_MAX)
                buf[y * GAUGE_SPR_PITCH + x] = val;
        }
}

/* bounding box of the needle at a given speed, sprite-local origin */
static void needle_bbox(float mph, int *minx, int *miny, int *w, int *h) {
    float a = mph_angle(mph), ca = cosf(a), sa = sinf(a);
    float ex0 = 36.0f, ex1 = 280.0f;
    *minx = (int)floorf(fminf(ex0*ca, ex1*ca)) - 7;
    int maxx = (int)ceilf(fmaxf(ex0*ca, ex1*ca)) + 7;
    *miny = (int)floorf(fminf(ex0*sa, ex1*sa)) - 7;
    int maxy = (int)ceilf(fmaxf(ex0*sa, ex1*sa)) + 7;
    *w = maxx - *minx + 1;
    *h = maxy - *miny + 1;
    if (*w > GAUGE_SPR_MAX) *w = GAUGE_SPR_MAX;
    if (*h > GAUGE_SPR_MAX) *h = GAUGE_SPR_MAX;
}

/* draw (or erase, with zero values) the needle in its own bbox frame */
static void needle_walk(uint8_t *buf, float mph, uint8_t core, uint8_t edge) {
    int minx, miny, w, h;
    needle_bbox(mph, &minx, &miny, &w, &h);
    float a = mph_angle(mph), ca = cosf(a), sa = sinf(a);
    float ox = (float)-minx, oy = (float)-miny;
    needle_span(buf, ox + 37.0f*ca, oy + 37.0f*sa, ox + 170.0f*ca, oy + 170.0f*sa, 9.0f, edge);
    needle_span(buf, ox + 170.0f*ca, oy + 170.0f*sa, ox + 276.0f*ca, oy + 276.0f*sa, 4.5f, edge);
    needle_span(buf, ox + 38.0f*ca, oy + 38.0f*sa, ox + 170.0f*ca, oy + 170.0f*sa, 6.2f, core);
    needle_span(buf, ox + 170.0f*ca, oy + 170.0f*sa, ox + 272.0f*ca, oy + 272.0f*sa, 2.2f, core);
}

void Gauge_DrawNeedleSprite(uint8_t *buf, float mph, float prev_mph,
                            int *out_x, int *out_y, int *out_w, int *out_h) {
    if (prev_mph >= 0.0f)                          /* erase previous shape */
        needle_walk(buf, prev_mph, 0x00, 0x00);
    needle_walk(buf, mph, 0xF0 | SPN_CORE, 0xF0 | SPN_EDGE);

    int minx, miny, w, h;
    needle_bbox(mph, &minx, &miny, &w, &h);
    *out_x = W / 2 + minx;
    *out_y = H / 2 + miny;
    *out_w = w;
    *out_h = h;
}
