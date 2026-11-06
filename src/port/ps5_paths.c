/* Original PE2 storage policy, CC0-1.0. Existing mounted USB roots only. */
#include "port/ps5_paths.h"
#include "port/ps5_app_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#ifdef PE2_PS5_HOST
extern int sceKernelOpen(const char *,int,unsigned);
extern long sceKernelRead(int,void *,size_t);
extern int sceKernelClose(int);
extern int sceKernelMkdir(const char *,unsigned);
#endif
static int discOpen(const char *,int);
static int discClose(int);
static int missingLeaf(long raw){return raw==-ENOENT||(uint32_t)raw==0x80020002u;}
static char discovered[2][4096],configRoot[4096],cardDirectoryRoot[4096];
static const char *storageReason="Writable USB0 storage has not been selected.";
static char storageFailure[4608];
static int storageFail(const char *operation,const char *path,int error){
 snprintf(storageFailure,sizeof storageFailure,"%s: errno=%d (%s), path=%s",operation,error,strerror(error),path?path:"(null)");
 storageReason=storageFailure;return 0;
}
static int storageRawFail(const char *operation,const char *path,long raw,int observed){
 snprintf(storageFailure,sizeof storageFailure,"%s: raw=%lld (0x%08X), observed_errno=%d, path=%s",
  operation,(long long)raw,(unsigned)(uint32_t)raw,observed,path?path:"(null)");
 storageReason=storageFailure;return 0;
}
static int storageMkdir(const char *path){
#ifdef PE2_PS5_HOST
 return sceKernelMkdir(path,0700);
#else
 int result=mkdir(path,0700);return result<0?-errno:result;
#endif
}
static size_t mountLength(const char *path){
 if(!strncmp(path,"/mnt/usb",8)&&path[8]>='0'&&path[8]<='7'&&(!path[9]||path[9]=='/'))return 9;
 return 0;
}
static int lexical(const char *path){
 if(!path||!*path||strlen(path)>=4096||!mountLength(path))return 0;
 for(const char *q=path;*q;q++)if((unsigned char)*q<32||*q==127||*q=='\\'||*q==':')return 0;
 for(const char *q=path+1;*q;){const char *end=strchr(q,'/');size_t n=end?(size_t)(end-q):strlen(q);
  if(!n||(n==1&&q[0]=='.')||(n==2&&q[0]=='.'&&q[1]=='.'))return 0;
  if(!end)break;q=end+1;if(!*q)return 0;
 }
 return 1;
}
/* No libc realpath or native struct-stat interpretation: the SDK's actual
 * O_DIRECTORY contract rejects non-directories, O_NOFOLLOW rejects a symlink
 * at EVERY absolute component, including /mnt and the allowed mount itself.
 * No *at import (unavailable to this title), raw syscall or fallback root. */
static int checkedPath(const char *chosen,int missingFinal,int finalDirectory){
 char path[4096];
 if(!lexical(chosen))return storageFail("Rejected writable path",chosen,EINVAL);
 strcpy(path,chosen);size_t length=strlen(path);
 for(size_t i=1;i<=length;i++)if(i==length||path[i]=='/'){
  char saved=path[i];path[i]=0;int final=i==length;
  int flags=O_RDONLY|O_NONBLOCK|O_NOFOLLOW;
  if(!final||finalDirectory)flags|=O_DIRECTORY;
  errno=0;int fd=discOpen(path,flags);int error=errno;
  if(fd<0){
   if(final&&missingFinal&&missingLeaf(fd))return 1;
   return storageRawFail("Open no-follow storage component",path,fd,error);
  }
  errno=0;int closed=discClose(fd);error=errno;
  if(closed<0)return storageRawFail("Close storage component",path,closed,error);
  path[i]=saved;
 }
 return 1;
}
int pePs5WritePathAllowed(const char *path){return checkedPath(path,1,0);}
static const char *directory(const char *chosen,char output[4096]){
 char path[4096];output[0]=0;
 if(!chosen||!*chosen||strlen(chosen)>=sizeof path){storageFail("Invalid writable directory",chosen,EINVAL);return NULL;}
 strcpy(path,chosen);size_t length=strlen(path);while(length>1&&path[length-1]=='/')path[--length]=0;
 if(!lexical(path)){storageFail("Rejected writable directory",path,EINVAL);return NULL;}
 size_t mount=mountLength(path);char saved=path[mount];path[mount]=0;
 /* Existing /mnt and mount must be real directories, never created here. */
 if(!checkedPath(path,0,1))return NULL;
 path[mount]=saved;
 for(size_t i=mount+1;i<=length;i++)if(i==length||path[i]=='/'){
  saved=path[i];path[i]=0;
  if(!checkedPath(path,1,1))return NULL;
  /* An existing directory is accepted by open; mkdir is needed only for a
   * positively ENOENT final component, never an absent mount or ancestor. */
  errno=0;int fd=discOpen(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_DIRECTORY),observed=errno;
  if(fd>=0){errno=0;int closed=discClose(fd);observed=errno;
   if(closed<0){storageRawFail("Close writable directory",path,closed,observed);return NULL;}}
  else {
   if(!missingLeaf(fd)){storageRawFail("Open writable directory",path,fd,observed);return NULL;}
   errno=0;int created=storageMkdir(path);observed=errno;
   if(created!=0){storageRawFail("Create writable directory",path,created,observed);return NULL;}
   if(!checkedPath(path,0,1))return NULL;
  }
  path[i]=saved;
 }
 if(!checkedPath(path,0,1))return NULL;
 strcpy(output,path);
 storageReason="Writes use selected existing USB storage; no migration or /data fallback.";return output;
}
const char* pePs5ConfigDirectory(void){
 const char *override=getenv("PE2_CONFIG_DIR");
 return directory(override&&*override?override:"/mnt/usb0/parasite-eve-ii",configRoot);
}
const char* pePs5CardDirectory(void){
 const char *override=getenv("PE2_MEMORY_CARD_DIR");
 return directory(override&&*override?override:"/mnt/usb0/parasite-eve-ii/memory-cards",cardDirectoryRoot);
}
const char* pePs5StorageReason(void){return storageReason;}

/* Defaults are selected as a PAIR once. Explicit/configured paths are never
 * overwritten: companion discovery is confined to that explicit directory. */
static int defaultsInitialized,discDiscoveryError;
static const char *discDiscoveryReason="Disc defaults not yet discovered.";
static char discDiscoveryFailure[4608];
static char defaults[2][4096];
const char *pePs5DiscDiscoveryReason(void){return discDiscoveryReason;}
int pePs5DiscDiscoveryError(void){return discDiscoveryError;}
/* Native libkernel returns negative FreeBSD errno OR encoded SCE status, not
 * a guaranteed libc errno. Recognize only the two actual missing-path forms;
 * every other status is retained verbatim for an actionable first failure. */
static int discMissing(long raw){
 return raw==-ENOENT||raw==-ENOTDIR||(uint32_t)raw==0x80020002u||(uint32_t)raw==0x80020014u;
}
static int discFail(const char *operation,const char *path,long raw,int observedErrno){
 snprintf(discDiscoveryFailure,sizeof discDiscoveryFailure,
  "%s: raw=%lld (0x%08X), observed_errno=%d, path=%s",operation,
  (long long)raw,(unsigned)(uint32_t)raw,observedErrno,path?path:"(null)");
 discDiscoveryError=1;discDiscoveryReason=discDiscoveryFailure;return -1;
}
static int discOpen(const char *path,int flags){
#ifdef PE2_PS5_HOST
 return sceKernelOpen(path,flags,0);
#else
 int fd=open(path,flags);return fd<0?-errno:fd;
#endif
}
static long discRead(int fd,void *out,size_t bytes){
#ifdef PE2_PS5_HOST
 return sceKernelRead(fd,out,bytes);
#else
 long n=(long)read(fd,out,bytes);return n<0?-errno:n;
#endif
}
static int discClose(int fd){
#ifdef PE2_PS5_HOST
 return sceKernelClose(fd);
#else
 int r=close(fd);return r<0?-errno:r;
#endif
}
/* Positively open each absolute directory component. No stat layout, realpath,
 * unsupported *at function, directory creation or symlink following. Optional
 * absent mounts/layouts never reach per-file alias probing. */
static int discDirectory(const char *chosen){
 char path[4096];
 if(!chosen||chosen[0]!='/'||strlen(chosen)>=sizeof path)
  return discFail("Invalid discovery directory",chosen,-EINVAL,EINVAL);
 strcpy(path,chosen);size_t length=strlen(path);
 for(size_t i=1;i<=length;i++)if(i==length||path[i]=='/'){
  char saved=path[i];path[i]=0;errno=0;
  int fd=discOpen(path,O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_NOFOLLOW),observed=errno;
  if(fd<0){int absent=discMissing(fd);if(!absent)discFail("Open discovery directory",path,fd,observed);return absent?0:-1;}
  errno=0;int result=discClose(fd);observed=errno;
  if(result<0)return discFail("Close discovery directory",path,result,observed);
  path[i]=saved;
 }
 return 1;
}
static int readCandidate(const char *path){
 errno=0;int fd=discOpen(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW),observed=errno;
 if(fd<0)return discMissing(fd)?0:discFail("Open disc candidate",path,fd,observed);
 unsigned char byte;errno=0;long count=discRead(fd,&byte,1);observed=errno;
 if(count<0){discFail("Read disc candidate",path,count,observed);discClose(fd);return -1;}
 if(count>1){discFail("Invalid disc read length",path,count,observed);discClose(fd);return -1;}
 errno=0;int result=discClose(fd);observed=errno;
 if(result<0)return discFail("Close disc candidate",path,result,observed);
 return count==0?2:1; /* absent, IO, empty, readable */
}
static int findAt(int number,const char *base,char out[4096]){
 static const char *names[2][3]={{"SLUS-01042.cue","Parasite Eve II (USA) (Disc 1).cue","disc1.bin"},
 {"SLUS-01055.cue","Parasite Eve II (USA) (Disc 2).cue","disc2.bin"}};
 int present=0;out[0]=0;
 for(unsigned i=0;i<3;i++){
  char path[4096];int n=snprintf(path,sizeof path,"%s/%s",base,names[number-1][i]);
  if(n<0||(size_t)n>=sizeof path)return discFail("Build disc candidate path",base,-ENAMETOOLONG,ENAMETOOLONG);
  int state=readCandidate(path);if(state<0)return -1;
  if(state==1){strcpy(out,path);return 1;}if(state==2)present=1;
 }return present?2:0;
}
static int pairAt(const char *base,char pair[2][4096]){
 int directoryState=discDirectory(base);if(directoryState<=0)return directoryState;
 int first=findAt(1,base,pair[0]);if(first<0)return -1;
 int second=findAt(2,base,pair[1]);if(second<0)return -1;
 return first==1&&second==1?1:(first||second)?2:0;
}
static void discoverDefaults(void){
 if(defaultsInitialized)return;defaultsInitialized=1;
 char app[4096],base[4096],pair[2][4096];int selected=pePs5DiscoverApplication(app,sizeof app);
 if(selected<0){discDiscoveryError=1;discDiscoveryReason=selected==-2?"Multiple matching application copies; automatic disc discovery stopped.":pePs5ApplicationDiscoveryReason();return;}
 if(selected==1){
  int n=snprintf(base,sizeof base,"%s/parasite-eve-ii/discs",app);
  if(n<0||(size_t)n>=sizeof base){discDiscoveryError=1;discDiscoveryReason="Application disc path exceeds bounds.";return;}
  int state=pairAt(base,pair);
  if(state<0)return; /* Keep exact operation/path/native status; no fallback. */
  if(state==1){memcpy(defaults,pair,sizeof defaults);discDiscoveryReason="Complete app-contained pair selected; disc identity validation follows.";return;}
 }
 unsigned complete=0;int incomplete=0;
 /* Read-only enumeration. Writable settings/cards policy above is independent. */
 static const char *roots[]={"/mnt/usb0/parasite-eve-ii"};
 for(unsigned volume=0;volume<sizeof roots/sizeof roots[0];volume++){
  strcpy(base,roots[volume]);
  /* Two supported layouts are distinct roots; never join their partial pairs. */
  for(unsigned layout=0;layout<2;layout++){
   char folder[4096];int n=snprintf(folder,sizeof folder,"%s%s",base,layout?"/discs":"");
   if(n<0||(size_t)n>=sizeof folder){discFail("Build external disc directory",base,-ENAMETOOLONG,ENAMETOOLONG);return;}
   int state=pairAt(folder,pair);if(state<0){memset(defaults,0,sizeof defaults);return;}
   else if(state==1){complete++;memcpy(defaults,pair,sizeof defaults);}else if(state==2)incomplete=1;
  }
 }
 if(complete>1){memset(defaults,0,sizeof defaults);discDiscoveryError=1;discDiscoveryReason="Multiple complete external pairs; configure explicit disc paths.";return;}
 discDiscoveryReason=complete?"Unique complete external pair selected; disc identity validation follows.":incomplete?"No complete pair: partial or empty disc files remain separate.":"No complete app-contained or external disc pair found.";
}
const char* pePs5FindDisc(int number,const char* companion){
 if(number!=1&&number!=2)return NULL;
 if(companion&&*companion){
  const char *slash=strrchr(companion,'/');if(!slash||slash==companion)return NULL;
  size_t length=(size_t)(slash-companion);if(length>=4096)return NULL;
  char base[4096];memcpy(base,companion,length);base[length]=0;
  int directoryState=discDirectory(base);if(directoryState<=0)return NULL;
  int state=findAt(number,base,discovered[number-1]);
  /* Explicit single-disc selection never borrows a default from another root. */
  return state==1?discovered[number-1]:NULL;
 }
 discoverDefaults();
 if(discDiscoveryError){fprintf(stderr,"[ps5] %s\n",discDiscoveryReason);return NULL;}
 return *defaults[number-1]?defaults[number-1]:NULL;
}
