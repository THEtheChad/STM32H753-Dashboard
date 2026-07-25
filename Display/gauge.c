/* Retro F-100 speedometer face, rendered once into the L8 framebuffer.
 * Styling: charcoal face, cream ticks/numerals, red tapered needle,
 * chrome bezel. 0-120 MPH over a 240° sweep (0 at lower-left, 60 up top). */

#include "gauge.h"
#include <math.h>

/* ---- palette ---- */
enum {
    C_BLACK = 0,   /* outside face / gaps        */
    C_FACE  = 1,   /* charcoal dial face          */
    C_CREAM = 2,   /* ticks, numerals             */
    C_RED   = 3,   /* needle                      */
    C_CHROME= 4,   /* bezel highlight, needle cap */
    C_GRAY  = 5,   /* hub                         */
    C_SHADOW= 6,   /* hub inner shadow            */
    C_DIM   = 7,   /* dim chrome accents, "MPH"   */
};

const uint32_t Gauge_CLUT[256] = {
    [C_BLACK]  = 0x000000,
    [C_FACE]   = 0x1B1B1D,
    [C_CREAM]  = 0xEFE3C2,
    [C_RED]    = 0xD8321E,
    [C_CHROME] = 0xC9CCD1,
    [C_GRAY]   = 0x63666B,
    [C_SHADOW] = 0x2E2F33,
    [C_DIM]    = 0x8A8D92,
};

/* ---- geometry ---- */
#define SWEEP_START_DEG 150.0f   /* 0 MPH, screen-space angle (y down) */
#define SWEEP_DEG       240.0f
#define MPH_MAX         120.0f

static int W, H;
static uint8_t *FB;

static inline void px(int x, int y, uint8_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) FB[y * W + x] = c;
}

static void draw_ring(int cx, int cy, int r0, int r1, uint8_t c) {
    for (int y = cy - r1; y <= cy + r1; y++)
        for (int x = cx - r1; x <= cx + r1; x++) {
            int dx = x - cx, dy = y - cy, d2 = dx*dx + dy*dy;
            if (d2 >= r0*r0 && d2 <= r1*r1) px(x, y, c);
        }
}

static void draw_disc(int cx, int cy, int r, uint8_t c) { draw_ring(cx, cy, 0, r, c); }

/* thick segment: every pixel within w/2 of the line segment */
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

/* ---- 5x7 bitmap font: digits + M, P, H ---- */
static const uint8_t FONT[13][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},  /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},  /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},  /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},  /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},  /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},  /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},  /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},  /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},  /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},  /* 9 */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},  /* M */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},  /* P */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},  /* H */
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
    return n * 5 * scale + (n - 1) * scale;   /* 1-column gap between glyphs */
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

/* screen-space angle (radians, y down) for a speed value */
static float mph_angle(float mph) {
    return (SWEEP_START_DEG + SWEEP_DEG * (mph / MPH_MAX)) * (3.14159265f / 180.0f);
}

void Gauge_RenderSpeedo(uint8_t *fb, int w, int h, float mph) {
    FB = fb; W = w; H = h;
    const int cx = w / 2, cy = h / 2;

    /* face + bezel, back to front */
    for (int i = 0; i < w * h; i++) fb[i] = C_BLACK;
    draw_disc(cx, cy, 359, C_CHROME);          /* outer chrome bezel      */
    draw_ring(cx, cy, 347, 352, C_DIM);        /* machined bezel groove   */
    draw_disc(cx, cy, 340, C_SHADOW);          /* bezel-to-face step      */
    draw_disc(cx, cy, 334, C_FACE);            /* dial face               */
    draw_ring(cx, cy, 330, 333, C_DIM);        /* thin face edge ring     */

    /* ticks: minors every 5 MPH, majors every 10 */
    for (int v = 0; v <= (int)MPH_MAX; v += 5) {
        float a = mph_angle((float)v), ca = cosf(a), sa = sinf(a);
        int major = (v % 10) == 0;
        float r0 = major ? 288.0f : 306.0f, r1 = 326.0f;
        float wd = major ? 7.0f : 3.0f;
        draw_seg(cx + ca*r0, cy + sa*r0, cx + ca*r1, cy + sa*r1, wd, C_CREAM);
    }

    /* numerals every 20 MPH, upright, inside the ticks */
    for (int v = 0; v <= (int)MPH_MAX; v += 20) {
        float a = mph_angle((float)v);
        char label[4]; int n = 0;
        if (v >= 100) label[n++] = (char)('0' + v / 100);
        if (v >= 10)  label[n++] = (char)('0' + (v / 10) % 10);
        label[n++] = (char)('0' + v % 10);
        label[n] = 0;
        draw_text_center(cx + (int)(cosf(a) * 240.0f),
                         cy + (int)(sinf(a) * 240.0f), label, 4, C_CREAM);
    }

    draw_text_center(cx, cy + 96, "MPH", 3, C_DIM);

    /* needle: tapered, tail through the hub */
    {
        float a = mph_angle(mph), ca = cosf(a), sa = sinf(a);
        draw_seg(cx - ca*46.0f, cy - sa*46.0f, cx + ca*170.0f, cy + sa*170.0f, 10.0f, C_RED);
        draw_seg(cx + ca*170.0f, cy + sa*170.0f, cx + ca*282.0f, cy + sa*282.0f, 5.0f, C_RED);
    }

    /* hub over the needle tail */
    draw_disc(cx, cy, 34, C_GRAY);
    draw_ring(cx, cy, 28, 33, C_SHADOW);
    draw_disc(cx, cy, 12, C_CHROME);
}
