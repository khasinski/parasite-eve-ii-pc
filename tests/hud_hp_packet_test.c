/* Production HP packet layout must not depend on retained primitive bytes. */
#include "port/gpu_len.h"
#include "../src/gameplay/hud_sprites.c"
#include "../src/gameplay/menu_prompt.c"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
DisplayState gDisplayState;
OT_TYPE* gGpuCurrentOt;
void* gGpuPrimCursor;
static GameDebugState debugState;
GameDebugState* gGameDebugState=&debugState;
void textDrawString(TextDrawReq* request,const u8* text) {(void)request;(void)text;}
u8* textItoaUnsigned(u8* out,u32 value) {(void)value;out[0]=0;return out;}
void uiDrawRectFrame(RECT* r,s32 ot,s32 style,const char* title) {(void)r;(void)ot;(void)style;(void)title;}
const u8 gHudHpLabelText[4]={0};
void Psyz_PadsOverride(int p,char* f,int n){(void)p;(void)f;(void)n;}
int main(void)
{
    _Alignas(16) unsigned char arena[sizeof(TILE)+2*sizeof(SPRT)+sizeof(POLY_FT4)+64];
    ResetGraph(0);
    OT_TYPE ot[8];ClearOTagR(ot,8);gGpuCurrentOt=ot+4;
    memset(arena,0xa5,sizeof arena);gGpuPrimCursor=arena;
    hudDrawHpReadout(98,-53,80,160,HUD_HP_READOUT_ENEMY);
    TILE* fill=(void*)arena;CHECK(getlen(fill)==3 && getcode(fill)==0x60);
    CHECK(fill->x0==103 && fill->y0==-39 && fill->w==22 && fill->h==2);
    SPRT_8* cap=(void*)(arena+sizeof(TILE));CHECK(getlen(cap)==3);
    POLY_FT4* bar=(void*)(arena+sizeof(TILE)+2*sizeof(SPRT));CHECK(getlen(bar)==9);
    CHECK(bar->x0==110 && bar->x1==141 && bar->x2==110 && bar->x3==141);
    CHECK(bar->y0==-42 && bar->y1==-42 && bar->y2==-34 && bar->y3==-34);
    CHECK(gGpuPrimCursor==(void*)(arena+sizeof(TILE)+2*sizeof(SPRT)+sizeof(POLY_FT4)));
    UiPanel panel={0};
    ClearOTagR(ot,8);gGpuCurrentOt=ot+4;
    memset(arena,0xa5,sizeof arena);gGpuPrimCursor=arena;
    itemMenuDrawMeter(&panel,-62,-4,-76,160,80,0x01741f);
    cap=(void*)(arena+sizeof(TILE));CHECK(getcode(cap)==0x75 && getlen(cap)==3);
    cap=(void*)(arena+sizeof(TILE)+sizeof(SPRT));CHECK(getcode(cap)==0x75 && getlen(cap)==3);
    puts("HP overlay: poisoned packet arena retains bounded primitive geometry and native lengths");
    return 0;
}
