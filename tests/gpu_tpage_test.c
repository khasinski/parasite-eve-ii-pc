/* Textured polygon TPage must select the following SPRT page as on PS1.
 * UI frame edges carry TPage; their 8x8 corners inherit it. */
#include "port/gpu_len.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
void Psyz_PadsOverride(int p, char* f, int n) { (void)p; (void)f; (void)n; }
int main(void)
{
    ResetGraph(0);
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 128, 128); PutDrawEnv(&env); DrawSync(0);
    _Alignas(8) u16 texels[32], palette[16], actual[64];
    for (int i = 0; i < 32; ++i) texels[i] = 0x1111;
    memset(palette, 0, sizeof palette); palette[1] = 0x03e0;
    RECT right = {896, 256, 4, 8}, wrong = {640, 256, 4, 8}, clut = {48, 240, 16, 1};
    LoadImage(&right, (u_long*)texels); LoadImage(&clut, (u_long*)palette);
    for (int i = 0; i < 32; ++i) texels[i] = 0x2222;
    LoadImage(&wrong, (u_long*)texels); DrawSync(0);
    for (int kind = 0; kind < 4; ++kind) {
        union { POLY_FT3 ft3; POLY_FT4 ft4; POLY_GT3 gt3; POLY_GT4 gt4; } poly;
        memset(&poly, 0, sizeof poly);
        u16 page = getTPage(0, 0, 896, 256), pal = getClut(48, 240);
        switch (kind) {
        case 0:
            setPolyFT3(&poly.ft3); setXY3(&poly.ft3, 0,0, 8,0, 0,8);
            setUV3(&poly.ft3, 0,0, 8,0, 0,8); poly.ft3.tpage=page; poly.ft3.clut=pal; break;
        case 1:
            setPolyFT4(&poly.ft4); setXY4(&poly.ft4, 0,0, 8,0, 0,8, 8,8);
            setUV4(&poly.ft4, 0,0, 8,0, 0,8, 8,8); poly.ft4.tpage=page; poly.ft4.clut=pal; break;
        case 2:
            setPolyGT3(&poly.gt3); setXY3(&poly.gt3, 0,0, 8,0, 0,8);
            setUV3(&poly.gt3, 0,0, 8,0, 0,8); poly.gt3.tpage=page; poly.gt3.clut=pal; break;
        default:
            setPolyGT4(&poly.gt4); setXY4(&poly.gt4, 0,0, 8,0, 0,8, 8,8);
            setUV4(&poly.gt4, 0,0, 8,0, 0,8, 8,8); poly.gt4.tpage=page; poly.gt4.clut=pal; break;
        }
        SPRT_8 corner; memset(&corner, 0, sizeof corner);
        setSprt8(&corner); setShadeTex(&corner, 1); setXY0(&corner, 32,16);
        setUV0(&corner, 0,0); corner.clut = pal;
        DR_MODE mode; SetDrawMode(&mode, 1, 1, getTPage(0,0,640,256), NULL);
        OT_TYPE ot[4]; ClearOTagR(ot, 4);
        addPrim(&ot[1], &corner); addPrim(&ot[1], &poly); addPrim(&ot[1], &mode);
        DrawOTag(&ot[3]); DrawSync(0);
        RECT sample = {32,16,8,8}; StoreImage(&sample, (u_long*)actual); DrawSync(0);
        for (int i = 0; i < 64; ++i) {
            /* This fixture checks texture-page selection, independent of the
             * backend's framebuffer alpha/mask representation. */
            if ((actual[i] & 0x7fff) != 0x03e0) {
                fprintf(stderr, "polygon kind %d, sprite pixel %d: expected 03e0, got %04x\n", kind, i, actual[i]);
                exit(1);
            }
        }
    }
    puts("GPU TPage: FT3/FT4/GT3/GT4 select the subsequent 8x8 sprite texture page OK");
    return 0;
}
