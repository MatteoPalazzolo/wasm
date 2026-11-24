// game.c - dino runner con rendering interamente in C (framebuffer RGBA).
//
// Build (clang puro, niente libc):
//   clang --target=wasm32 -O2 -nostdlib -mbulk-memory \
//         -Wl,--no-entry -o game.wasm game.c
//
// Il JS fa solo: loop, input, putImageData del framebuffer.

#include <stdint.h>

#define W          600
#define H          150
#define GROUND_Y   130
#define DINO_X     30
#define DINO_W     16
#define DINO_H     16
#define CACTUS_W   9
#define CACTUS_H   16
#define MAX_OBS    4
#define STEP       (1.0f / 60.0f)   // timestep fisso della fisica
#define GRAVITY    1800.0f          // px/s^2
#define JUMP_V     380.0f           // px/s

// Colori: ImageData vuole byte R,G,B,A -> uint32 little-endian = 0xAABBGGRR
#define RGB(rr, gg, bb) (0xFF000000u | ((uint32_t)(bb) << 16) | ((uint32_t)(gg) << 8) | (uint32_t)(rr))
#define COL_BG   RGB(247, 247, 247)
#define COL_FG   RGB(83, 83, 83)
#define COL_DIM  RGB(172, 172, 172)
#define COL_DEAD RGB(200, 50, 50)

#define EXPORT(name) __attribute__((export_name(name)))

// ---------------------------------------------------------------------------
// Sprite: '#' = pixel pieno, 'o' = occhio, qualsiasi altro char = trasparente.
// ---------------------------------------------------------------------------

#define DINO_BODY \
    "........######..", \
    ".......########.", \
    ".......##o#####.", \
    ".......########.", \
    ".......########.", \
    ".......#####....", \
    "#......#######..", \
    "#.....#####.#...", \
    "##...#######....", \
    "###.#########...", \
    "#############...", \
    ".############...", \
    "..##########....", \
    "...########....."

static const char dino_run0[DINO_H][DINO_W + 1] = {
    DINO_BODY, "....###..###....", "....###.........."
};
static const char dino_run1[DINO_H][DINO_W + 1] = {
    DINO_BODY, "....###..###....", ".........###...."
};
static const char dino_jump[DINO_H][DINO_W + 1] = {
    DINO_BODY, "....###..###....", "....###..###...."
};

static const char cactus[CACTUS_H][CACTUS_W + 1] = {
    "....#....",
    "...###...",
    "...###..#",
    "#..###..#",
    "#..###..#",
    "#..###..#",
    "##.###.##",
    ".#######.",
    "..#####..",
    "...###...",
    "...###...",
    "...###...",
    "...###...",
    "...###...",
    "...###...",
    "...###...",
};

// Font 3x5: 0-9 cifre, 10 = 'H', 11 = 'I'
static const char font[12][5][4] = {
    {"###", "#.#", "#.#", "#.#", "###"},
    {".#.", "##.", ".#.", ".#.", "###"},
    {"###", "..#", "###", "#..", "###"},
    {"###", "..#", "###", "..#", "###"},
    {"#.#", "#.#", "###", "..#", "..#"},
    {"###", "#..", "###", "..#", "###"},
    {"###", "#..", "###", "#.#", "###"},
    {"###", "..#", "..#", "..#", "..#"},
    {"###", "#.#", "###", "#.#", "###"},
    {"###", "#.#", "###", "..#", "###"},
    {"#.#", "#.#", "###", "#.#", "#.#"},
    {"###", ".#.", ".#.", ".#.", "###"},
};

// ---------------------------------------------------------------------------
// Stato
// ---------------------------------------------------------------------------

typedef struct { float x; int n; int active; } Obs;

typedef struct {
    float dy, vy;          // altezza dal suolo (px, verso l'alto) e velocita'
    float speed, dist, scroll, anim, acc;
    float since, gap;      // spazio percorso dall'ultimo spawn / prossimo gap
    int on_ground, game_over, hi;
    Obs obs[MAX_OBS];
} Game;

static Game g;
static uint32_t fb[W * H];
static uint32_t rng = 2463534242u;

static uint32_t rnd(void) {            // xorshift32
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static uint32_t hash(uint32_t v) {
    v *= 2654435761u; v ^= v >> 15; v *= 2246822519u; v ^= v >> 13;
    return v;
}

static int obs_w(int n) { return n * CACTUS_W + (n - 1) * 2; }
static int score(void)  { int s = (int)(g.dist / 10.0f); return s > 99999 ? 99999 : s; }

static void reset(void) {
    Game z = {0};
    z.hi = g.hi;
    z.speed = 150.0f;
    z.gap = 250.0f;
    z.on_ground = 1;
    g = z;
}

// ---------------------------------------------------------------------------
// Disegno
// ---------------------------------------------------------------------------

static void rect(int x, int y, int w, int h, uint32_t col) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= H) continue;
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < W) fb[j * W + i] = col;
    }
}

// Disegna una matrice di char. 'k' = fattore di scala, 'tint' != 0 sovrascrive il colore del corpo.
static void blit(const char *s, int sw, int sh, int stride, int px, int py, int k, uint32_t tint) {
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            char c = s[y * stride + x];
            if (c != '#' && c != 'o') continue;
            uint32_t col = (c == 'o') ? COL_BG : (tint ? tint : COL_FG);
            rect(px + x * k, py + y * k, k, k, col);
        }
    }
}

static void glyph(int idx, int x, int y, uint32_t col) {
    blit(&font[idx][0][0], 3, 5, 4, x, y, 2, col);
}

static void number(int v, int digits, int x, int y, uint32_t col) {
    for (int i = digits - 1; i >= 0; i--) {
        glyph(v % 10, x + i * 8, y, col);
        v /= 10;
    }
}

static void render(void) {
    for (int i = 0; i < W * H; i++) fb[i] = COL_BG;

    // terreno + sassolini che scorrono
    rect(0, GROUND_Y, W, 1, COL_FG);
    for (int x = 0; x < W; x++) {
        uint32_t h = hash((uint32_t)(x + (int)g.scroll));
        if (h % 41 == 0) rect(x, GROUND_Y + 3 + (int)((h >> 8) % 8), 1, 1, COL_DIM);
    }

    // ostacoli
    for (int i = 0; i < MAX_OBS; i++) {
        if (!g.obs[i].active) continue;
        for (int k = 0; k < g.obs[i].n; k++)
            blit(&cactus[0][0], CACTUS_W, CACTUS_H, CACTUS_W + 1,
                 (int)g.obs[i].x + k * (CACTUS_W + 2), GROUND_Y - CACTUS_H, 1, 0);
    }

    // dino
    const char *spr;
    if (!g.on_ground || g.game_over) spr = &dino_jump[0][0];
    else spr = (((int)(g.anim * 10.0f)) & 1) ? &dino_run1[0][0] : &dino_run0[0][0];
    blit(spr, DINO_W, DINO_H, DINO_W + 1, DINO_X, GROUND_Y - DINO_H - (int)g.dy, 1,
         g.game_over ? COL_DEAD : 0);

    // punteggio (destra) e record (a sinistra del punteggio)
    number(score(), 5, W - 10 - 40, 8, COL_FG);
    glyph(10, 440, 8, COL_DIM);              // H
    glyph(11, 448, 8, COL_DIM);              // I
    number(g.hi, 5, 464, 8, COL_DIM);
}

// ---------------------------------------------------------------------------
// Logica (un passo da 1/60 s)
// ---------------------------------------------------------------------------

static void step(void) {
    if (g.game_over) return;

    if (g.speed < 420.0f) g.speed += 0.3f;
    g.dist   += g.speed * STEP;
    g.scroll += g.speed * STEP;
    g.anim   += STEP;

    if (!g.on_ground) {
        g.vy -= GRAVITY * STEP;
        g.dy += g.vy * STEP;
        if (g.dy <= 0.0f) { g.dy = 0.0f; g.vy = 0.0f; g.on_ground = 1; }
    }

    // spawn: il gap cresce con la velocita' cosi' c'e' sempre il tempo di atterrare e risaltare
    g.since += g.speed * STEP;
    if (g.since >= g.gap) {
        for (int i = 0; i < MAX_OBS; i++) {
            if (g.obs[i].active) continue;
            g.obs[i].active = 1;
            g.obs[i].x = (float)W;
            g.obs[i].n = 1 + (int)(rnd() % 3);
            break;
        }
        g.since = 0.0f;
        g.gap = g.speed * 0.45f + 80.0f + (float)(rnd() % 120);
    }

    // hitbox del dino (piu' piccola dello sprite)
    int top = GROUND_Y - DINO_H - (int)g.dy;
    int ax = DINO_X + 3, ay = top + 3, aw = 10, ah = 13;

    for (int i = 0; i < MAX_OBS; i++) {
        Obs *o = &g.obs[i];
        if (!o->active) continue;
        o->x -= g.speed * STEP;
        int w = obs_w(o->n);
        if (o->x + w < 0) { o->active = 0; continue; }

        int bx = (int)o->x + 1, by = GROUND_Y - CACTUS_H + 1, bw = w - 2, bh = CACTUS_H - 1;
        if (ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by) {
            g.game_over = 1;
            int s = score();
            if (s > g.hi) g.hi = s;
        }
    }
}

// ---------------------------------------------------------------------------
// API esportata verso JS
// ---------------------------------------------------------------------------

EXPORT("init")
void init(uint32_t seed) {
    rng = seed ? seed : 2463534242u;
    g.hi = 0;
    reset();
    render();
}

EXPORT("press")                      // salto, oppure restart se sei morto
void press(void) {
    if (g.game_over) { reset(); return; }
    if (g.on_ground) { g.vy = JUMP_V; g.on_ground = 0; }
}

EXPORT("update")                     // dt in secondi; accumula e fa passi fissi
void update(float dt) {
    if (dt > 0.25f) dt = 0.25f;
    g.acc += dt;
    while (g.acc >= STEP) { step(); g.acc -= STEP; }
    render();
}

EXPORT("fb_ptr")    uint32_t *fb_ptr(void)   { return fb; }
EXPORT("width")     int width(void)          { return W; }
EXPORT("height")    int height(void)         { return H; }
EXPORT("is_over")   int is_over(void)        { return g.game_over; }
