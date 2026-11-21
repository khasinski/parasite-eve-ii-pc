/* An empty GP0(E3h)/(E4h) draw area must clip everything, as on PS1.
 *
 * PE2 hides a menu panel's content with SetDrawArea({0, buffer * 272, 0, 0})
 * (src/main/ui.c, _uiLayoutHiddenPanel). The SDK clamps the end to (0, 271) in
 * display buffer 1, an inverted Y range, and to the single pixel (0,0) in
 * buffer 0. The hardware GPU backends ignored the inverted area and kept the
 * previous scissor, so hidden attachment rows, counts and the Exchange/Load/
 * Discard popup were drawn without their panel every other frame.
 *
 * Run with PSYZ_HEADLESS=1 (software rasterizer) and PSYZ_HEADLESS=gpu
 * (hidden SDL_GPU window); the GPU variant skips when no device is available. */
#include <libgpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
enum { TEST_SKIPPED = 77, BUFFER_STRIDE_ROWS = 272, SCREEN_W = 320, SCREEN_H = 240, TILE_SIZE = 16 };
enum { RED = 0x001f, GREEN = 0x03e0, BLUE = 0x7c00 };

void Psyz_PadsOverride(int p, char* f, int n) { (void)p; (void)f; (void)n; }

static void queueTile(TILE* tile, int x, int y, int w, int h, u_char r, u_char g, u_char b)
{
    memset(tile, 0, sizeof *tile);
    setTile(tile); setXY0(tile, x, y); setWH(tile, w, h); setRGB0(tile, r, g, b);
}

static void queueArea(DR_AREA* area, int x, int y, int w, int h)
{
    RECT rect = { (short)x, (short)y, (short)w, (short)h };
    SetDrawArea(area, &rect);
}

static u_short pixel(int x, int y)
{
    _Alignas(8) u_short value[2];
    RECT rect = { (short)x, (short)y, 1, 1 };
    StoreImage(&rect, (u_long*)value); DrawSync(0);
    return value[0] & 0x7fff;
}

/* Returns 1 when the visible control tile was drawn, 0 when nothing renders. */
static int checkBuffer(int buffer)
{
    const int top = buffer * BUFFER_STRIDE_ROWS;
    RECT clear = { 0, (short)top, SCREEN_W, SCREEN_H };
    ClearImage(&clear, 0, 0, 0); DrawSync(0);

    /* One OT, drawn from its highest tag down, as the UI submits it: a full
     * area and visible tile, the hidden-panel area with content queued under
     * it, then the full-screen restore and another visible tile. */
    /* Each primitive lives in a slot large enough for the P_TAG view addPrim
     * takes of it (the game's primitives sit in one arena), so UBSan accepts it. */
    static union { DR_AREA area; TILE tile; P_TAG tag; } slots[7];
    DR_AREA* const full = &slots[0].area;
    TILE* const before = &slots[1].tile;
    DR_AREA* const hidden = &slots[2].area;
    TILE* const hiddenCorner = &slots[3].tile;
    TILE* const hiddenRow = &slots[4].tile;
    DR_AREA* const restore = &slots[5].area;
    TILE* const after = &slots[6].tile;
    queueArea(full, 0, top, SCREEN_W, SCREEN_H);
    queueTile(before, 8, top + 8, TILE_SIZE, TILE_SIZE, 255, 0, 0);
    queueArea(hidden, 0, top, 0, 0);
    queueTile(hiddenCorner, 0, top, 4, 4, 0, 255, 0);
    queueTile(hiddenRow, 40, top + 8, 96, TILE_SIZE, 0, 255, 0);
    queueArea(restore, 0, top, SCREEN_W, SCREEN_H);
    queueTile(after, 160, top + 8, TILE_SIZE, TILE_SIZE, 0, 0, 255);

    void* chain[] = { full, before, hidden, hiddenCorner, hiddenRow, restore, after };
    enum { COUNT = sizeof chain / sizeof chain[0] };
    OT_TYPE ot[COUNT + 4]; /* spare entries: addPrim reads each tag through the larger P_TAG */
    ClearOTagR(ot, COUNT);
    for (int i = 0; i < COUNT; ++i) addPrim(&ot[COUNT - 1 - i], chain[i]);
    DrawOTag(&ot[COUNT - 1]); DrawSync(0);

    if (pixel(8, top + 8) != RED) return 0;
    CHECK(pixel(8 + TILE_SIZE - 1, top + 8 + TILE_SIZE - 1) == RED);
    /* Content under the empty area must not appear; only buffer 0's clamped
     * one-pixel area at VRAM (0,0) may receive the corner tile. */
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            u_short value = pixel(x, top + y);
            if (buffer == 0 && x == 0 && y == 0) CHECK(value == 0 || value == GREEN);
            else if (value != 0) {
                fprintf(stderr, "buffer %d: hidden corner pixel (%d,%d) drew %04x\n", buffer, x, y, value);
                exit(1);
            }
        }
    for (int y = top + 8; y < top + 8 + TILE_SIZE; y += 5)
        for (int x = 40; x < 136; x += 7)
            if (pixel(x, y) != 0) {
                fprintf(stderr, "buffer %d: hidden row pixel (%d,%d) drew %04x\n", buffer, x, y, pixel(x, y));
                exit(1);
            }
    /* The empty area must not persist after the game restores the view. */
    CHECK(pixel(160, top + 8) == BLUE);
    CHECK(pixel(160 + TILE_SIZE - 1, top + 8 + TILE_SIZE - 1) == BLUE);
    return 1;
}

int main(void)
{
    ResetGraph(0);
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, SCREEN_W, SCREEN_H); PutDrawEnv(&env); DrawSync(0);
    for (int buffer = 0; buffer < 2; ++buffer) {
        if (!checkBuffer(buffer)) {
            const char* headless = getenv("PSYZ_HEADLESS");
            if (buffer == 0 && headless && strcmp(headless, "gpu") == 0) {
                puts("draw area: no hidden GPU device rendered the control tile; skipped");
                return TEST_SKIPPED;
            }
            fprintf(stderr, "buffer %d: visible control tile was not drawn\n", buffer);
            return 1;
        }
    }
    puts("draw area: empty and inverted SetDrawArea clip hidden content in both display buffers OK");
    return 0;
}
