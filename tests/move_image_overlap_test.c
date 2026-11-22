/* MoveImage between overlapping VRAM rectangles, as the PS1 GPU copies them.
 *
 * The GPU copies row by row: a row reads what earlier rows of the same move
 * wrote, and within a row the source is read before it is written. The SDL_GPU
 * backend copied an overlapping move within one texture, which Vulkan, D3D12
 * and Metal leave undefined when the regions overlap (a horizontal move); it
 * now bounces each row through its sampling copy.
 *
 * Run with PSYZ_HEADLESS=1 (software rasterizer) and PSYZ_HEADLESS=gpu
 * (hidden SDL_GPU window); the GPU variant skips when no device is available. */
#include <libgpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEST_SKIPPED = 77, AREA_X = 512, AREA_Y = 0, AREA_W = 128, AREA_H = 96 };

void Psyz_PadsOverride(int p, char* f, int n) { (void)p; (void)f; (void)n; }

static u_short expected[AREA_H][AREA_W];
static _Alignas(8) u_short actual[AREA_H][AREA_W];

static u_short patternAt(int x, int y) { return (u_short)((x * 7 + y * 131 + 1) & 0x7fff); }

static void load(void)
{
    static _Alignas(8) u_short pixels[AREA_H][AREA_W];
    for (int y = 0; y < AREA_H; y++)
        for (int x = 0; x < AREA_W; x++) expected[y][x] = pixels[y][x] = patternAt(x, y);
    RECT rect = { AREA_X, AREA_Y, AREA_W, AREA_H };
    LoadImage(&rect, (u_long*)pixels); DrawSync(0);
}

/* Moves within the test area, in area coordinates, on the GPU and in the model. */
static void move(int x, int y, int w, int h, int toX, int toY)
{
    RECT rect = { (short)(AREA_X + x), (short)(AREA_Y + y), (short)w, (short)h };
    MoveImage(&rect, AREA_X + toX, AREA_Y + toY); DrawSync(0);
    for (int row = 0; row < h; row++) memmove(&expected[toY + row][toX], &expected[y + row][x], (size_t)w * sizeof(u_short));
}

static int compare(const char* what)
{
    RECT rect = { AREA_X, AREA_Y, AREA_W, AREA_H };
    StoreImage(&rect, (u_long*)actual); DrawSync(0);
    for (int y = 0; y < AREA_H; y++)
        for (int x = 0; x < AREA_W; x++)
            if ((actual[y][x] & 0x7fff) != expected[y][x]) {
                fprintf(stderr, "%s: pixel (%d,%d) is %04x, expected %04x\n", what, x, y, actual[y][x], expected[y][x]);
                return 0;
            }
    return 1;
}

int main(void)
{
    ResetGraph(0);
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 240); PutDrawEnv(&env); DrawSync(0);

    load();
    if (!compare("upload")) {
        const char* headless = getenv("PSYZ_HEADLESS");
        if (headless && strcmp(headless, "gpu") == 0) {
            puts("move image: no hidden GPU device stored the uploaded image; skipped");
            return TEST_SKIPPED;
        }
        return 1;
    }
    move(0, 0, 64, 16, 8, 0);   /* right within the same rows */
    if (!compare("horizontal right")) return 1;
    move(24, 16, 64, 16, 4, 16); /* left within the same rows */
    if (!compare("horizontal left")) return 1;
    move(0, 32, 32, 32, 0, 40);  /* down: later rows read rows this move already wrote */
    if (!compare("vertical down")) return 1;
    move(64, 48, 32, 32, 70, 42); /* up and right */
    if (!compare("diagonal up")) return 1;
    puts("move image: overlapping moves copy row by row as on the PS1 OK");
    return 0;
}
