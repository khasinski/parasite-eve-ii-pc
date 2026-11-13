#ifndef PORT_PSYQ_COMPAT_H
#define PORT_PSYQ_COMPAT_H

/* Small PSY-Q definitions the game uses that PsyZ's headers leave out. */

#include <psyz.h>
#include <libgpu.h>
#include <libgte.h>

/* libgte: the GTE treats 4096 as 1.0. */
#ifndef ONE
#define ONE 4096
#endif

/* libspu: voice channel masks, 32-bit as on the PlayStation (`long` is 64-bit on the host). */
#ifndef SPU_KEYCH
#define SPU_KEYCH(x) ((unsigned int)1 << (x))
#endif
#ifndef SPU_VOICECH
#define SPU_VOICECH(x) SPU_KEYCH(x)
#endif

/* libgpu: fill a DR_TWIN texture-window packet; `p` is any primitive pointer of that size. */
#ifndef setTexWindow
#define setTexWindow(p, tw) SetTexWindow((DR_TWIN*)(p), (tw))
#endif

/* libpad connection states and PadInfoMode terms (documented in PsyZ, not declared). */
enum {
    PadStateDiscon = 0, PadStateFindPad = 1, PadStateFindCTP1 = 2,
    PadStateFindCTP2 = 3, PadStateReqInfo = 4, PadStateExecCmd = 5, PadStateStable = 6
};
enum { InfoModeCurID = 1, InfoModeCurExID = 2, InfoModeCurExOffs = 3, InfoModeIdTable = 4 };

#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif

/* Calls PsyZ leaves out; implemented by the host platform layer. */
int CdDiskReady(int mode);
int CdGetDiskType(void);
void StCdInterrupt(void);
#include "port/heap.h"

/* The SDK's VectorNormal returns the length of v0; PsyZ's is void. */
#include <math.h>
static inline long PortVectorNormal(VECTOR* v0, VECTOR* v1)
{
    double x = v0->vx, y = v0->vy, z = v0->vz;
    long   length = (long)sqrt(x * x + y * y + z * z);
    (VectorNormal)(v0, v1);
    return length;
}
#define VectorNormal(v0, v1) PortVectorNormal((v0), (v1))

/* CD sector size in 32-bit words, as libcd documents it. */
#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512
#endif

/* libgte/libmath: Q12 natural logarithm (cln(4096) == 0, cln(8192) == 2839). */
long cln(long x);

/* libgs world-screen matrices (defined in main/gfxlight.c). */
extern MATRIX GsWSMATRIX;
extern MATRIX GsLIGHTWSMATRIX;

/* libgte matrix-vector products PsyZ does not declare; see psyq_host.c. */
VECTOR* ApplyMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1);
VECTOR* ApplyTransposeMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1);
SVECTOR* ApplyMatrixSV(MATRIX* m, SVECTOR* v0, SVECTOR* v1);
/* libgte MIMe: out[i] += (delta[i] * weight) >> 12 for XYZ (weight in Q12), saturated to 16 bits; w is untouched. */
/* libgte rotation in the order Rz * Rx * Ry (PsyZ has YXZ and ZYX but not this one). */
MATRIX* RotMatrixZXY(SVECTOR* r, MATRIX* m);
/* libgte scale from the left: row i of the rotation is multiplied by v[i] (world-axis scale); translation is kept. */
MATRIX* ScaleMatrixL(MATRIX* m, VECTOR* v);
void gteMIMefunc(SVECTOR* out, const SVECTOR* delta, int count, int weight);

/* GTE LCOL colour blend: out = (c0 * w0 + c1 * w1) >> 12 per channel (r, g, b). */
void LoadAverageCol(unsigned char* c0, unsigned char* c1, int w0, int w1, unsigned char* out);

/* PS1 main RAM (2 MiB at 0x80000000) is an ordinary host array; fixed game addresses translate through this. */
#define PS1_RAM_BASE_ADDRESS 0x80000000u
extern unsigned char gHostRam[0x200000];
#define PS1_RAM_PTR(addr) ((void*)(gHostRam + ((addr) - PS1_RAM_BASE_ADDRESS)))

/* PsyZ libcd: deliver the pending CdSync/CdReady callbacks (VSync does this implicitly). */
void Psyz_CdPump(void);
void Psyz_PollInterrupts(void);

/* Disc data stores PS1 addresses in 32-bit fields; this maps one to the host RAM array (0 stays NULL). */
static inline void* Ps1AddrToHost(unsigned int addr)
{
    unsigned int off = addr & 0x1FFFFFu;
    if (addr == 0) {
        return 0;
    }
    return (void*)(gHostRam + off);
}

#endif
