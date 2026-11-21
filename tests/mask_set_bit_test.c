/* GP0(E6h) bit 0 ("set mask while drawing") must mark every pixel drawn.
 *
 * PE2's frame-feedback effects (scene_runtime.c _displayRedrawPreviousFrame,
 * the cafeteria boss death camera among them) draw the scene with the mask
 * bit forced on, reset it before the HUD (OT -10), then redraw the previous
 * framebuffer as a raw semitransparent texture: marked pixels blend into an
 * afterimage, unmarked HUD pixels are copied once and, being marked by that
 * copy, fade out. The SDL_GPU backend ignored E6h, so nothing was ever
 * marked: the whole picture froze and the textless damage-readout frames
 * stayed on screen until the camera changed (tester report 0.3.6a).
 *
 * Run with PSYZ_HEADLESS=1 (software rasterizer) and PSYZ_HEADLESS=gpu
 * (hidden SDL_GPU window); the GPU variant skips when no device is available. */
#include <libgpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
enum { TEST_SKIPPED = 77, LOWER_Y = 256, SIZE = 16, MASK = 0x8000 };
enum { RED = 0x001f, BLUE = 0x7c00 };

void Psyz_PadsOverride(int p, char* f, int n) { (void)p; (void)f; (void)n; }

/* The game's DR_STP builder (src/port/psyq_host.c); PsyZ declares but does not define it. */
void SetDrawStp(DR_STP* p, int stp)
{
    setlen(p, 2);
    p->code[0] = 0xE6000000u | (stp ? 1u : 0u);
    p->code[1] = 0;
}

static u_short pixel(int x, int y)
{
    _Alignas(8) u_short value[2];
    RECT rect = { (short)x, (short)y, 1, 1 };
    StoreImage(&rect, (u_long*)value); DrawSync(0);
    return value[0];
}

static void tile(TILE* t, int x, int y, u_char r, u_char g, u_char b, int semi)
{
    memset(t, 0, sizeof *t);
    setTile(t); setXY0(t, x, y); setWH(t, SIZE, SIZE); setRGB0(t, r, g, b);
    setSemiTrans(t, semi);
}

static void drawChain(void** chain, int count)
{
    OT_TYPE ot[16];
    ClearOTagR(ot, count);
    for (int i = 0; i < count; ++i) addPrim(&ot[count - 1 - i], chain[i]);
    DrawOTag(&ot[count - 1]); DrawSync(0);
}

int main(void)
{
    ResetGraph(0);
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 1024, 512); PutDrawEnv(&env); DrawSync(0);
    RECT clear = { 0, 0, 320, 512 };
    ClearImage(&clear, 0, 0, 0); DrawSync(0);

    /* Source frame: scene with the mask forced on, then a "HUD" tile without. */
    DR_STP setOn, setOff, setOn2, setOff2;
    SetDrawStp(&setOn, 1); SetDrawStp(&setOff, 0);
    TILE scene, additive, hud;
    tile(&scene, 8, 8, 255, 0, 0, 0);
    tile(&additive, 40, 8, 0, 0, 255, 1); /* abr 0 (average) over black */
    tile(&hud, 72, 8, 0, 0, 255, 0);
    void* frame[] = { &setOn, &scene, &additive, &setOff, &hud };
    drawChain(frame, 5);

    if ((pixel(8, 8) & 0x7fff) != RED) {
        const char* headless = getenv("PSYZ_HEADLESS");
        if (headless && strcmp(headless, "gpu") == 0) {
            puts("mask set: no hidden GPU device rendered the control tile; skipped");
            return TEST_SKIPPED;
        }
        fprintf(stderr, "control tile not drawn: %04x\n", pixel(8, 8));
        return 1;
    }
    CHECK(pixel(8, 8) == (RED | MASK));
    CHECK(pixel(8 + SIZE - 1, 8 + SIZE - 1) == (RED | MASK));
    CHECK((pixel(40, 8) & MASK) != 0);         /* semitransparent pixels are marked too */
    CHECK(pixel(72, 8) == BLUE);               /* drawn after the reset: unmarked */

    /* Feedback: redraw that frame as a raw semitransparent 16-bit texture into
     * the lower buffer with the mask forced on, as the death camera does. */
    POLY_FT4 copy;
    memset(&copy, 0, sizeof copy);
    setPolyFT4(&copy); setSemiTrans(&copy, 1); setShadeTex(&copy, 1);
    setXYWH(&copy, 0, LOWER_Y, 128, 32);
    setUVWH(&copy, 0, 0, 128, 32);
    copy.tpage = getTPage(2, 0, 0, 0);
    SetDrawStp(&setOn2, 1); SetDrawStp(&setOff2, 0);
    void* feedback[] = { &setOn2, &copy, &setOff2 };
    drawChain(feedback, 3);

    /* The unmarked HUD texel is copied opaque, and the copy carries the mask,
     * so the next feedback pass blends it away instead of copying it forever. */
    CHECK(pixel(72, LOWER_Y + 8) == (BLUE | MASK));
    CHECK(pixel(72 + SIZE - 1, LOWER_Y + 8 + SIZE - 1) == (BLUE | MASK));
    /* The marked scene texel averages with the (black) destination: 31/2 is
     * 15 on PS1, the software rasterizer and SDL_GPU; the OpenGL backend's
     * fixed-function blend into RGBA8 rounds the half up to 16. Either way it
     * is blended, not copied (31). */
    const u_short averaged = pixel(8, LOWER_Y + 8) & 0x7fff;
    CHECK(averaged == (RED >> 1 & 0x1f) || averaged == ((RED >> 1) + 1 & 0x1f));
    CHECK((pixel(8, LOWER_Y + 8) & MASK) != 0);
    /* Fully transparent texels (0x0000) are not drawn and leave the mask alone. */
    CHECK(pixel(120, LOWER_Y + 30) == 0);

    /* With the mask reset nothing new is marked. */
    TILE plain;
    tile(&plain, 200, 8, 255, 0, 0, 0);
    void* after[] = { &plain };
    drawChain(after, 1);
    CHECK(pixel(200, 8) == RED);
    puts("mask set: E6h bit 0 marks opaque, semitransparent and copied pixels OK");
    return 0;
}
