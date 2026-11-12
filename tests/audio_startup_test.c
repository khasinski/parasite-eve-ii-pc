#include <psyz.h>
#include <libgpu.h>
#include <libspu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned captured;
static void capture(const short* samples, int frames, void* userdata)
{
    (void)samples; (void)userdata;
    captured += frames;
}
void Psyz_PadsOverride(int port, char* frame, int len)
{
    (void)port; (void)frame; (void)len;
}
int main(void)
{
    ResetGraph(0);
    CHECK(Psyz_AudioIsPaused());
    /* A game may skip every XA movie. SPU initialization must still start
     * sound, before it uploads the first weapon/menu sample bank. */
    SpuInit();
    int started = !Psyz_AudioIsPaused();
    unsigned char bank[64];
    memset(bank,0x37,sizeof bank);
    Psyz_SpuMemWrite(0x1000,bank,sizeof bank);
    CHECK(!memcmp(Psyz_SpuGetRam()+0x1000,bank,sizeof bank));
    /* Later CD audio initialization must preserve those already loaded bytes. */
    CHECK(Psyz_AudioInit()==0);
    int preserved = !memcmp(Psyz_SpuGetRam()+0x1000,bank,sizeof bank);
    printf("SPU startup: mixer_started=%d bank_preserved=%d\n",started,preserved);
    CHECK(started && preserved);
    Psyz_AudioSetCaptureCallback(capture,NULL);
    CHECK(Psyz_AudioWaitForTimer()==1 && captured>0);
    Psyz_AudioSetCaptureCallback(NULL,NULL);
    Psyz_AudioDestroy();
    puts("SPU startup: audio starts without XA, bank survives later init, mixer advances");
    return 0;
}
