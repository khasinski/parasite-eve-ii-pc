#ifdef NDEBUG
#undef NDEBUG
#endif
#include "port/asset_bind.h"
#include "port/asset_parts.h"
#include "port/disc_assets.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned char actual[4];
static unsigned char later[4];
static unsigned char laterAgain[4];
PE_ASSET_BIND("test/disc", actual);
PE_ASSET_BIND("test/later", later);
PE_ASSET_BIND("test/later-again", laterAgain);
const PeAssetPackage gPeAssetPackages[]={{"test",1,0,0},{"later",4,0,0}};
const int gPeAssetPackageCount=2;
const PeAssetPart gPeAssetParts[]={
    {"test/disc",0,0,4,PE_ASSET_OP_COPY,0,0},
    {"test/later",1,0,4,PE_ASSET_OP_COPY,0,0},
    {"test/later-again",1,0,4,PE_ASSET_OP_COPY,0,0}};
const int gPeAssetPartCount=3;
struct PeDisc { unsigned char value; int readable; int laterReadable; };
static unsigned reads;
uint8_t* peDiscReadPackage(PeDisc* d,int stage,int folder,int file,size_t* size) {
    (void)folder;(void)file;reads++;
    if(!d->readable || (stage==4 && !d->laterReadable))return NULL;
    uint8_t* bytes=malloc(4);assert(bytes);memset(bytes,d->value,4);*size=4;return bytes;
}
uint8_t* peDiscReadFile(PeDisc* d,const char* name,size_t* size) {
    (void)d;(void)name;(void)size;assert(0);return NULL;
}
int main(void) {
    PeDisc first={0x11,1,0},second={0x22,1,1},missing={0,0,0};
    assert(peAssetsLoad(&first)==1 && actual[0]==0x11 && actual[3]==0x11);
    assert(later[0]==0 && laterAgain[0]==0);
    assert(reads==2); /* One read per package, including an absent package. */
    /* Stand in for an in-place TMD handler fixup made after initial loading. */
    actual[1]=0x7e;
    assert(peAssetsLoad(&missing)==0 && actual[1]==0x7e && later[0]==0);
    assert(peAssetsLoad(&second)==2 && later[0]==0x22 && later[3]==0x22);
    assert(laterAgain[0]==0x22 && laterAgain[3]==0x22);
    assert(actual[0]==0x11 && actual[1]==0x7e && actual[3]==0x11);
    assert(peAssetsLoad(&first)==0 && actual[1]==0x7e && later[0]==0x22);
    assert(reads==4);
    return 0;
}
