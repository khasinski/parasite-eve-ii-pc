/* SPDX-License-Identifier: CC0-1.0
 * Early CRT output must not use libc/errno/TLS before _init_env. */
#include <stddef.h>
#include <fcntl.h>
#include <SDL3/SDL_log.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
extern int sceKernelOpen(const char*, int, unsigned);
extern long sceKernelWrite(int, const void*, size_t);
extern int sceKernelFsync(int);
extern int sceKernelClose(int);
extern void ps5_fp_ieee(void);
static int logFd = -1, attempted;
static void rawWrite(int fd, const char* message)
{
    size_t left = 0;
    while (message[left]) ++left;
    while (left) {
        long written = sceKernelWrite(fd, message, left);
        if (written <= 0 || (size_t)written > left) break;
        message += written;
        left -= (size_t)written;
    }
}
void pe_application_stage(const char* message)
{
    /* Same native platform requirement as PE1: establish IEEE FP before
     * constructors, so subsequently created audio/GPU threads inherit it. */
    static int fpInitialized;
    if (!fpInitialized) { fpInitialized = 1; ps5_fp_ieee(); }
    /* Raw SDK flags WRONLY|APPEND|CREAT. Never truncate another session. The
     * USB mount must already exist; failure leaves only inherited output. */
    if (!attempted) {
        attempted = 1;
        int parent = sceKernelOpen("/mnt", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_NOFOLLOW, 0);
        if (parent >= 0 && sceKernelClose(parent) >= 0) {
            int mount = sceKernelOpen("/mnt/usb0", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_NOFOLLOW, 0);
            if (mount >= 0 && sceKernelClose(mount) >= 0)
                logFd = sceKernelOpen("/mnt/usb0/parasite-eve-ii-startup.log",
                    O_WRONLY|O_APPEND|O_CREAT|O_NOFOLLOW, 0666);
        }
    }
    rawWrite(1, message);
    if (logFd >= 0) { rawWrite(logFd, message); (void)sceKernelFsync(logFd); }
}
static void SDLCALL logOutput(void* unused, int category, SDL_LogPriority priority, const char* message)
{
    (void)unused;
    int saved = errno;
    static char lastVsync[512];
    if (priority == SDL_LOG_PRIORITY_INFO && message && strstr(message, "[ConfigureVSync]") && strlen(message) < sizeof lastVsync) {
        if (!strcmp(lastVsync, message)) { errno = saved; return; }
        strcpy(lastVsync, message);
    }
    char line[2048];
    snprintf(line, sizeof line, "SDL category=%d priority=%d: %s\n", category, (int)priority, message ? message : "(null)");
    pe_application_stage(line);
    errno = saved;
}
void pe_ps5_install_logging(void)
{
    /* Called only from main, after _init_env and constructors. */
    SDL_SetLogOutputFunction(logOutput, NULL);
    pe_application_stage("Parasite Eve II native: main entered\n");
}
