/* Host settings/disc UI adapted from parasite-eve-pc/src/main/boot:
 * settings_overlay.h (Plex painter + SDL_GPU compositor), main.c (modal
 * event ownership, native file dialogs, audio/clock pause and pad reset).
 * Kept separate from retail headers: their bool typedef is not SDL's ABI. */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psyz.h>
#include <psyz/audio.h>
#include <psyz/cd.h>
#include <psyz/video.h>
#include <psyz/overlay.h>
#include <psyz/overlay_sdl3.h>
#include <psyz/overlay_sdl3_gpu.h>
#include <libgpu.h>
#include "port/host_ui.h"
#include "port/disc_assets.h"
#include "port/asset_bind.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "ui/stb_truetype.h"
#include "ui/fonts/ibm_plex_sans.ttf.inc"
#include "../../third_party/psyz/external/SDL/src/render/gpu/shaders/tri_texture.vert.spv.h"
#include "../../third_party/psyz/external/SDL/src/render/gpu/shaders/tri_texture.vert.msl.h"
#include "../../third_party/psyz/external/SDL/src/render/gpu/shaders/tri_texture.vert.dxil.h"
#include "../../third_party/psyz/external/SDL/src/render/gpu/shaders/texture_rgba.frag.spv.h"
#include "../../third_party/psyz/external/SDL/src/render/gpu/shaders/texture_rgba.frag.msl.h"
#include "../../third_party/psyz/external/SDL/src/render/gpu/shaders/texture_rgba.frag.dxil.h"

enum { OVERLAY_HEADER_H=128, OVERLAY_ROW_H=46, OVERLAY_ACTION_GAP=14,
       OVERLAY_FOOTER_H=90, OVERLAY_REFERENCE_H=820, OVERLAY_PANEL_W=680 };
typedef struct { uint8_t* pixels; int w,h; } OverlaySurface;
typedef struct { float scale; int x,y,w,h; } OverlayLayout;
typedef struct { float r,g,b,a; } Color;
typedef struct { int open,page,row,scroll,dirty,quit,capture; } HostOverlayState;
/* The panel always shows this many rows, so its height, tab bar and footer stay put when the page changes. */
enum { VISIBLE_ROWS=8 };
static const Color kPanel={0.075f,0.082f,0.102f,0.96f}, kText={0.93f,0.95f,0.98f,1},
    kMuted={0.60f,0.65f,0.73f,1}, kAccent={0.27f,0.62f,1,1};
#include "ui/paint.h"
#include "ui/gpu.h"

static char discPaths[2][4096], configPath[4096], statusText[256];
static int requiredDisc, selectedDisc;
static const char* const pages[]={"Graphics","Controls","Audio","Discs"};
static const char* const buttons[]={"Select","L3","R3","Start","Up","Right","Down","Left",
    "L2","R2","L1","R1","Triangle","Circle","Cross","Square"};
static const char* configDir;
static PsyzOverlayEventFilter_SDL3 previousFilter;
static SDL_Window* uiWindow;

static int rowCount(void) {
    return s_overlayState.page==0?8:s_overlayState.page==2?1:s_overlayState.page==1?16:2;
}

static int maxScroll(void) { int n=rowCount()-VISIBLE_ROWS; return n>0?n:0; }

/* Scroll by the least amount that keeps the selected row in view (keyboard and pad); the mouse wheel sets it directly. */
static void ensureRowVisible(void) {
    if(s_overlayState.scroll>maxScroll())s_overlayState.scroll=maxScroll();
    if(s_overlayState.scroll<0)s_overlayState.scroll=0;
    if(s_overlayState.row<s_overlayState.scroll)s_overlayState.scroll=s_overlayState.row;
    else if(s_overlayState.row>=s_overlayState.scroll+VISIBLE_ROWS)
        s_overlayState.scroll=s_overlayState.row-VISIBLE_ROWS+1;
}

static void setPage(int page) { s_overlayState.page=page;s_overlayState.row=0;s_overlayState.scroll=0; }

static void saveOptions(void) {
    char temp[4120];
    snprintf(temp,sizeof temp,"%s.tmp",configPath);
    FILE* f=fopen(temp,"w");
    if(!f) { snprintf(statusText,sizeof statusText,"Could not save preferences."); return; }
    fprintf(f,"disc1=%s\ndisc2=%s\nscale=%u\naspect=%d\ndither=%d\ngeometry=%d\naa=%d\nfilter=%d\nlight=%d\nvolume=%u\n",
        discPaths[0],discPaths[1],Psyz_VideoGetInternalResolution(),Psyz_VideoGetAspectMode(),
        Psyz_VideoGetDitheringMode(),Psyz_VideoGetGeometryCorrection(),Psyz_VideoGetAntialiasing(),
        Psyz_VideoGetModelFiltering(),Psyz_VideoGetDynamicLighting(),Psyz_AudioGetOutputVolume());
    for(unsigned i=0;i<16;i++) fprintf(f,"key%u=%d\n",i,Psyz_GetKeyboardBinding_SDL3(i));
    int failed=ferror(f); if(fclose(f)) failed=1;
    if(failed || rename(temp,configPath)) snprintf(statusText,sizeof statusText,"Could not save preferences.");
}

static void loadOptions(void) {
    configDir=getenv("PE2_CONFIG_DIR");
    if(!configDir) configDir=SDL_GetPrefPath("parasite-eve","parasite-eve-ii");
    if(!configDir) configDir=".";
    SDL_CreateDirectory(configDir);
    snprintf(configPath,sizeof configPath,"%s/settings.ini",configDir);
    FILE* f=fopen(configPath,"r"); if(!f)return;
    char line[8192];
    while(fgets(line,sizeof line,f)) {
        line[strcspn(line,"\r\n")]=0;
        char* eq=strchr(line,'='); if(!eq)continue; *eq++=0;
        if(!strcmp(line,"disc1")||!strcmp(line,"disc2")) {
            if(strlen(eq)<sizeof discPaths[0]) strcpy(discPaths[line[4]-'1'],eq);
            continue;
        }
        char* end; long n=strtol(eq,&end,10); if(end==eq || *end)continue;
        if(!strcmp(line,"scale") && n>=1 && n<=8) Psyz_VideoSetInternalResolution((unsigned)n);
        else if(!strcmp(line,"aspect") && n>=0 && n<=1) Psyz_VideoSetAspectMode(n);
        else if(!strcmp(line,"dither") && n>=0 && n<=1) Psyz_VideoSetDitheringMode(n);
        else if(!strcmp(line,"geometry") && n>=0 && n<=1) Psyz_VideoSetGeometryCorrection(n);
        else if(!strcmp(line,"aa") && n>=0 && n<=1) Psyz_VideoSetAntialiasing(n);
        else if(!strcmp(line,"filter") && n>=0 && n<=1) Psyz_VideoSetModelFiltering(n);
        else if(!strcmp(line,"light") && n>=0 && n<=1) Psyz_VideoSetDynamicLighting(n);
        else if(!strcmp(line,"volume") && n>=0 && n<=100) Psyz_AudioSetOutputVolume(n);
        else if(!strncmp(line,"key",3)) {
            char* last; long index=strtol(line+3,&last,10);
            if(!*last && index>=0 && index<16 && n>=0 && n<SDL_SCANCODE_COUNT)
                Psyz_SetKeyboardBinding_SDL3(index,(SDL_Scancode)n);
        }
    }
    fclose(f);
}

/* Raw BINs are presented to the SDK through a generated CUE in preferences.
 * The user's image is opened read-only and is never modified. */
static int preparePath(const char* path,char out[4096]) {
    if(strlen(path)>=4096 || strpbrk(path,"\r\n\"")) return 0;
    const char* ext=strrchr(path,'.');
    if(ext && !SDL_strcasecmp(ext,".bin")) {
        char* absolute=SDL_GetCurrentDirectory();
        char bin[4096];
        int length=path[0]=='/'?snprintf(bin,sizeof bin,"%s",path):
            snprintf(bin,sizeof bin,"%s%s",absolute?absolute:"",path);
        SDL_free(absolute);
        if(length<0 || length>=(int)sizeof bin)return 0;
        /* Slot determined after validation; one temporary CUE per candidate. */
        snprintf(out,4096,"%s/selected-image.cue",configDir);
        FILE* f=fopen(out,"w"); if(!f)return 0;
        fprintf(f,"FILE \"%s\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n",bin);
        int failed=ferror(f); if(fclose(f))failed=1;
        return !failed;
    }
    if(path[0]=='/' || path[0]=='\\' || (path[0] && path[1]==':'))strcpy(out,path);
    else {
        char* directory=SDL_GetCurrentDirectory();
        int length=snprintf(out,4096,"%s%s",directory?directory:"",path);
        SDL_free(directory);if(length<0||length>=4096)return 0;
    }
    return 1;
}

static int acceptPath(const char* path,int expected) {
    char cue[4096]; if(!preparePath(path,cue))return 0;
    PeDisc* d=peDiscOpen(cue); int number=peDiscNumber(d); peDiscClose(d);
    if(!number || (expected && number!=expected)) {
        FILE* probe=fopen(path,"rb");
        if(!probe)snprintf(statusText,sizeof statusText,"Cannot open %s. Check the path and permissions.",path);
        else {
            fclose(probe);
            if(number)snprintf(statusText,sizeof statusText,"This is USA Disc %d, but Disc %d is needed here.",number,expected);
            else snprintf(statusText,sizeof statusText,"Not a supported USA Parasite Eve II image (keep the CUE next to its BIN, and use the North American release).");
        }
        return 0;
    }
    const char* ext=strrchr(path,'.');
    if(ext && !SDL_strcasecmp(ext,".bin")) {
        char stable[4096]; snprintf(stable,sizeof stable,"%s/disc%d.cue",configDir,number);
        if(rename(cue,stable))return 0;
        strcpy(cue,stable);
    }
    strcpy(discPaths[number-1],cue);
    snprintf(statusText,sizeof statusText,"USA Disc %d configured.",number);
    return number;
}

typedef struct { SDL_AtomicInt done; char* paths[2]; int failed; } DiscDialog;
static void SDLCALL discChosen(void* user,const char* const* files,int filter) {
    DiscDialog* d=user; (void)filter;
    if(!files)d->failed=1;
    else for(int i=0;files[i];i++) {
        if(i>=2) { d->failed=1; break; }
        d->paths[i]=SDL_strdup(files[i]); if(!d->paths[i])d->failed=1;
    }
    SDL_SetAtomicInt(&d->done,1);
}
static int chooseDiscs(SDL_Window* window,int expected) {
    const SDL_DialogFileFilter filters[]={{"Parasite Eve II USA disc images","cue;bin"}};
    DiscDialog d={0};
    Psyz_PauseHostClock_SDL3(true);
    SDL_ShowOpenFileDialog(discChosen,&d,window,filters,1,NULL,expected==0);
    while(!SDL_GetAtomicInt(&d.done)) { SDL_PumpEvents(); SDL_Delay(10); }
    int count=0;
    if(!d.failed)for(int i=0;i<2;i++) if(d.paths[i])count+=acceptPath(d.paths[i],expected)!=0;
    for(int i=0;i<2;i++)SDL_free(d.paths[i]);
    Psyz_PauseHostClock_SDL3(false);
    s_overlayState.dirty=1;
    return count;
}

static void rowText(int row,char label[128],char value[256]) {
    value[0]=0;
    if(s_overlayState.page==0) {
        const char* labels[]={"Picture detail","Aspect ratio","Dithering","Geometry correction","Fullscreen","Antialiasing","Model filtering","Smooth lighting"};
        strcpy(label,labels[row]);
        if(row==0)snprintf(value,256,"%ux",Psyz_VideoGetInternalResolution());
        if(row==1)strcpy(value,Psyz_VideoGetAspectMode()?"Square pixels":"Classic 4:3");
        if(row==2)strcpy(value,Psyz_VideoGetDitheringMode()?"Off":"Original");
        if(row==3)strcpy(value,Psyz_VideoGetGeometryCorrection()?"On":"Off");
        if(row==4)strcpy(value,(s_overlayGpuWindow && SDL_GetWindowFlags(s_overlayGpuWindow)&SDL_WINDOW_FULLSCREEN)?"On":"Off");
        if(row==5)strcpy(value,Psyz_VideoGetAntialiasing()?"On":"Off");
        if(row==6)strcpy(value,Psyz_VideoGetModelFiltering()?"On":"Off");
        if(row==7)strcpy(value,Psyz_VideoGetDynamicLighting()?"On":"Off");
    } else if(s_overlayState.page==2) {
        strcpy(label,"Master volume");snprintf(value,256,"%u%%",Psyz_AudioGetOutputVolume());
    } else if(s_overlayState.page==1) {
        strcpy(label,buttons[row]);
        snprintf(value,256,"%s",s_overlayState.capture && row==s_overlayState.row?"Press a key...":
            SDL_GetScancodeName(Psyz_GetKeyboardBinding_SDL3(row)));
    } else {
        snprintf(label,128,"Disc %d",row+1);
        const char* p=strrchr(discPaths[row],'/'); p=p?p+1:discPaths[row];
        snprintf(value,256,"%.40s",*p?p:"Choose image...");
    }
}

static void paintPanel(OverlaySurface* surface,OverlayLayout* layout) {
    memset(surface->pixels,0,(size_t)surface->w*surface->h*4);
    float s=layout->scale;
    FillRoundRect(surface,0,0,680*s,(surface->h-1),16*s,kPanel);
    DrawText(surface,32*s,42*s,24*s,"PARASITE EVE II",kText,ALIGN_LEFT);
    DrawText(surface,648*s,42*s,14*s,"0.1 alpha / development",kMuted,ALIGN_RIGHT);
    for(int i=0;i<4;i++)DrawText(surface,(32+i*161)*s,87*s,17*s,pages[i],
        s_overlayState.page==i?kAccent:kMuted,ALIGN_LEFT);
    ensureRowVisible();
    int first=s_overlayState.scroll;
    int rows=VISIBLE_ROWS;
    int shown=rowCount()<VISIBLE_ROWS?rowCount():VISIBLE_ROWS;
    for(int i=0;i<shown;i++) {
        int row=first+i; char label[128],value[256];rowText(row,label,value);
        float y=(128+i*46)*s;
        if(row==s_overlayState.row)FillRoundRect(surface,20*s,y-27*s,640*s,40*s,7*s,
            (Color){0.27f,0.62f,1,0.16f});
        DrawText(surface,32*s,y,17*s,label,kText,ALIGN_LEFT);
        DrawText(surface,648*s,y,16*s,value,row==s_overlayState.row?kAccent:kMuted,ALIGN_RIGHT);
    }
    if(maxScroll()>0) {   /* scrollbar of a page with more rows than fit */
        float trackTop=(101+4)*s,trackH=(VISIBLE_ROWS*46-8)*s,thumbH=trackH*VISIBLE_ROWS/rowCount();
        float thumbTop=trackTop+(trackH-thumbH)*s_overlayState.scroll/maxScroll();
        FillRoundRect(surface,668*s,trackTop,4*s,trackH,2*s,(Color){0.93f,0.95f,0.98f,0.10f});
        FillRoundRect(surface,668*s,thumbTop,4*s,thumbH,2*s,(Color){0.27f,0.62f,1,0.65f});
    }
    float y=(128+rows*46+16)*s;
    DrawText(surface,32*s,y,14*s,statusText,kMuted,ALIGN_LEFT);
    DrawText(surface,32*s,y+27*s,13*s,"Tab / shoulders: page   Arrows: select / change   Enter: choose",kMuted,ALIGN_LEFT);
    DrawText(surface,32*s,y+48*s,13*s,requiredDisc?"Select the requested disc; Esc cancels.":"Esc / controller Start: resume   Controls: Enter to bind a key",kMuted,ALIGN_LEFT);
    surface->pixels[((size_t)surface->h*surface->w-1)*4+3]=255;
}

static int refreshPanel(SDL_Window* window) {
    int w,h; SDL_GetWindowSizeInPixels(window,&w,&h);if(w<=0||h<=0||!OverlayFontReady())return 0;
    OverlayLayout layout;OverlayFitLayout(680,OverlayDesignHeight(VISIBLE_ROWS),w,h,&layout);
    s_overlayWindowW=w;s_overlayWindowH=h;s_overlayLayout=layout;
    if(!OverlaySurfaceResize(&s_overlaySurface,layout.w,layout.h))return 0;
    paintPanel(&s_overlaySurface,&layout);
    if(getenv("PE2_DUMP_MENU")) {   /* development: the painted panel as a PPM (RGB over black), for a capture-free review */
        FILE* f=fopen(getenv("PE2_DUMP_MENU"),"wb");
        if(f) {
            fprintf(f,"P6\n%d %d\n255\n",s_overlaySurface.w,s_overlaySurface.h);
            for(size_t i=0;i<(size_t)s_overlaySurface.w*s_overlaySurface.h;i++) {
                const unsigned char* px=&s_overlaySurface.pixels[i*4];
                for(int c=0;c<3;c++)fputc(px[c]*px[3]/255,f);
            }
            fclose(f);
        }
    }
    return 1;
}

static void changeRow(int direction,int activate) {
    int r=s_overlayState.row;
    if(s_overlayState.page==0) {
        if(r==0) { int n=(int)Psyz_VideoGetInternalResolution()+direction;
            if(n>=1&&n<=8)Psyz_VideoSetInternalResolution(n); }
        if(r==1)Psyz_VideoSetAspectMode(!Psyz_VideoGetAspectMode());
        if(r==2)Psyz_VideoSetDitheringMode(!Psyz_VideoGetDitheringMode());
        if(r==3)Psyz_VideoSetGeometryCorrection(!Psyz_VideoGetGeometryCorrection());
        if(r==4 && s_overlayGpuWindow)SDL_SetWindowFullscreen(s_overlayGpuWindow,
            !(SDL_GetWindowFlags(s_overlayGpuWindow)&SDL_WINDOW_FULLSCREEN));
        if(r==5)Psyz_VideoSetAntialiasing(!Psyz_VideoGetAntialiasing());
        if(r==6)Psyz_VideoSetModelFiltering(!Psyz_VideoGetModelFiltering());
        if(r==7)Psyz_VideoSetDynamicLighting(!Psyz_VideoGetDynamicLighting());
    } else if(s_overlayState.page==2) {
        int n=(int)Psyz_AudioGetOutputVolume()+direction*5;
        Psyz_AudioSetOutputVolume(n<0?0:n>100?100:n);
    } else if(s_overlayState.page==1 && activate)s_overlayState.capture=1;
    else if(s_overlayState.page==3 && activate) {
        if(chooseDiscs(s_overlayGpuWindow,r+1) && requiredDisc==r+1) {
            selectedDisc=requiredDisc;s_overlayState.open=0;
        }
    }
    s_overlayState.dirty=1;
}

static void processEvent(const SDL_Event* e) {
    if(e->type==SDL_EVENT_QUIT) { s_overlayState.open=0;s_overlayState.quit=1;return; }
    int key=0;
    if(e->type==SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button==SDL_BUTTON_LEFT && uiWindow) {
        int w,h;SDL_GetWindowSize(uiWindow,&w,&h);
        float x=(e->button.x*s_overlayWindowW/w-s_overlayLayout.x)/s_overlayLayout.scale;
        float y=(e->button.y*s_overlayWindowH/h-s_overlayLayout.y)/s_overlayLayout.scale;
        if(y>=60 && y<=100 && x>=20 && x<664) {
            setPage((int)((x-20)/161));
        } else if(y>=101 && x>=20 && x<=660) {
            int first=s_overlayState.scroll;
            int row=first+(int)((y-101)/46);
            if(row<rowCount() && row<first+VISIBLE_ROWS) {
                s_overlayState.row=row;changeRow(x<500?-1:1,1);
            }
        }
        s_overlayState.dirty=1;return;
    }
    if(e->type==SDL_EVENT_MOUSE_WHEEL) {
        int n=s_overlayState.scroll-(int)(e->wheel.y>0?1:e->wheel.y<0?-1:0);
        s_overlayState.scroll=n<0?0:n>maxScroll()?maxScroll():n;
        s_overlayState.dirty=1;return;
    }
    if(e->type==SDL_EVENT_KEY_DOWN) {
        if(e->key.repeat)return;
        if(s_overlayState.capture) {
            if(e->key.scancode!=SDL_SCANCODE_ESCAPE)
                Psyz_SetKeyboardBinding_SDL3(s_overlayState.row,e->key.scancode);
            s_overlayState.capture=0;s_overlayState.dirty=1;return;
        }
        key=e->key.scancode;
    } else if(e->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch(e->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:key=SDL_SCANCODE_UP;break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:key=SDL_SCANCODE_DOWN;break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:key=SDL_SCANCODE_LEFT;break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:key=SDL_SCANCODE_RIGHT;break;
        case SDL_GAMEPAD_BUTTON_SOUTH:key=SDL_SCANCODE_RETURN;break;
        case SDL_GAMEPAD_BUTTON_EAST:case SDL_GAMEPAD_BUTTON_START:key=SDL_SCANCODE_ESCAPE;break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:key=SDL_SCANCODE_TAB;break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            setPage((s_overlayState.page+3)%4);break;
        default:break;
        }
    }
    if(key==SDL_SCANCODE_ESCAPE)s_overlayState.open=0;
    if(key==SDL_SCANCODE_TAB)setPage((s_overlayState.page+1)%4);
    if(key==SDL_SCANCODE_UP)s_overlayState.row=(s_overlayState.row+rowCount()-1)%rowCount();
    if(key==SDL_SCANCODE_DOWN)s_overlayState.row=(s_overlayState.row+1)%rowCount();
    ensureRowVisible();
    if(key==SDL_SCANCODE_LEFT)changeRow(-1,0);
    if(key==SDL_SCANCODE_RIGHT)changeRow(1,0);
    if(key==SDL_SCANCODE_RETURN)changeRow(1,1);
    s_overlayState.dirty=1;
}

static void settings(void) {
    int wasPaused=Psyz_AudioIsPaused();
    Psyz_PauseHostClock_SDL3(true);Psyz_AudioPause();
    s_overlayState.open=1;s_overlayState.quit=0;
    if(getenv("PE2_MENU_PAGE")) setPage(atoi(getenv("PE2_MENU_PAGE"))&3);   /* development: open on this page */
    SDL_Window* fallback=NULL;SDL_Renderer* renderer=NULL;SDL_Texture* texture=NULL;
    if(!s_overlayGpuDevice) {
        fallback=SDL_CreateWindow("Parasite Eve II - Settings",900,720,SDL_WINDOW_HIGH_PIXEL_DENSITY|SDL_WINDOW_RESIZABLE);
        if(fallback)renderer=SDL_CreateRenderer(fallback,NULL);
        if(!renderer) { s_overlayState.open=0; goto done; }
    }
    while(s_overlayState.open) {
        SDL_Window* window=fallback?fallback:s_overlayGpuWindow;
        uiWindow=window;
        if(!window || !refreshPanel(window)) { s_overlayState.open=0; break; }
        if(fallback) {
            SDL_DestroyTexture(texture);
            texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,s_overlaySurface.w,s_overlaySurface.h);
            if(!texture)break;
            SDL_UpdateTexture(texture,NULL,s_overlaySurface.pixels,s_overlaySurface.w*4);
            SDL_SetRenderDrawColor(renderer,13,17,23,255);SDL_RenderClear(renderer);
            SDL_FRect rect={s_overlayLayout.x,s_overlayLayout.y,s_overlayLayout.w,s_overlayLayout.h};
            SDL_RenderTexture(renderer,texture,NULL,&rect);SDL_RenderPresent(renderer);
            SDL_Event e; if(SDL_WaitEventTimeout(&e,16))processEvent(&e);
        } else {
            if(!Host_EnsureOverlayPipeline(s_overlayGpuDevice,SDL_GetGPUSwapchainTextureFormat(s_overlayGpuDevice,window)) ||
               !Host_UploadOverlaySurfaceAndVertices(s_overlayGpuDevice)) {
                s_overlayState.open=0;break;
            }
            Psyz_VideoVSync(0);
        }
    }
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(fallback);
    saveOptions();
done:
    uiWindow=NULL;s_overlayState.open=0;SDL_ResetKeyboard();Psyz_PauseHostClock_SDL3(false);
    if(!wasPaused)Psyz_AudioUnpause();
    if(s_overlayState.quit) { SDL_Event e={.type=SDL_EVENT_QUIT};SDL_PushEvent(&e); }
}

static bool settingsEvent(const SDL_Event* e) {
    if(s_overlayState.open) {processEvent(e);return true;}
    int open=e->type==SDL_EVENT_KEY_DOWN && e->key.scancode==SDL_SCANCODE_ESCAPE && !e->key.repeat;
    if(e->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
       (e->gbutton.button==SDL_GAMEPAD_BUTTON_LEFT_STICK||e->gbutton.button==SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
        SDL_Gamepad* pad=SDL_GetGamepadFromID(e->gbutton.which);
        open=pad && SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_LEFT_STICK) && SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    }
    if(open) { settings();return true; }
    return previousFilter?previousFilter(e):false;
}

int peHostStartup(int argc,char** argv,const char** disc) {
    loadOptions();
    int menuOnly=0,validateOnly=0;
    const char* explicitDisc=getenv("PE2_DISC");
    const char* explicitDisc2=getenv("PE2_DISC2");
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--host-menu"))menuOnly=1;
        else if(!strcmp(argv[i],"--validate-discs"))validateOnly=1;
        else if(!strcmp(argv[i],"--disc") && i+1<argc)explicitDisc=argv[++i];
        else if(!strcmp(argv[i],"--disc2") && i+1<argc)explicitDisc2=argv[++i];
        else if(argv[i][0]!='-' && !explicitDisc)explicitDisc=argv[i];
        else { fprintf(stderr,"usage: %s [disc.cue|disc.bin] [--disc2 image] [--host-menu]\n",argv[0]);return -1; }
    }
    if(explicitDisc && acceptPath(explicitDisc,1)!=1) {fprintf(stderr,"%s\n",statusText);return -1;}
    if(explicitDisc2 && acceptPath(explicitDisc2,2)!=2) {fprintf(stderr,"%s\n",statusText);return -1;}
    if(validateOnly) {
        for(int i=0;i<2;i++) {
            PeDisc* d=peDiscOpen(discPaths[i]);int number=peDiscNumber(d);peDiscClose(d);
            if(number!=i+1) {fprintf(stderr,"Disc %d validation failed.\n",i+1);return -1;}
            fprintf(stderr,"USA Disc %d validated: %s\n",number,discPaths[i]);
        }
        saveOptions();return 0;
    }
    s_prevOverlayInit=Psyz_OverlayInit_SDL3GPU(Host_OverlayInitGPU);
    s_prevOverlayRender=Psyz_OverlayRender_SDL3GPU(Host_OverlayRenderGPU);
    s_prevOverlayDestroy=Psyz_OverlayDestroyCB(Host_OverlayDestroyGPU);
    previousFilter=Psyz_SetOverlayEventFilter_SDL3(settingsEvent);
    if(menuOnly) {
        if(!SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD))return -1;
        Psyz_SetTitle("Parasite Eve II - Settings");
        ResetGraph(0);
        DISPENV display;
        SetDefDispEnv(&display,0,0,320,240);
        PutDispEnv(&display);
        SetDispMask(1);
        Psyz_VideoVSync(0);
        settings();return 0;
    }
    if(!*discPaths[0]) {
        if(getenv("PSYZ_HEADLESS")) { fprintf(stderr,"Provide Disc 1 with --disc or PE2_DISC.\n");return -1; }
        if(!SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD))return -1;
        if(!chooseDiscs(NULL,0))return 0;
    }
    PeDisc* d=peDiscOpen(discPaths[0]);int valid=peDiscNumber(d)==1;peDiscClose(d);
    if(!valid) {fprintf(stderr,"Configured Disc 1 is unavailable or unsupported.\n");return -1;}
    saveOptions();*disc=discPaths[0];return 1;
}

int peHostSwapDisc(int number) {
    if(getenv("PE2_TRACE"))fprintf(stderr,"[disc] swap to disc %d requested (path: %s)\n",number,number>=1&&number<=2&&*discPaths[number-1]?discPaths[number-1]:"none");
    if(number<1||number>2)return 0;
    requiredDisc=number;selectedDisc=0;
    s_overlayState.page=3;s_overlayState.row=number-1;
    snprintf(statusText,sizeof statusText,"Please select USA Disc %d.",number);
    if(!*discPaths[number-1])settings();
    requiredDisc=0;
    if(!*discPaths[number-1] || s_overlayState.quit)return 0;
    PeDisc* d=peDiscOpen(discPaths[number-1]);
    if(peDiscNumber(d)!=number) {peDiscClose(d);return 0;}
    Psyz_CdShellOpen(1);
    if(Psyz_CdSetDiskPath(discPaths[number-1])) {Psyz_CdShellOpen(0);peDiscClose(d);return 0;}
    peAssetsLoad(d);
    peDiscClose(d);return 1;
}
