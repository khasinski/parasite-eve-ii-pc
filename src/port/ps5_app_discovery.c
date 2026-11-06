/* SPDX-License-Identifier: MIT
 * Active-binary identity policy adapted from PE1 Build18 pe-portable-root.c.
 * No PE1 data, IDs, writable storage policy or SDK structs are imported. */
#include "port/ps5_app_discovery.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
static char applicationReason[4608];
const char *pePs5ApplicationDiscoveryReason(void){return applicationReason;}
#ifdef PE2_PS5_HOST
extern int sceKernelOpen(const char *,int,unsigned);
extern long sceKernelRead(int,void *,size_t);
extern int sceKernelClose(int);
static unsigned budget;
static unsigned char a[4096],b[4096];
static int failed;
static int fail(const char *operation,const char *path,long raw,int observed){
 if(!failed)snprintf(applicationReason,sizeof applicationReason,
  "%s: raw=%lld (0x%08X), observed_errno=%d, path=%s",operation,
  (long long)raw,(unsigned)(uint32_t)raw,observed,path);
 failed=1;return -1;
}
static int missing(int r){return r==-ENOENT||r==-ENOTDIR||(unsigned)r==0x80020002u||(unsigned)r==0x80020014u;}
static int appDirectory(const char *root){
 char path[256];size_t length=strlen(root);if(length>=sizeof path)return fail("Build application directory",root,-ENAMETOOLONG,ENAMETOOLONG);
 strcpy(path,root);
 for(size_t i=1;i<=length;i++)if(i==length||path[i]=='/'){
  char saved=path[i];path[i]=0;errno=0;int fd=sceKernelOpen(path,O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_NOFOLLOW,0),observed=errno;
  if(fd<0)return missing(fd)?0:fail("Open application directory",path,fd,observed);
  errno=0;int result=sceKernelClose(fd);observed=errno;
  if(result<0)return fail("Close application directory",path,result,observed);
  path[i]=saved;
 }return 1;
}
static int readChunk(int fd,unsigned char *out,const char *path){
 unsigned n=0;
 while(n<4096){unsigned ask=4096-n;if(ask>budget)ask=budget;if(!ask)return fail("Application identity read budget",path,-EFBIG,EFBIG);
  errno=0;long got=sceKernelRead(fd,out+n,ask);int observed=errno;
  if(got<0||(unsigned long)got>ask)return fail("Read application identity",path,got,observed);
  if(!got)break;n+=(unsigned)got;budget-=(unsigned)got;
 }return (int)n;
}
static int sameBinary(const char *path){
 errno=0;int other=sceKernelOpen(path,O_RDONLY|O_NOFOLLOW,0),observed=errno;
 if(other<0)return missing(other)?0:fail("Open candidate application identity",path,other,observed);
 errno=0;int active=sceKernelOpen("/app0/eboot.bin",O_RDONLY|O_NOFOLLOW,0);observed=errno;
 if(active<0){fail("Open active application identity","/app0/eboot.bin",active,observed);sceKernelClose(other);return -1;}
 int result=0;unsigned total=0;
 for(;;){int x=readChunk(active,a,"/app0/eboot.bin"),y=readChunk(other,b,path);if(x<0||y<0){result=-1;break;}
  if(x!=y)break;if(!x){result=total>=4096?1:fail("Application identity too short",path,0,0);break;}
  if(memcmp(a,b,(size_t)x))break;total+=(unsigned)x;if(total>64u*1024u*1024u){result=fail("Application identity size cap",path,-EFBIG,EFBIG);break;}
 }
 errno=0;int closed=sceKernelClose(active);observed=errno;if(closed<0)result=fail("Close active application identity","/app0/eboot.bin",closed,observed);
 errno=0;closed=sceKernelClose(other);observed=errno;if(closed<0)result=fail("Close candidate application identity",path,closed,observed);return result;
}
#endif
int pePs5DiscoverApplication(char *out,size_t capacity){
 if(!out||!capacity)return -1;*out=0;
 snprintf(applicationReason,sizeof applicationReason,"No matching USB0 application copy.");
#ifdef PE2_PS5_HOST
 /* Current PE2 package receipt selects PPSA77023. This is candidate location,
  * NEVER application identity; all bytes must match actual /app0/eboot.bin. */
 const char *roots[]={"/mnt/usb0/homebrew/PPSA77023"};
 unsigned matches=0;budget=256u*1024u*1024u;failed=0;
 for(unsigned i=0;i<sizeof roots/sizeof roots[0];i++){
  int directory=appDirectory(roots[i]);if(directory<0){*out=0;return -1;}if(!directory)continue;
  char binary[256];int n=snprintf(binary,sizeof binary,"%s/eboot.bin",roots[i]);if(n<0||(size_t)n>=sizeof binary)return -1;
  int match=sameBinary(binary);if(match<0){*out=0;return -1;}
  if(match){if(++matches>1||strlen(roots[i])>=capacity){*out=0;return -2;}strcpy(out,roots[i]);}
 }
 return matches==1?1:0;
#else
 return 0;
#endif
}
