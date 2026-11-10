// Cobra Arena 2P - slither.io offline, 2 jogadores locais (pads 1 e 2), PSL1GHT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>
#include <audio/audio.h>

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)

#define WORLD        3200.0f
#define MAXSEG       420
#define NSN          10      /* total de cobras */
#define NHUM         2       /* cobras de jogadores: 0 = P1, 1 = P2 */
#define NPEL         500
#define SP           7.0f
#define PI           3.14159265f
#define DEADZONE     0.2f
#define RESPAWN_BOT  240
#define RESTART_WAIT 60

#define AUD_RATE     48000.0f
#define AUD_BLOCKS   8
#define AUD_SAMPLES  256

typedef struct {
    float x[MAXSEG], y[MAXSEG];
    int len, alive, respawn, boost, grow, score, tick, dead_t;
    float ang, tgt;
    u32 col;
} Snake;

typedef struct {
    float x, y;
    int size, active;
    u32 col;
} Pellet;

static const u32 palette[8] = {
    0x00FF4D4D, 0x0055D6FF, 0x0066FF66, 0x00FFD23F,
    0x00C77DFF, 0x00FF8FAB, 0x0000E5C0, 0x00FF9F43
};

/* ---------- video ---------- */
static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static void init_screen(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);

    videoState st;
    videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if (videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        resid = VIDEO_RESOLUTION_720;

    videoResolution res;
    videoGetResolution(resid, &res);
    W = res.width;
    H = res.height;

    videoConfiguration vc;
    memset(&vc, 0, sizeof(vc));
    vc.resolution = resid;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.aspect = VIDEO_ASPECT_AUTO;
    vc.pitch = W * 4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);
    videoGetState(VIDEO_PRIMARY, 0, &st);

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    for (int i = 0; i < 2; i++) {
        fb[i] = (u32 *)rsxMemalign(64, W * H * 4);
        gcmAddressToOffset(fb[i], &fboff[i]);
        gcmSetDisplayBuffer(i, fboff[i], W * 4, W, H);
    }
    back = fb[0];
    gcmResetFlipStatus();
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

/* o desenho vai direto no buffer da tela: sem copia extra por quadro */
static void present(void)
{
    gcmSetFlip(ctx, cur);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur ^= 1;
    first_flip = 0;
}

/* ---------- desenho ---------- */
static void fill_rect(int x, int y, int w, int h, u32 c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w, y1 = y + h;
    if (x1 > (int)W) x1 = W;
    if (y1 > (int)H) y1 = H;
    for (int yy = y0; yy < y1; yy++) {
        u32 *p = back + yy * W;
        for (int xx = x0; xx < x1; xx++) p[xx] = c;
    }
}

static void circle(int cx, int cy, int r, u32 c)
{
    if (r < 1) r = 1;
    if (cx + r < 0 || cx - r >= (int)W || cy + r < 0 || cy - r >= (int)H) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= (int)H) continue;
        int hw = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - hw, x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= (int)W) x1 = W - 1;
        u32 *p = back + y * W;
        for (int x = x0; x <= x1; x++) p[x] = c;
    }
}

/* fonte 3x5 minima */
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"},
    {'2', "###" "..#" "###" "#.." "###"}, {'3', "###" "..#" "###" "..#" "###"},
    {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
    {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" "..#" "..#" "..#"},
    {'8', "###" "#.#" "###" "#.#" "###"}, {'9', "###" "#.#" "###" "..#" "###"},
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'B', "##." "#.#" "##." "#.#" "##."},
    {'C', "###" "#.." "#.." "#.." "###"}, {'E', "###" "#.." "###" "#.." "###"},
    {'G', "###" "#.." "#.#" "#.#" "###"}, {'M', "#.#" "###" "###" "#.#" "#.#"},
    {'O', "###" "#.#" "#.#" "#.#" "###"}, {'P', "###" "#.#" "###" "#.." "#.."},
    {'R', "##." "#.#" "##." "#.#" "#.#"}, {'S', "###" "#.." "###" "..#" "###"},
    {'T', "###" ".#." ".#." ".#." ".#."}, {'U', "#.#" "#.#" "#.#" "#.#" "###"},
    {'V', "#.#" "#.#" "#.#" "#.#" ".#."}, {'X', "#.#" "#.#" ".#." "#.#" "#.#"},
};

static const char *find_glyph(char c)
{
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
        if (glyphs[i].c == c) return glyphs[i].p;
    return NULL;
}

static int text_width(const char *s, int sc) { return (int)strlen(s) * 4 * sc - sc; }

static void draw_text(const char *s, int x, int y, int sc, u32 c)
{
    for (; *s; s++) {
        const char *g = find_glyph(*s);
        if (g)
            for (int r = 0; r < 5; r++)
                for (int k = 0; k < 3; k++)
                    if (g[r * 3 + k] == '#') fill_rect(x + k * sc, y + r * sc, sc, sc, c);
        x += 4 * sc;
    }
}

/* ---------- som (sintetizado, sem arquivos) ---------- */
static audioPortConfig aud_cfg;
static u32 aud_port;
static u32 aud_next = 0;
static int aud_ok = 0;
static float aud_phase = 0, aud_freq = 0;
static int aud_left = 0;

static void audio_init(void)
{
    audioInit();
    audioPortParam p;
    memset(&p, 0, sizeof(p));
    p.numChannels = AUDIO_PORT_2CH;
    p.numBlocks   = AUDIO_BLOCK_8;
    p.attr        = 0;
    p.level       = 1.0f;
    if (audioPortOpen(&p, &aud_port) != 0) return;
    audioGetPortConfig(aud_port, &aud_cfg);
    audioPortStart(aud_port);
    aud_ok = 1;
}

/* toca um tom: freq em Hz, frames em amostras (48000 = 1 segundo) */
static void beep(float freq, int frames)
{
    aud_freq = freq;
    aud_left = frames;
}

static void fill_block(u32 blk)
{
    float *out = (float *)((u8 *)aud_cfg.addr + blk * AUD_SAMPLES * 2 * sizeof(float));
    for (int i = 0; i < AUD_SAMPLES; i++) {
        float v = 0;
        if (aud_left > 0) {
            v = sinf(aud_phase) * 0.15f;
            aud_phase += 2 * PI * aud_freq / AUD_RATE;
            if (aud_phase > 2 * PI) aud_phase -= 2 * PI;
            aud_left--;
        }
        out[2 * i]     = v;
        out[2 * i + 1] = v;
    }
}

/* chamar a cada quadro: mantem 2 blocos a frente da reproducao */
static void audio_pump(void)
{
    if (!aud_ok) return;
    audioGetPortConfig(aud_port, &aud_cfg);
    u32 target = (aud_cfg.readIndex + 2) % AUD_BLOCKS;
    while (aud_next != target) {
        fill_block(aud_next);
        aud_next = (aud_next + 1) % AUD_BLOCKS;
    }
}

/* ---------- jogo ---------- */
static Snake sn[NSN];
static Pellet pel[NPEL];
static int pelrot = 0;
static float camx = WORLD / 2, camy = WORLD / 2;

/* estado de cada controle (P1 = 0, P2 = 1) */
static int pad_on[NHUM];
static float pad_ax[NHUM], pad_ay[NHUM], pad_mag[NHUM];
static int pad_boost[NHUM], pad_x[NHUM], pad_xprev[NHUM];

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static int is_human(int i) { return i < NHUM && pad_on[i]; }

static float radius_of(const Snake *s)
{
    int l = s->len > 240 ? 240 : s->len;
    return 9.0f + l * 0.035f;
}

static void add_pellet(float x, float y, int size)
{
    for (int k = 0; k < NPEL; k++) {
        int i = (pelrot + k) % NPEL;
        if (!pel[i].active) {
            pel[i].x = x; pel[i].y = y; pel[i].size = size; pel[i].active = 1;
            pel[i].col = palette[rand() % 8];
            pelrot = (i + 1) % NPEL;
            return;
        }
    }
}

static void spawn_snake(int i)
{
    Snake *s = &sn[i];
    float x = 400 + frand() * (WORLD - 800), y = 400 + frand() * (WORLD - 800);
    s->ang = frand() * 2 * PI;
    s->tgt = s->ang;
    s->len = 10;
    for (int j = 0; j < s->len; j++) {
        s->x[j] = x - cosf(s->ang) * SP * j;
        s->y[j] = y - sinf(s->ang) * SP * j;
    }
    s->alive = 1; s->respawn = 0; s->boost = 0; s->grow = 0; s->tick = 0; s->dead_t = 0;
    s->col = palette[i % 8];
}

static void kill_snake(int i)
{
    Snake *s = &sn[i];
    for (int j = 0; j < s->len; j += 2)
        add_pellet(s->x[j] + (frand() - 0.5f) * 10, s->y[j] + (frand() - 0.5f) * 10, 2);
    s->alive = 0;
    s->respawn = RESPAWN_BOT;
    s->dead_t = 0;
    if (is_human(i)) beep(140.0f, 12000);
}

static void bot_think(int i)
{
    Snake *s = &sn[i];
    float hx = s->x[0], hy = s->y[0];
    s->tgt += (frand() - 0.5f) * 0.25f;

    if (s->tick % 6 == 0) {
        float best = 380.0f * 380.0f;
        int bi = -1;
        for (int k = 0; k < NPEL; k++) {
            if (!pel[k].active) continue;
            float dx = pel[k].x - hx, dy = pel[k].y - hy, d = dx * dx + dy * dy;
            if (d < best) { best = d; bi = k; }
        }
        if (bi >= 0) s->tgt = atan2f(pel[bi].y - hy, pel[bi].x - hx);
    }

    if (hx < 250 || hx > WORLD - 250 || hy < 250 || hy > WORLD - 250)
        s->tgt = atan2f(WORLD / 2 - hy, WORLD / 2 - hx);

    float px = hx + cosf(s->ang) * 55, py = hy + sinf(s->ang) * 55;
    float rs = radius_of(s);
    for (int o = 0; o < NSN; o++) {
        if (o == i || !sn[o].alive) continue;
        float lim = rs + radius_of(&sn[o]) + 25;
        lim *= lim;
        for (int j = 0; j < sn[o].len; j += 2) {
            float dx = sn[o].x[j] - px, dy = sn[o].y[j] - py;
            if (dx * dx + dy * dy < lim) { s->tgt = s->ang + 1.6f; return; }
        }
    }
}

static void update_snake(int i)
{
    Snake *s = &sn[i];
    if (!s->alive) {
        if (is_human(i)) {
            s->dead_t++;                 /* jogador humano: espera o X */
        } else if (--s->respawn <= 0) {
            spawn_snake(i);              /* bot: volta sozinho */
        }
        return;
    }
    s->tick++;

    if (is_human(i)) {
        if (pad_mag[i] > 0.3f) s->tgt = atan2f(pad_ay[i], pad_ax[i]);
        s->boost = pad_boost[i];
    } else {
        bot_think(i);
        s->boost = (s->len > 30 && (s->tick / 90) % 5 == 0) ? 1 : 0;
    }

    float d = s->tgt - s->ang;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    if (d > 0.075f) d = 0.075f;
    if (d < -0.075f) d = -0.075f;
    s->ang += d;

    float sp = 3.0f;
    if (s->boost && s->len > 12) {
        sp = 5.4f;
        if (s->tick % 10 == 0) {
            s->len--;
            add_pellet(s->x[s->len], s->y[s->len], 1);
        }
    }
    s->x[0] += cosf(s->ang) * sp;
    s->y[0] += sinf(s->ang) * sp;
    for (int j = 1; j < s->len; j++) {
        float dx = s->x[j - 1] - s->x[j], dy = s->y[j - 1] - s->y[j];
        float dd = sqrtf(dx * dx + dy * dy);
        if (dd > SP) {
            float f = (dd - SP) / dd;
            s->x[j] += dx * f;
            s->y[j] += dy * f;
        }
    }

    if (s->x[0] < 0 || s->x[0] > WORLD || s->y[0] < 0 || s->y[0] > WORLD) { kill_snake(i); return; }

    float rs = radius_of(s), er = rs + 12;
    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        float dx = pel[k].x - s->x[0], dy = pel[k].y - s->y[0];
        if (dx * dx + dy * dy < er * er) {
            pel[k].active = 0;
            s->grow += pel[k].size;
            s->score += pel[k].size * 5;
            if (is_human(i)) beep(660.0f, 1500);
        }
    }
    while (s->grow >= 2 && s->len < MAXSEG) {
        s->x[s->len] = s->x[s->len - 1];
        s->y[s->len] = s->y[s->len - 1];
        s->len++;
        s->grow -= 2;
    }
    if (s->len >= MAXSEG) s->grow = 0;

    for (int o = 0; o < NSN; o++) {
        if (o == i || !sn[o].alive) continue;
        float lim = (rs + radius_of(&sn[o])) * 0.75f;
        lim *= lim;
        for (int j = 0; j < sn[o].len; j++) {
            float dx = sn[o].x[j] - s->x[0], dy = sn[o].y[j] - s->y[0];
            if (dx * dx + dy * dy < lim) { kill_snake(i); return; }
        }
    }
}

static void init_game(void)
{
    memset(pel, 0, sizeof(pel));
    for (int k = 0; k < NPEL; k++) add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 5 == 0));
    for (int i = 0; i < NSN; i++) { sn[i].score = 0; spawn_snake(i); }
}

static void read_pad(void)
{
    padInfo info;
    ioPadGetInfo(&info);
    for (int h = 0; h < NHUM; h++) {
        pad_ax[h] = pad_ay[h] = pad_mag[h] = 0;
        pad_boost[h] = pad_x[h] = 0;
        pad_on[h] = info.status[h] ? 1 : 0;
        if (!pad_on[h]) continue;

        padData d;
        ioPadGetData(h, &d);

        float ax = ((int)d.ANA_L_H - 128) / 128.0f;
        float ay = ((int)d.ANA_L_V - 128) / 128.0f;
        if (fabsf(ax) < DEADZONE) ax = 0;
        if (fabsf(ay) < DEADZONE) ay = 0;
        if (d.BTN_LEFT)  ax = -1;
        if (d.BTN_RIGHT) ax =  1;
        if (d.BTN_UP)    ay = -1;
        if (d.BTN_DOWN)  ay =  1;

        pad_ax[h] = ax;
        pad_ay[h] = ay;
        pad_mag[h] = sqrtf(ax * ax + ay * ay);
        pad_boost[h] = (d.BTN_CROSS || d.BTN_R1 || d.BTN_R2) ? 1 : 0;
        pad_x[h] = d.BTN_CROSS ? 1 : 0;
    }
}

static void update_game(void)
{
    for (int i = 0; i < NSN; i++) update_snake(i);

    int count = 0;
    for (int k = 0; k < NPEL; k++) count += pel[k].active;
    for (int n = 0; n < 2 && count < NPEL - 40; n++, count++)
        add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 5 == 0));

    /* camera no meio dos jogadores vivos */
    float cx = 0, cy = 0;
    int n = 0;
    for (int h = 0; h < NHUM; h++)
        if (sn[h].alive) { cx += sn[h].x[0]; cy += sn[h].y[0]; n++; }
    if (n > 0) { camx = cx / n; camy = cy / n; }

    /* X reinicia o jogador humano morto (borda de subida) */
    for (int h = 0; h < NHUM; h++) {
        if (!pad_on[h] || sn[h].alive) continue;
        if (sn[h].dead_t > RESTART_WAIT && pad_x[h] && !pad_xprev[h]) {
            spawn_snake(h);
            sn[h].score = 0;
        }
    }
    for (int h = 0; h < NHUM; h++) pad_xprev[h] = pad_x[h];
}

static void render(void)
{
    float sc = (float)W / 1280.0f;
    float hw = W / 2.0f, hh = H / 2.0f;
    u32 n = W * H;
    for (u32 i = 0; i < n; i++) back[i] = 0x000E1420;

    /* fora da arena */
    int sx0 = (int)((0 - camx) * sc + hw), sx1 = (int)((WORLD - camx) * sc + hw);
    int sy0 = (int)((0 - camy) * sc + hh), sy1 = (int)((WORLD - camy) * sc + hh);
    u32 red = 0x00380E12;
    if (sx0 > 0) fill_rect(0, 0, sx0, H, red);
    if (sx1 < (int)W) fill_rect(sx1, 0, W - sx1, H, red);
    if (sy0 > 0) fill_rect(0, 0, W, sy0, red);
    if (sy1 < (int)H) fill_rect(0, sy1, W, H - sy1, red);

    /* pontos de fundo para dar sensacao de movimento */
    float gx0 = floorf((camx - hw / sc) / 100.0f) * 100.0f;
    float gy0 = floorf((camy - hh / sc) / 100.0f) * 100.0f;
    for (float gx = gx0; gx < camx + hw / sc; gx += 100.0f) {
        if (gx < 0 || gx > WORLD) continue;
        for (float gy = gy0; gy < camy + hh / sc; gy += 100.0f) {
            if (gy < 0 || gy > WORLD) continue;
            fill_rect((int)((gx - camx) * sc + hw), (int)((gy - camy) * sc + hh), 4, 4, 0x00203047);
        }
    }

    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        int px = (int)((pel[k].x - camx) * sc + hw), py = (int)((pel[k].y - camy) * sc + hh);
        circle(px, py, (int)((4 + pel[k].size * 2) * sc), pel[k].col);
    }

    for (int i = NSN - 1; i >= 0; i--) {
        Snake *s = &sn[i];
        if (!s->alive) continue;
        int r = (int)(radius_of(s) * sc);
        u32 dark = (s->col >> 1) & 0x007F7F7F;
        for (int j = s->len - 1; j >= 0; j--) {
            int px = (int)((s->x[j] - camx) * sc + hw), py = (int)((s->y[j] - camy) * sc + hh);
            circle(px, py, r, (j % 6 < 3) ? s->col : dark);
        }
        int hx = (int)((s->x[0] - camx) * sc + hw), hy = (int)((s->y[0] - camy) * sc + hh);
        for (int e = -1; e <= 1; e += 2) {
            int ex = hx + (int)(cosf(s->ang + e * 0.7f) * r * 0.6f);
            int ey = hy + (int)(sinf(s->ang + e * 0.7f) * r * 0.6f);
            circle(ex, ey, r * 35 / 100 + 1, 0x00FFFFFF);
            circle(ex + (int)(cosf(s->ang) * r * 0.15f), ey + (int)(sinf(s->ang) * r * 0.15f), r * 18 / 100 + 1, 0x00000000);
        }
    }

    /* HUD */
    int ui = (int)(4 * sc + 0.5f);
    if (ui < 3) ui = 3;
    char buf[32];
    for (int h = 0; h < NHUM; h++) {
        if (h == 1 && !pad_on[1]) break;
        u32 col = sn[h].col;
        int right = (h == 1);

        sprintf(buf, "P%d", h + 1);
        int x0 = right ? (int)W - 24 - text_width(buf, ui + 2) : 24;
        draw_text(buf, x0, 20, ui + 2, col);

        sprintf(buf, "%d", sn[h].score);
        int x1 = right ? (int)W - 24 - text_width(buf, ui + 2) : 24;
        draw_text(buf, x1, 20 + 7 * ui, ui + 2, 0x00FFFFFF);

        int xh = right ? (int)W - 24 - text_width("X TURBO", ui) : 24;
        draw_text("X TURBO", xh, (int)H - 8 * ui, ui, 0x00809BBF);
    }

    /* jogador morto: GAME OVER na propria metade da tela */
    int half = (int)W / 4;
    for (int h = 0; h