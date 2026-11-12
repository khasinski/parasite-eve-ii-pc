/* Offscreen GPU regression: native polygon scissor beside a wide sprite. */
#include <psyz.h>
#include <psyz/video.h>
#include <psyz/gpu.h>
#include <libgpu.h>
#include <stdio.h>
#include <stdlib.h>
void Psyz_PadsOverride(int p,char* f,int n){(void)p;(void)f;(void)n;}
static unsigned short pixels[1024*512];
int main(int argc,char** argv)
{
    if(argc!=6)return 2;
    int native_clip=atoi(argv[1]),wide=atoi(argv[2]),y=atoi(argv[3]);
    ResetGraph(0);Psyz_VideoSetVsyncMode(PSYZ_VSYNC_LIMITLESS);
    Psyz_VideoSetInternalResolution(4);Psyz_VideoSetWidescreenDisplayOnly(1);
    DRAWENV draw;DISPENV display;
    SetDefDrawEnv(&draw,0,y,320,240);SetDefDispEnv(&display,0,y,320,240);
    draw.ofs[0]=160;draw.ofs[1]=y+120;
    PutDrawEnv(&draw);PutDispEnv(&display);SetDispMask(1);Psyz_VideoVSync(0);
    Psyz_VideoSetWidescreen(wide);Psyz_VideoSetWidescreenPicture(y,-64,384);
    Psyz_VideoSetWidescreenNativePolygons(native_clip);
    Psyz_GpuDisplayCommand(0x07000000u|(256u<<10));
    TILE background;setTile(&background);setRGB0(&background,0,0,100);
    setXY0(&background,-256,-120);setWH(&background,832,240);DrawPrim(&background);
    POLY_F4 outside,crossing;
    setPolyF4(&outside);setRGB0(&outside,255,0,0);
    setXY4(&outside,-216,-70,-184,-70,-216,70,-184,70);DrawPrim(&outside);
    setPolyF4(&crossing);setRGB0(&crossing,0,255,0);
    setXY4(&crossing,-176,-50,-144,-50,-176,50,-144,50);DrawPrim(&crossing);
    /* Sprite after polygons catches a batch-scissor leak into the backdrop. */
    TILE sprite;setTile(&sprite);setRGB0(&sprite,0,0,200);
    setXY0(&sprite,180,-120);setWH(&sprite,40,240);DrawPrim(&sprite);
    DrawSync(0);Psyz_GpuDisplayCommand(0x07000000u|16u|(256u<<10));
    StoreImage(&(RECT){0,0,1024,512},(u_long*)pixels);
    FILE* f=fopen(argv[5],"wb");if(!f)return 3;fwrite(pixels,2,1024*512,f);fclose(f);
    Psyz_VideoVSync(0);int w,h;unsigned char* rgb=Psyz_VideoAllocRenderedFrame(&w,&h);
    if(!rgb)return 3;f=fopen(argv[4],"wb");if(!f)return 3;
    fprintf(f,"P6\n%d %d\n255\n",w,h);fwrite(rgb,3,(size_t)w*h,f);fclose(f);free(rgb);
    return 0;
}
