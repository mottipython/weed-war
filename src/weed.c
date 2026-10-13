/*
 * weed-war: screensaver of Lio's weed war from "Anathem".
 *
 * Fraa Lio re-enacts the Battle of Trantae in the meadow by the river, with weeds
 * for armies, and Erasmas draws it through a frame strung with a grid. The weeds
 * really do spread patch by patch; Lio's weeding is what turns it into the battle.
 *
 *   weed-war            full screen on every display; any key or mouse movement quits
 *   weed-war --window   in a window; Esc or Q quits
 *   WeedWar.scr /s      Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, prepared by tools/make_sprites.py) are built
 * into the program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(__APPLE__)
/* Mach-O: its own section names, and C names carry a leading underscore */
#define ART_SECTION ".section __DATA,__const\n"
#define ART_SYM "_art_"
#elif defined(_WIN32)
#define ART_SECTION ".section .rdata,\"dr\"\n"
#define ART_SYM "art_"
#else
#define ART_SECTION ".section .rodata\n"
#define ART_SYM "art_"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".globl " ART_SYM #name "\n" ART_SYM #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".globl " ART_SYM #name "_end\n" ART_SYM #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
ART(meadow, "meadow.jpg")
ART(banner, "banner.png")
ART(star_y, "star_y.png")
ART(star_r, "star_r.png")
ART(star_w, "star_w.png")
ART(cane, "cane.png")
ART(thicket, "thicket.png")
ART(sapling, "sapling.png")
ART(tree, "tree.png")
ART(stake, "stake.png")
ART(lio_weed, "lio_weed.png")
ART(lio_walk1, "lio_walk1.png")
ART(lio_walk2, "lio_walk2.png")
ART(lio_proud, "lio_proud.png")
ART(raz_draw, "raz_draw.png")
ART(raz_doubt, "raz_doubt.png")

enum { SP_MEADOW, SP_BANNER, SP_STAR_Y, SP_STAR_R, SP_STAR_W, SP_CANE, SP_THICKET, SP_SAPLING, SP_TREE, SP_STAKE, SP_LIO_WEED, SP_LIO_WALK1, SP_LIO_WALK2, SP_LIO_PROUD, SP_RAZ_DRAW, SP_RAZ_DOUBT, NSPRITE };
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = {
    {art_meadow, art_meadow_end},
    {art_banner, art_banner_end},
    {art_star_y, art_star_y_end},
    {art_star_r, art_star_r_end},
    {art_star_w, art_star_w_end},
    {art_cane, art_cane_end},
    {art_thicket, art_thicket_end},
    {art_sapling, art_sapling_end},
    {art_tree, art_tree_end},
    {art_stake, art_stake_end},
    {art_lio_weed, art_lio_weed_end},
    {art_lio_walk1, art_lio_walk1_end},
    {art_lio_walk2, art_lio_walk2_end},
    {art_lio_proud, art_lio_proud_end},
    {art_raz_draw, art_raz_draw_end},
    {art_raz_doubt, art_raz_doubt_end},
};
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {20, 18, 14}, INK = {46, 32, 22}, CHORD = {130, 44, 30}, PAPER = {253, 246, 226};
static const Rgb WOOD = {150, 104, 62}, BLOOM = {236, 150, 196}, WINTER = {136, 106, 70};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* a picture stretched over a rectangle; sx0..sx1 picks a slice of it across (0..1) */
static void pic_part(int which, double x, double y, double w, double h, double sx0, double sx1, double tilt, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_Rect src = {(int)(sx0 * sprite[which].w), 0, (int)((sx1 - sx0) * sprite[which].w), sprite[which].h};
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    SDL_FPoint pivot = {(float)(w / 2), (float)h};
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, tilt, &pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* a picture `tall` high, standing with the point (ax, ay) of it (0..1 across and down) at (x, y) */
static void pic(int which, double x, double y, double tall, double ax, double ay)
{
    double w = tall * sprite[which].w / sprite[which].h;
    pic_part(which, x - w * ax, y - tall * ay, w, tall, 0, 1, 0, 0);
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
/* ---- the battle ----
 *
 * Lio's re-enactment of the Battle of Trantae, with plants. The clover meadow is the
 * Plains of Thrania and the river is the Chontus. Starblossom vines run along the
 * ground like light cavalry: yellow for the Sarthians, red and white for their
 * allies. Slashberry comes on behind, slower but better at holding ground, like
 * infantry, and the trees make it permanent.
 *
 * The meadow is a grid of patches. A patch of clover next to starblossom may be
 * taken by it; starblossom that has stood a while with weeds all round turns to
 * slashberry, then to thicket, and at last a tree comes up. Left alone the weeds
 * would simply spread; Lio's weeding and coaxing is what makes it Trantae. That is
 * the plan below: where, at each stage of the campaign, the weeds are allowed to
 * go, and where the starblossom is pulled back out.
 */
#define GW 76
#define GH 40
enum { CLOVER, STAR_Y, STAR_R, STAR_W, CANE, THICKET, SAPLING, TREE };
typedef struct { unsigned char kind, legion; float age, jx, jy, size; } Patch;
static Patch field[GH][GW];
static double T, anim_t, tick_acc;       /* T: seconds into the year */
static double lio_x = 0.5, lio_y = 0.9;  /* where Lio is, across and down the meadow */
static int lio_busy, lio_dir = 1;
static uint64_t seed_state;
#define CAMPAIGN 104.0                   /* seconds from the thaw to the fall of Baz */
#define ROUND 116.0

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double seg_dist(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    double u = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    return hypot(px - ax - dx * u, py - ay - dy * u);
}

/* The stages of the campaign, as fractions of it:
 *   0.00 the Sarthians cross the frozen river and make three bridgeheads
 *   0.10 weeks of manoeuvre; Oxas marches up from the south
 *   0.30 the Sarthian centre gives ground: the false retreat, and Oxas follows
 *   0.44 the wings ride round and close behind him: the pincer
 *   0.62 the pocket is crushed
 *   0.76 the plains are overrun
 */
#define ST_MANOEUVRE 0.10
#define ST_RETREAT 0.30
#define ST_PINCER 0.44
#define ST_POCKET 0.62
#define ST_OVERRUN 0.76
#define ST_END 0.94
static const double WING[2][4][2] = { /* the road each wing takes, across and down */
    {{0.20, 0.00}, {0.16, 0.32}, {0.21, 0.58}, {0.50, 0.70}},
    {{0.80, 0.00}, {0.84, 0.32}, {0.79, 0.58}, {0.50, 0.70}},
};
static double centre_depth(double t)
{
    if (t < ST_RETREAT) return 0.04 + 0.34 * ramp(t, 0, ST_RETREAT);
    if (t < ST_PINCER) return 0.38 - 0.25 * ramp(t, ST_RETREAT, ST_PINCER);
    return 0.13 + 0.14 * ramp(t, ST_PINCER, ST_POCKET);
}
static double pocket_radius(double t) { return 0.25 * (1 - ramp(t, ST_POCKET, ST_OVERRUN)); }
/* may weeds grow at (u, v) by now? u runs across the meadow, v from the river down */
static int weeds_allowed(double u, double v, double t)
{
    if (t >= ST_OVERRUN) return v < 0.74 + 0.3 * ramp(t, ST_OVERRUN, ST_END);
    if (t >= ST_POCKET && v < 0.74 && hypot(u - 0.5, v - 0.42) > pocket_radius(t)) return 1;
    if (seg_dist(u, v, 0.5, 0, 0.5, centre_depth(t)) < 0.115) return 1;
    for (int k = 0; k < 2; k++) {
        const double (*p)[2] = WING[k];
        double first = t < ST_MANOEUVRE ? 0.3 * ramp(t, 0, ST_MANOEUVRE) : 0.3 + 0.7 * ramp(t, ST_MANOEUVRE, ST_RETREAT);
        if (seg_dist(u, v, p[0][0], p[0][1], p[0][0] + (p[1][0] - p[0][0]) * first, p[1][1] * first) < 0.105) return 1;
        double go = ramp(t, ST_PINCER, ST_POCKET) * 2;
        for (int s = 1; s < 3 && go > 0; s++, go -= 1) {
            double f = go > 1 ? 1 : go;
            if (seg_dist(u, v, p[s][0], p[s][1], p[s][0] + (p[s + 1][0] - p[s][0]) * f, p[s][1] + (p[s + 1][1] - p[s][1]) * f) < 0.1) return 1;
        }
    }
    return 0;
}
/* is this clover part of the legions of Oxas? */
static int in_legion(double u, double v, double t)
{
    if (t < ST_MANOEUVRE || t >= ST_OVERRUN) return 0;
    double top = t < ST_RETREAT ? 1.0 - 0.52 * ramp(t, ST_MANOEUVRE, ST_RETREAT) : 0.48 - 0.25 * ramp(t, ST_RETREAT, ST_PINCER);
    if (t >= ST_POCKET && hypot(u - 0.5, v - 0.42) > pocket_radius(t) * 0.8) return 0;
    return seg_dist(u, v, 0.5, top, 0.5, top + 0.3) < 0.085;
}
/* starblossom that has to be pulled out again: the false retreat */
static int pulled_back(double u, double v, double t)
{
    return t >= ST_RETREAT && t < ST_POCKET && fabs(u - 0.5) < 0.2 && v > centre_depth(t) + 0.05 && v < 0.62 &&
           !weeds_allowed(u, v, t);
}

static void new_year(void)
{
    T = 0; tick_acc = 0;
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++) {
            Patch *p = &field[j][i];
            p->kind = CLOVER; p->legion = 0; p->age = 0;
            p->jx = (float)(frand() - 0.5); p->jy = (float)(frand() - 0.5); p->size = (float)(0.85 + 0.3 * frand());
        }
    /* the three bridgeheads on the near bank */
    static const int SEED_KIND[3] = {STAR_R, STAR_Y, STAR_W};
    for (int k = 0; k < 3; k++)
        for (int d = -1; d <= 1; d++) field[0][(int)((0.2 + 0.3 * k) * GW) + d].kind = (unsigned char)SEED_KIND[k];
    lio_x = 0.5; lio_y = 0.92; lio_busy = 0;
}

static int is_star(int k) { return k == STAR_Y || k == STAR_R || k == STAR_W; }
static void tick(double dt)
{
    static unsigned char next[GH][GW];
    double t = T / CAMPAIGN;
    if (t > 1) t = 1;
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++) {
            Patch *p = &field[j][i];
            double u = (i + 0.5) / GW, v = (j + 0.5) / GH;
            int k = p->kind, weeds = 0, star = 0, star_kind = 0;
            for (int dj = -1; dj <= 1; dj++)
                for (int di = -1; di <= 1; di++) {
                    int a = i + di, b = j + dj;
                    if ((!di && !dj) || a < 0 || b < 0 || a >= GW || b >= GH) continue;
                    int q = field[b][a].kind;
                    if (q != CLOVER) weeds++;
                    if (is_star(q)) { star++; if (!star_kind || frand() < 0.4) star_kind = q; }
                }
            next[j][i] = (unsigned char)k;
            p->age += (float)dt;
            p->legion = (unsigned char)(k == CLOVER && in_legion(u, v, t));
            if (k == CLOVER) {
                /* cavalry: quick to take open ground */
                if (star && weeds_allowed(u, v, t) && frand() < 0.34) next[j][i] = (unsigned char)star_kind;
            } else if (is_star(k)) {
                if (pulled_back(u, v, t)) { if (frand() < 0.22) next[j][i] = CLOVER; }
                /* infantry comes up behind, but not into the ground the centre is about to give up */
                else if (p->age > 9 && weeds == 8 && frand() < 0.012 &&
                         !(t < ST_PINCER && fabs(u - 0.5) < 0.2 && v > 0.1)) next[j][i] = CANE;
            } else if (k == CANE) { if (p->age > 14 && weeds == 8 && frand() < 0.008) next[j][i] = THICKET; }
            else if (k == THICKET) { if (p->age > 12 && frand() < 0.0012) next[j][i] = SAPLING; }
            else if (k == SAPLING) { if (p->age > 8 && frand() < 0.03) next[j][i] = TREE; }
        }
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++)
            if (next[j][i] != field[j][i].kind) { field[j][i].kind = next[j][i]; field[j][i].age = 0; }
}

/* where Lio has work to do at this stage of the campaign */
static void lio_goal(double t, double *gx, double *gy)
{
    double wob = sin(anim_t * 0.23) * 0.5;
    if (t < ST_MANOEUVRE) { *gx = 0.5 + wob * 0.6; *gy = 0.1; }
    else if (t < ST_RETREAT) { *gx = 0.5 + wob * 0.6; *gy = centre_depth(t) + 0.08; }
    else if (t < ST_PINCER) { *gx = 0.5 + wob * 0.16; *gy = centre_depth(t) + 0.1; }           /* pulling the centre back */
    else if (t < ST_POCKET) { int k = wob > 0; double f = ramp(t, ST_PINCER, ST_POCKET); *gx = WING[k][2][0] + (0.5 - WING[k][2][0]) * f; *gy = 0.5 + 0.2 * f; }
    else if (t < ST_OVERRUN) { *gx = 0.5 + wob * 0.1; *gy = 0.42; }
    else { *gx = 0.5 + wob * 0.5; *gy = 0.78 + 0.15 * ramp(t, ST_OVERRUN, ST_END); }
}

static void update(double dt)
{
    anim_t += dt;
    dt *= opt_speed;
    T += dt;
    if (T > ROUND) new_year();
    if (T < CAMPAIGN) {
        tick_acc += dt;
        while (tick_acc >= 0.05) { tick_acc -= 0.05; tick(0.05); }
    }
    double gx, gy, t = T / CAMPAIGN;
    lio_goal(t > 1 ? 1 : t, &gx, &gy);
    double dx = gx - lio_x, dy = gy - lio_y, d = hypot(dx, dy * 0.6);
    /* he settles to his weeding and stays at it until the fighting has moved well away */
    static double held;
    held += dt;
    if (!lio_busy && d < 0.03) { lio_busy = 1; held = 0; }
    else if (lio_busy && d > 0.12 && held > 5) { lio_busy = 0; held = 0; }
    if (!lio_busy) { double sp = 0.085 * dt / d; lio_x += dx * sp; lio_y += dy * sp; lio_dir = dx >= 0 ? 1 : -1; }
}

/* ---- drawing ---- */
static double unit;
static void caption(double t, const char **l1, const char **l2)
{
    *l1 = "THE BATTLE OF TRANTAE"; *l2 = "FOUGHT AGAIN, WITH WEEDS";
    if (T > 5) { *l1 = "THE SARTHIANS CROSS THE FROZEN CHONTUS"; *l2 = "YELLOW STARBLOSSOM, WITH RED AND WHITE ALLIES"; }
    if (t >= ST_MANOEUVRE) { *l1 = "WEEKS OF MANOEUVRE"; *l2 = "GENERAL OXAS MARCHES NORTH TO DROWN THEM LIKE RATS"; }
    if (t >= ST_RETREAT) { *l1 = "THE SARTHIAN CENTRE GIVES WAY"; *l2 = "A FALSE RETREAT, AND OXAS FALLS FOR IT"; }
    if (t >= ST_PINCER) { *l1 = "THE PINCER"; *l2 = "STARBLOSSOM RUNS ROUND BOTH FLANKS LIKE LIGHT CAVALRY"; }
    if (t >= ST_POCKET) { *l1 = "OXAS IS SURROUNDED"; *l2 = "PIERCED BY EIGHT ARROWS, HE FALLS ON HIS SWORD"; }
    if (t >= ST_OVERRUN) { *l1 = "THE PLAINS OF THRANIA ARE LOST"; *l2 = "SLASHBERRY HOLDS THE GROUND, AND THE TREES MAKE IT PERMANENT"; }
    if (t >= ST_END) { *l1 = "THREE MONTHS LATER BAZ IS ON FIRE"; *l2 = "NEXT SPRING, LIO WILL DO IT ALL AGAIN"; }
}

static void draw_caption(int w, int h, const char *l1, const char *l2)
{
    double s1 = unit * 0.46, s2 = unit * 0.3;
    double widest = fmax(text_width(l1, s1), text_width(l2, s2)), room = w * 0.6;
    if (widest > room) { s1 *= room / widest; s2 *= room / widest; }
    double bw = fmax(text_width(l1, s1), text_width(l2, s2)) + unit * 13, bh = unit * 11.5;
    double bx = w / 2.0 - bw / 2, by = h - unit * 0.8 - bh;
    double cap = 0.135, cw = bh * 1.15 * sprite[SP_BANNER].w * cap / sprite[SP_BANNER].h;
    rect(bx + cw * 0.6 + unit * 0.5, by + bh * 0.16 + unit * 0.6, bw - cw * 1.2, bh * 0.72, col(INK, 0.3));
    pic_part(SP_BANNER, bx + cw - 1, by, bw - 2 * cw + 2, bh, cap, 1 - cap, 0, 0);
    pic_part(SP_BANNER, bx, by, cw, bh, 0, cap, 0, 0);
    pic_part(SP_BANNER, bx + bw - cw, by, cw, bh, 1 - cap, 1, 0, 0);
    text(l1, w / 2.0, by + bh * 0.25, s1, 0.95, col(INK, 1));
    text(l2, w / 2.0, by + bh * 0.25 + s1 * 6 + unit * 1.2, s2, 0.9, col(CHORD, 1));
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    pic_part(SP_MEADOW, 0, 0, w, h, 0, 1, 0, 0);
    double t = T / CAMPAIGN;
    if (t > 1) t = 1;
    /* the meadow on screen: from the near bank of the river down to the wall we watch from */
    double y0 = h * 0.225, y1 = h * 0.93, cw = (double)w / GW, ch = (y1 - y0) / GH;
    static const int PIC[8] = {0, SP_STAR_Y, SP_STAR_R, SP_STAR_W, SP_CANE, SP_THICKET, SP_SAPLING, SP_TREE};
    static const double BIG[8] = {0, 1.5, 1.5, 1.5, 1.8, 2.3, 2.2, 3.6};
    double lsx = lio_x * w, lsy = y0 + lio_y * (y1 - y0);
    int lio_row = (int)(lio_y * GH), lio_drawn = 0;
    for (int j = 0; j < GH; j++) {
        double near = 0.72 + 0.4 * (j + 0.5) / GH; /* things nearer the wall are bigger */
        for (int i = 0; i < GW; i++) {
            const Patch *p = &field[j][i];
            double x = (i + 0.5 + p->jx * 0.7) * cw, y = y0 + (j + 0.5 + p->jy * 0.6) * ch;
            if (p->kind == CLOVER) {
                if (p->legion) { /* the legions: clover in flower */
                    double q = cw * 0.16 * near;
                    disc(x - cw * 0.2, y, q, col(BLOOM, 1), col(BLOOM, 0.9));
                    disc(x + cw * 0.22, y - ch * 0.2, q, col(BLOOM, 1), col(BLOOM, 0.9));
                    disc(x + cw * 0.02, y + ch * 0.26, q * 0.9, col(BLOOM, 1), col(BLOOM, 0.9));
                }
                continue;
            }
            /* brambles are drawn on every other patch and larger, so the canes can be made out */
            int bramble = p->kind == CANE || p->kind == THICKET;
            if (bramble && ((i + j) & 1)) continue;
            double grown = p->age < 1.2 ? 0.25 + 0.75 * p->age / 1.2 : 1;
            double tall = cw * BIG[p->kind] * (bramble ? 1.45 : 1) * p->size * near * grown;
            if (p->kind == TREE || p->kind == THICKET) disc(x + tall * 0.06, y + tall * 0.08, tall * 0.4, col(INK, 0.22), col(INK, 0));
            pic(PIC[p->kind], x, y + tall * 0.25, tall, 0.5, 1);
        }
        if (j == lio_row && !lio_drawn) { /* Lio, in among it */
            double tall = h * 0.15 * (0.72 + 0.4 * lio_y);
            int which = lio_busy ? (fmod(anim_t, 22) < 19 ? SP_LIO_WEED : SP_LIO_PROUD) : (fmod(anim_t * 2.2, 2) < 1 ? SP_LIO_WALK1 : SP_LIO_WALK2);
            double lw = tall * sprite[which].w / sprite[which].h;
            disc(lsx, lsy, lw * 0.4, col(INK, 0.28), col(INK, 0));
            pic_part(which, lsx - lw / 2, lsy - tall + (lio_busy ? 0 : fabs(sin(anim_t * 2.2 * M_PI)) * unit * 0.3), lw, tall, 0, 1, 0, lio_dir < 0);
            lio_drawn = 1;
        }
    }
    /* the stakes that mark where things happened */
    static const struct { double u, v; const char *name; } STAKE[2] = {{0.5, 0.33, "TRANTAE"}, {0.535, 0.47, "OXAS"}};
    for (int k = 0; k < 2; k++) {
        double x = STAKE[k].u * w, y = y0 + STAKE[k].v * (y1 - y0), tall = h * 0.055;
        pic(SP_STAKE, x, y, tall, 0.5, 1);
        double s = unit * 0.17, bw = text_width(STAKE[k].name, s) + s * 6;
        rect(x - bw / 2 - s * 0.6, y + s * 1.4, bw + s * 1.2, s * 10.2, col(INK, 1));
        rect(x - bw / 2, y + s * 2, bw, s * 9, col(PAPER, 1));
        text(STAKE[k].name, x, y + s * 3.6, s, 1.0, col(INK, 1));
    }
    flush(r);
    /* winter comes: everything browns before the year turns */
    double brown = ramp(T, CAMPAIGN + 2, ROUND - 2);
    if (brown > 0) { rect(0, y0, w, h - y0, col(WINTER, 0.6 * brown)); flush(r); }

    /* Erasmas's frame: a grid of strings, so that each drawing tallies with the last */
    for (int k = 1; k < 8; k++) line(w * k / 8.0, 0, w * k / 8.0, h, unit * 0.1, col(INK, 0.4));
    for (int k = 1; k < 5; k++) line(0, h * k / 5.0, w, h * k / 5.0, unit * 0.1, col(INK, 0.4));
    double fr = unit * 1.6;
    rect(0, 0, w, fr, col(WOOD, 1)); rect(0, h - fr, w, fr, col(WOOD, 1)); rect(0, 0, fr, h, col(WOOD, 1)); rect(w - fr, 0, fr, h, col(WOOD, 1));
    rect(fr, fr, w - 2 * fr, unit * 0.25, col(INK, 0.6)); rect(fr, h - fr - unit * 0.25, w - 2 * fr, unit * 0.25, col(INK, 0.6));
    rect(fr, fr, unit * 0.25, h - 2 * fr, col(INK, 0.6)); rect(w - fr - unit * 0.25, fr, unit * 0.25, h - 2 * fr, col(INK, 0.6));
    /* and Erasmas himself, drawing it */
    int raz = t < 0.06 ? SP_RAZ_DOUBT : SP_RAZ_DRAW;
    pic(raz, w * 0.085, h - fr * 0.6, h * 0.26, 0.5, 1);
    const char *l1, *l2;
    caption(t, &l1, &l2);
    draw_caption(w, h, l1, l2);
    flush(r);
    double fade = fmin(ramp(T, 0, 1.4), 1 - ramp(T, ROUND - 1.5, ROUND - 0.1));
    if (fade < 1) { rect(0, 0, w, h, col(BG, 1 - fade)); flush(r); }
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("The Weed War", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    new_year();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "weed-war: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "weed-war: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Weed War",
        "The Weed War screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Weed War", "The Weed War screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "weed-war: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "weed-war: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}
