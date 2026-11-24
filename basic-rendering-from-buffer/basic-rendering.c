#define EXPORT(name) __attribute__((export_name(name)))

#define W 1920 // 320
#define H 1080 // 180

#define NULL ((void *)0)

// clang --target=wasm32 -ffreestanding -nostdlib -O2 "-Wl,--no-entry" -o basic-rendering-from-buffer/basic-rendering.wasm basic-rendering-from-buffer/basic-rendering.c

typedef unsigned int size_t;
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef struct {
    uint8_t R, G, B, A;
} RGBA;

static RGBA fb[H][W];

EXPORT("get_fb")
RGBA *get_fb(void) {
    return &fb[0][0];
}

EXPORT("fb_w") int fb_w(void) { return W; }
EXPORT("fb_h") int fb_h(void) { return H; }

static uint32_t clock = 0;
static uint32_t pixels = 0;

static RGBA fragment(int x, int y) {
    return (RGBA){ pixels++%255, 255, clock%255, 255 };
}

EXPORT("render")
void render(void) {
    for (size_t y = 0; y < H; y++) {
        for (size_t x = 0; x < W; x++) {
            fb[y][x] = fragment(x, y);
        }   
    }
    clock++;
}

