/* Crash reporter: see include/port/crash_report.h.
 *
 * Everything the handler touches is prepared outside it: paths, the version and OS lines, the watch table, the
 * alternate signal stack, and (POSIX) one backtrace() call so its unwinder library is already loaded. The handler
 * itself formats numbers by hand and writes with write()/WriteFile. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* dladdr */
#endif
#include "port/crash_report.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/utsname.h>
#ifdef __APPLE__
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
#include <unistd.h>
#endif

enum { MAX_WATCHES = 32, MAX_FRAMES = 64, LOG_TAIL_BYTES = 12288, PATH_BYTES = 4096 };
static struct { const char* label; const volatile unsigned char* address; int bytes, kind; } watches[MAX_WATCHES];
static int watchCount;
static char version[160] = "unknown", osLine[256] = "unknown", imageLine[PATH_BYTES + 64];
static char session[40], startedLine[64];
static char reportPath[PATH_BYTES], markerPath[PATH_BYTES], logPath[PATH_BYTES];
static volatile sig_atomic_t inHandler;
/* One report per process: a sanitizer's handler that runs after ours may abort(), which must not replace it. */
static volatile sig_atomic_t reported;
static int installed;

#if defined(__aarch64__) || defined(_M_ARM64)
#define PE_CRASH_ARCH "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
#define PE_CRASH_ARCH "x86-64"
#elif defined(__i386__) || defined(_M_IX86)
#define PE_CRASH_ARCH "x86"
#else
#define PE_CRASH_ARCH "unknown architecture"
#endif

/* ---- output primitives (async-signal-safe) ---- */
#ifdef _WIN32
typedef HANDLE Sink;
#define NO_SINK INVALID_HANDLE_VALUE
static void putn(Sink s, const char* text, size_t n) {
    while (s != NO_SINK && n) {
        DWORD done = 0;
        if (!WriteFile(s, text, (DWORD)(n > 65536 ? 65536 : n), &done, NULL) || !done) return;
        text += done; n -= done;
    }
}
#else
typedef int Sink;
#define NO_SINK (-1)
static void putn(Sink s, const char* text, size_t n) {
    while (s != NO_SINK && n) {
        ssize_t done = write(s, text, n);
        if (done < 0 && errno == EINTR) continue;
        if (done <= 0) return;
        text += done; n -= (size_t)done;
    }
}
#endif
static void put(Sink s, const char* text) { putn(s, text, strlen(text)); }
static void putu(Sink s, uint64_t v) {
    char b[24]; int i = sizeof b;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    putn(s, b + i, sizeof b - i);
}
static void putd(Sink s, int64_t v) {
    if (v < 0) { put(s, "-"); putu(s, (uint64_t)0 - (uint64_t)v); } else putu(s, (uint64_t)v);
}
static void putx(Sink s, uint64_t v) {
    char b[20]; int i = sizeof b;
    do { b[--i] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
    b[--i] = 'x'; b[--i] = '0';
    putn(s, b + i, sizeof b - i);
}

/* ---- set-up (normal context) ---- */
static void copyText(char* dst, size_t cap, const char* src) {
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = 0;
}

int peCrashWatch(const char* label, const void* address, int bytes, int kind) {
    if (watchCount >= MAX_WATCHES || !label || !address) return 0;
    if (kind == PE_CRASH_WATCH_CODE) bytes = (int)sizeof(void*);
    if (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) return 0;
    watches[watchCount].label = label;
    watches[watchCount].address = address;
    watches[watchCount].bytes = bytes;
    watches[watchCount].kind = kind;
    watchCount++;
    return 1;
}

const char* peCrashReportPath(void) { return reportPath; }

static void joinPath(char* out, const char* folder, const char* name) {
    size_t n = strlen(folder);
    int slash = n && (folder[n - 1] == '/' || folder[n - 1] == '\\');
    if (snprintf(out, PATH_BYTES, "%s%s%s", folder, slash ? "" : "/", name) >= PATH_BYTES) out[0] = 0;
}

#ifdef _WIN32
static wchar_t reportPathW[PATH_BYTES], markerPathW[PATH_BYTES], logPathW[PATH_BYTES], dumpPathW[PATH_BYTES];
typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
static MiniDumpWriteDumpFn miniDumpWriteDump;
static LPTOP_LEVEL_EXCEPTION_FILTER previousFilter;
static void widen(wchar_t* out, const char* text) {
    out[0] = 0;
    if (text && *text && !MultiByteToWideChar(CP_UTF8, 0, text, -1, out, PATH_BYTES)) out[0] = 0;
}
#endif

void peCrashSetOutput(const char* folder, const char* log, const char* stamp) {
    char name[128];
    time_t now = time(NULL);
    struct tm local;
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    if (stamp && *stamp) copyText(session, sizeof session, stamp);
    else strftime(session, sizeof session, "%Y%m%d-%H%M%S", &local);
    strftime(startedLine, sizeof startedLine, "%Y-%m-%d %H:%M:%S", &local);
    reportPath[0] = markerPath[0] = 0;
    if (folder && *folder) {
        snprintf(name, sizeof name, "parasite-eve-ii-crash-%s.log", session);
        joinPath(reportPath, folder, name);
        joinPath(markerPath, folder, "parasite-eve-ii-crash.last");
    }
    copyText(logPath, sizeof logPath, log);
#ifdef _WIN32
    widen(reportPathW, reportPath);
    widen(markerPathW, markerPath);
    widen(logPathW, logPath);
    dumpPathW[0] = 0;
    if (folder && *folder) {
        char dump[PATH_BYTES];
        snprintf(name, sizeof name, "parasite-eve-ii-crash-%s.dmp", session);
        joinPath(dump, folder, name);
        widen(dumpPathW, dump);
    }
#endif
}

int peCrashTakePreviousReport(const char* folder, char* out, unsigned outSize) {
    char marker[PATH_BYTES], line[PATH_BYTES];
    if (!folder || !*folder || !out || !outSize) return 0;
    joinPath(marker, folder, "parasite-eve-ii-crash.last");
    FILE* f = marker[0] ? fopen(marker, "r") : NULL;
    if (!f) return 0;
    int got = fgets(line, sizeof line, f) != NULL;
    fclose(f);
    remove(marker);
    if (!got) return 0;
    line[strcspn(line, "\r\n")] = 0;
    FILE* report = line[0] ? fopen(line, "r") : NULL;
    if (!report) return 0;
    fclose(report);
    copyText(out, outSize, line);
    return 1;
}

/* ---- the report ---- */
typedef struct {
    const char* what;    /* "signal 11 (SIGSEGV)" style text, built by the caller */
    uint64_t code;       /* signal number or exception code */
    const char* detail;  /* si_code / access kind, may be NULL */
    int hasAddress;
    uint64_t address;    /* fault address */
    uint64_t pc, lr, sp; /* 0 when unknown */
    void** frames;
    int frameCount;
} CrashInfo;

static void putCode(Sink s, void* address);

static void putWatches(Sink s) {
    if (!watchCount) return;
    put(s, "\nGame state:\n");
    for (int i = 0; i < watchCount; i++) {
        uint64_t raw = 0;
        const volatile unsigned char* p = watches[i].address;
        /* Little-endian hosts only (all supported targets). */
        for (int b = 0; b < watches[i].bytes; b++) raw |= (uint64_t)p[b] << (8 * b);
        put(s, "  "); put(s, watches[i].label); put(s, ": ");
        if (watches[i].kind == PE_CRASH_WATCH_CODE) { putCode(s, (void*)(uintptr_t)raw); continue; }
        if (watches[i].kind == PE_CRASH_WATCH_SIGNED) {
            int shift = 64 - 8 * watches[i].bytes;
            putd(s, shift ? (int64_t)(raw << shift) >> shift : (int64_t)raw);
        } else putu(s, raw);
        put(s, "\n");
    }
}

static void putHeader(Sink s, const CrashInfo* c) {
    put(s, "Parasite Eve II PC port crash report\n");
    put(s, "Version: "); put(s, version); put(s, "\n");
    put(s, "System: "); put(s, osLine); put(s, ", built for "); put(s, PE_CRASH_ARCH); put(s, "\n");
    if (imageLine[0]) { put(s, "Image: "); put(s, imageLine); put(s, "\n"); }
    if (startedLine[0]) { put(s, "Started: "); put(s, startedLine); put(s, " (local time)\n"); }
#ifdef _WIN32
    {
        FILETIME now; /* 100 ns units since 1601 */
        GetSystemTimeAsFileTime(&now);
        uint64_t ticks = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
        put(s, "Crashed: unix time "); putu(s, ticks / 10000000u - 11644473600u); put(s, "\n");
    }
#else
    {
        struct timespec now;
        if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
            put(s, "Crashed: unix time "); putu(s, (uint64_t)now.tv_sec); put(s, "\n");
        }
    }
#endif
    put(s, "\nCrash: "); put(s, c->what);
    if (c->detail) { put(s, ", "); put(s, c->detail); }
    put(s, "\n");
    if (c->hasAddress) { put(s, "Fault address: "); putx(s, c->address); put(s, "\n"); }
    if (c->pc) { put(s, "pc: "); putCode(s, (void*)(uintptr_t)c->pc); }
    if (c->lr) { put(s, "lr: "); putCode(s, (void*)(uintptr_t)c->lr); }
    if (c->sp) { put(s, "sp: "); putx(s, c->sp); put(s, "\n"); }
}

static void putFrames(Sink s, const CrashInfo* c);

static void putLogTail(Sink s);

static void writeReport(const CrashInfo* c, Sink err) {
    Sink report = NO_SINK;
    if (reportPath[0]) {
#ifdef _WIN32
        report = CreateFileW(reportPathW, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
#else
        report = open(reportPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
    }
    if (report != NO_SINK) {
        putHeader(report, c);
        putWatches(report);
        putFrames(report, c);
        putLogTail(report);
#ifdef _WIN32
        CloseHandle(report);
        HANDLE marker = CreateFileW(markerPathW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        put(marker, reportPath); put(marker, "\n");
        if (marker != NO_SINK) CloseHandle(marker);
#else
        close(report);
        Sink marker = open(markerPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        put(marker, reportPath); put(marker, "\n");
        if (marker != NO_SINK) close(marker);
#endif
    }
    /* The normal log (or the terminal): a marker line, then the same report without the log tail. */
    put(err, "\n*** CRASH *** ");
    put(err, c->what);
    put(err, report != NO_SINK ? "; crash report written to " : (reportPath[0] ? "; could not write the crash report " : ""));
    put(err, reportPath);
    put(err, "\n");
    putHeader(err, c);
    putWatches(err);
    putFrames(err, c);
    put(err, "*** END OF CRASH REPORT ***\n");
}

#ifndef _WIN32
/* ---- POSIX ---- */
/* SIGTRAP: __builtin_trap and trapping sanitizer checks on arm64 raise it instead of SIGILL. */
static const int fatalSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP };
enum { FATAL_COUNT = sizeof fatalSignals / sizeof fatalSignals[0] };
static struct sigaction previous[FATAL_COUNT];
static char altStack[256 * 1024];

static void putCode(Sink s, void* address) {
    putx(s, (uint64_t)(uintptr_t)address);
    put(s, "  ");
    if (!address) { put(s, "(null)\n"); return; }
    void* one[1] = { address };
    backtrace_symbols_fd(one, 1, s); /* writes its own newline */
}

static void putFrames(Sink s, const CrashInfo* c) {
    put(s, "\nBacktrace (");
    putd(s, c->frameCount);
    put(s, " frames, innermost first; the crash handler's own frames come first):\n");
    backtrace_symbols_fd(c->frames, c->frameCount, s);
}

static void putLogTail(Sink s) {
    static char tail[LOG_TAIL_BYTES];
    if (!logPath[0]) return;
    int fd = open(logPath, O_RDONLY);
    if (fd < 0) return;
    off_t size = lseek(fd, 0, SEEK_END);
    off_t start = size > LOG_TAIL_BYTES ? size - LOG_TAIL_BYTES : 0;
    ssize_t n = size > 0 && lseek(fd, start, SEEK_SET) == start ? read(fd, tail, sizeof tail) : 0;
    close(fd);
    if (n <= 0) return;
    char* p = tail;
    if (start > 0) { /* begin at a whole line */
        char* nl = memchr(tail, '\n', (size_t)n);
        if (nl) p = nl + 1;
    }
    put(s, "\nLast log output (");
    put(s, logPath);
    put(s, "):\n");
    putn(s, p, (size_t)(tail + n - p));
    if (tail[n - 1] != '\n') put(s, "\n");
}

static const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGFPE: return "SIGFPE (arithmetic exception)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGTRAP: return "SIGTRAP (trap instruction)";
    default: return "unknown signal";
    }
}

static const char* codeName(int sig, int code, int fromPid) {
    if (code == SI_USER) return "sent by kill/raise";
#ifdef __APPLE__
    if (fromPid == 0 && sig == SIGSEGV && code == SEGV_ACCERR) return "access not permitted (or sent by kill)";
#endif
    (void)fromPid;
#ifdef SI_TKILL
    if (code == SI_TKILL) return "sent by tkill/raise";
#endif
    if (sig == SIGSEGV && code == SEGV_MAPERR) return "address not mapped";
    if (sig == SIGSEGV && code == SEGV_ACCERR) return "access not permitted";
    if (sig == SIGBUS && code == BUS_ADRALN) return "misaligned address";
    if (sig == SIGBUS && code == BUS_ADRERR) return "nonexistent physical address";
    if (sig == SIGFPE && code == FPE_INTDIV) return "integer divide by zero";
    if (sig == SIGILL && code == ILL_ILLOPC) return "illegal opcode";
    return NULL;
}

static void crashHandler(int sig, siginfo_t* info, void* context) {
    int slot = 0;
    while (slot < FATAL_COUNT && fatalSignals[slot] != sig) slot++;
    if (inHandler) { /* crashed while reporting: let it die */
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    inHandler = 1;
    static void* frames[MAX_FRAMES];
    static char what[96];
    CrashInfo c = { 0 };
    {
        char* w = what;
        const char* name = signalName(sig);
        const char* prefix = "signal ";
        while (*prefix) *w++ = *prefix++;
        char digits[8]; int d = 0; int v = sig;
        do { digits[d++] = (char)('0' + v % 10); v /= 10; } while (v && d < 8);
        while (d) *w++ = digits[--d];
        *w++ = ' ';
        while (*name && w < what + sizeof what - 1) *w++ = *name++;
        *w = 0;
    }
    c.what = what;
    c.code = (uint64_t)sig;
    c.detail = info ? codeName(sig, info->si_code, (int)info->si_pid) : NULL;
    if (info && sig != SIGABRT) { c.hasAddress = 1; c.address = (uint64_t)(uintptr_t)info->si_addr; }
    ucontext_t* uc = context;
    (void)uc;
#if defined(__APPLE__) && defined(__aarch64__)
    if (uc && uc->uc_mcontext) {
        c.pc = uc->uc_mcontext->__ss.__pc; c.lr = uc->uc_mcontext->__ss.__lr; c.sp = uc->uc_mcontext->__ss.__sp;
    }
#elif defined(__APPLE__) && defined(__x86_64__)
    if (uc && uc->uc_mcontext) { c.pc = uc->uc_mcontext->__ss.__rip; c.sp = uc->uc_mcontext->__ss.__rsp; }
#elif defined(__linux__) && defined(__x86_64__)
    if (uc) { c.pc = (uint64_t)uc->uc_mcontext.gregs[REG_RIP]; c.sp = (uint64_t)uc->uc_mcontext.gregs[REG_RSP]; }
#elif defined(__linux__) && defined(__aarch64__)
    if (uc) { c.pc = uc->uc_mcontext.pc; c.lr = uc->uc_mcontext.regs[30]; c.sp = uc->uc_mcontext.sp; }
#endif
    c.frames = frames;
    if (!reported) {
        reported = 1;
        c.frameCount = backtrace(frames, MAX_FRAMES);
        writeReport(&c, STDERR_FILENO);
    }

    /* Hand the signal on as if this handler were not installed: to the default action (core dump, the OS crash
     * reporter) or to a handler installed before ours, such as a sanitizer's. The default action is raised again
     * (it is delivered when we return). A real fault re-executes on return and so reaches the earlier handler with
     * its own siginfo; a signal that was sent is raised again. macOS reports kill(SIGSEGV) with SEGV_ACCERR, so a
     * sent SIGSEGV is only told apart from a fault for the default action, which is raised either way. */
    int useDefault = 1;
    if (slot < FATAL_COUNT) {
        struct sigaction next = previous[slot];
        if (next.sa_flags & SA_SIGINFO) useDefault = 0;
        else if (next.sa_handler == SIG_IGN) next.sa_handler = SIG_DFL;
        else if (next.sa_handler != SIG_DFL) useDefault = 0;
        sigaction(sig, &next, NULL);
    } else signal(sig, SIG_DFL);
    int sent = useDefault || !info || sig == SIGABRT || info->si_code == SI_USER || info->si_code <= 0;
#ifdef SI_QUEUE
    if (info && info->si_code == SI_QUEUE) sent = 1;
#endif
    inHandler = 0;
    if (sent) raise(sig);
}

static void describeSystem(void) {
    struct utsname u;
    if (uname(&u) == 0) snprintf(osLine, sizeof osLine, "%s %s on %s", u.sysname, u.release, u.machine);
    Dl_info image;
    if (dladdr((void*)&peCrashInstall, &image) && image.dli_fname)
        snprintf(imageLine, sizeof imageLine, "%s loaded at %p", image.dli_fname, image.dli_fbase);
}

void peCrashInstall(const char* text) {
    if (text && *text) copyText(version, sizeof version, text);
    if (installed) return;
    installed = 1;
    describeSystem();
    void* warm[4];
    backtrace(warm, 4); /* loads the unwinder now: the first call may allocate */
    /* Overflowing the stack leaves no room for a handler; give it its own. Keep one that a sanitizer already set. */
    stack_t current;
    if (sigaltstack(NULL, &current) != 0 || (current.ss_flags & SS_DISABLE) || current.ss_size < sizeof altStack) {
        stack_t ours = { .ss_sp = altStack, .ss_size = sizeof altStack, .ss_flags = 0 };
        sigaltstack(&ours, NULL);
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crashHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < FATAL_COUNT; i++) sigaction(fatalSignals[i], &sa, &previous[i]);
}

#else
/* ---- Windows ---- */
static void putCode(Sink s, void* address) {
    putx(s, (uint64_t)(uintptr_t)address);
    HMODULE module = NULL;
    wchar_t wide[MAX_PATH];
    char name[MAX_PATH * 3];
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      (LPCWSTR)address, &module) && module) {
        DWORD n = GetModuleFileNameW(module, wide, MAX_PATH);
        if (n && n < MAX_PATH) {
            wchar_t* base = wide;
            for (wchar_t* p = wide; *p; p++) if (*p == L'\\' || *p == L'/') base = p + 1;
            if (WideCharToMultiByte(CP_UTF8, 0, base, -1, name, sizeof name, NULL, NULL)) {
                put(s, "  "); put(s, name); put(s, "+");
                putx(s, (uint64_t)((uintptr_t)address - (uintptr_t)module));
            }
        }
    }
    put(s, "\n");
}

static void putFrames(Sink s, const CrashInfo* c) {
    put(s, "\nBacktrace (");
    putd(s, c->frameCount);
    put(s, " frames, innermost first; module+offset, see README for addr2line):\n");
    for (int i = 0; i < c->frameCount; i++) { put(s, "  #"); putd(s, i); put(s, " "); putCode(s, c->frames[i]); }
}

static void putLogTail(Sink s) {
    static char tail[LOG_TAIL_BYTES];
    if (!logPathW[0]) return;
    HANDLE f = CreateFileW(logPathW, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size, start;
    DWORD n = 0;
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0) {
        start.QuadPart = size.QuadPart > LOG_TAIL_BYTES ? size.QuadPart - LOG_TAIL_BYTES : 0;
        if (!SetFilePointerEx(f, start, NULL, FILE_BEGIN) || !ReadFile(f, tail, sizeof tail, &n, NULL)) n = 0;
    }
    CloseHandle(f);
    if (!n) return;
    char* p = tail;
    if (start.QuadPart > 0) { char* nl = memchr(tail, '\n', n); if (nl) p = nl + 1; }
    put(s, "\nLast log output ("); put(s, logPath); put(s, "):\n");
    putn(s, p, (size_t)(tail + n - p));
    if (tail[n - 1] != '\n') put(s, "\n");
}

static Sink errSink(void) {
    intptr_t h = _get_osfhandle(2);
    return h == -1 || h == -2 ? NO_SINK : (HANDLE)h;
}

/* Walks the faulting thread's stack from the exception context through the unwind tables. */
static int walkStack(const CONTEXT* from, void** frames, int max) {
    int n = 0;
#if defined(_M_X64) || defined(__x86_64__)
    CONTEXT ctx = *from;
    while (n < max && ctx.Rip) {
        frames[n++] = (void*)(uintptr_t)ctx.Rip;
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &base, NULL);
        if (!fn) { /* leaf: the return address is at the stack pointer */
            if (!ctx.Rsp) break;
            ctx.Rip = *(DWORD64*)(uintptr_t)ctx.Rsp;
            ctx.Rsp += 8;
            continue;
        }
        void* handlerData = NULL;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &handlerData, &establisher, NULL);
    }
#else
    (void)from;
    n = CaptureStackBackTrace(0, (DWORD)max, frames, NULL);
#endif
    return n;
}

static void reportWindows(const char* what, uint64_t code, const char* detail, int hasAddress, uint64_t address,
                          EXCEPTION_POINTERS* ep) {
    static void* frames[MAX_FRAMES];
    CrashInfo c = { 0 };
    c.what = what; c.code = code; c.detail = detail; c.hasAddress = hasAddress; c.address = address;
    c.frames = frames;
    if (ep && ep->ContextRecord) {
#if defined(_M_X64) || defined(__x86_64__)
        c.pc = ep->ContextRecord->Rip; c.sp = ep->ContextRecord->Rsp;
#elif defined(_M_ARM64) || defined(__aarch64__)
        c.pc = ep->ContextRecord->Pc; c.lr = ep->ContextRecord->Lr; c.sp = ep->ContextRecord->Sp;
#endif
        c.frameCount = walkStack(ep->ContextRecord, frames, MAX_FRAMES);
    } else c.frameCount = CaptureStackBackTrace(0, MAX_FRAMES, frames, NULL);
    writeReport(&c, errSink());
    if (miniDumpWriteDump && dumpPathW[0]) {
        HANDLE dump = CreateFileW(dumpPathW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (dump != INVALID_HANDLE_VALUE) {
            struct { DWORD ThreadId; EXCEPTION_POINTERS* ExceptionPointers; BOOL ClientPointers; } info = {
                GetCurrentThreadId(), ep, FALSE };
            /* MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo */
            miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, 0x00000000 | 0x00000040 | 0x00001000,
                              ep ? &info : NULL, NULL, NULL);
            CloseHandle(dump);
        }
    }
}

static const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN (fail fast)";
    default: return "exception";
    }
}

static LONG WINAPI exceptionFilter(EXCEPTION_POINTERS* ep) {
    if (inHandler) return EXCEPTION_CONTINUE_SEARCH;
    inHandler = 1;
    static char what[96];
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    const char* name = exceptionName(code);
    char* w = what;
    while (*name && w < what + 60) *w++ = *name++;
    const char* sep = " 0x"; while (*sep) *w++ = *sep++;
    for (int i = 7; i >= 0; i--) *w++ = "0123456789abcdef"[(code >> (4 * i)) & 15];
    *w = 0;
    const char* detail = NULL;
    int hasAddress = 0;
    uint64_t address = 0;
    if (ep && ep->ExceptionRecord) {
        address = (uint64_t)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;
        hasAddress = 1;
        if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
            ep->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
            detail = kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute (DEP)" : "access";
            address = ep->ExceptionRecord->ExceptionInformation[1];
        }
    }
    if (!reported) { reported = 1; reportWindows(what, code, detail, hasAddress, address, ep); }
    inHandler = 0;
    return previousFilter ? previousFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

static void abortHandler(int sig) {
    (void)sig;
    if (inHandler) return;
    inHandler = 1;
    if (!reported) { reported = 1; reportWindows("SIGABRT (abort)", SIGABRT, NULL, 0, 0, NULL); }
    inHandler = 0;
    /* Returning lets abort() terminate the process (exit code 3). */
}

#if defined(_MSC_VER) || defined(_UCRT)
static void invalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
                             unsigned int line, uintptr_t reserved) {
    (void)expression; (void)function; (void)file; (void)line; (void)reserved;
    if (inHandler) return;
    inHandler = 1;
    if (!reported) reportWindows("invalid parameter passed to a C runtime function", 0, NULL, 0, 0, NULL);
    TerminateProcess(GetCurrentProcess(), 0xC000000D);
}
#endif

static void describeSystem(void) {
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn getVersion = ntdll ? (RtlGetVersionFn)(void*)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    OSVERSIONINFOW v;
    memset(&v, 0, sizeof v);
    v.dwOSVersionInfoSize = sizeof v;
    if (getVersion && getVersion(&v) == 0)
        snprintf(osLine, sizeof osLine, "Windows %lu.%lu build %lu", (unsigned long)v.dwMajorVersion,
                 (unsigned long)v.dwMinorVersion, (unsigned long)v.dwBuildNumber);
    else snprintf(osLine, sizeof osLine, "Windows");
    HMODULE self = NULL;
    wchar_t wide[MAX_PATH];
    char name[MAX_PATH * 3];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void*)&peCrashInstall, &self) &&
        GetModuleFileNameW(self, wide, MAX_PATH) &&
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof name, NULL, NULL))
        snprintf(imageLine, sizeof imageLine, "%s loaded at %p", name, (void*)self);
}

void peCrashInstall(const char* text) {
    if (text && *text) copyText(version, sizeof version, text);
    if (installed) return;
    installed = 1;
    describeSystem();
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (dbghelp) miniDumpWriteDump = (MiniDumpWriteDumpFn)(void*)GetProcAddress(dbghelp, "MiniDumpWriteDump");
    ULONG guarantee = 64 * 1024; /* room for the filter after a stack overflow */
    SetThreadStackGuarantee(&guarantee);
    previousFilter = SetUnhandledExceptionFilter(exceptionFilter);
    signal(SIGABRT, abortHandler);
#if defined(_MSC_VER) || defined(_UCRT)
    _set_invalid_parameter_handler(invalidParameter);
#endif
}
#endif

/* ---- deliberate crashes for tests ---- */
static volatile int crashSink;
#if defined(__clang__)
__attribute__((optnone))
#elif defined(__GNUC__)
__attribute__((optimize("O0")))
#endif
static int overflowStack(volatile int depth) {
    volatile char pad[4096];
    pad[0] = (char)depth;
    pad[sizeof pad - 1] = (char)depth;
    if (crashSink < 0) return 0; /* never: keeps the recursion unbounded without a compiler warning */
    return overflowStack(depth + 1) + pad[0] + pad[sizeof pad - 1];
}

void peCrashTestTrigger(void) {
    const char* mode = getenv("PE2_TEST_CRASH");
    if (!mode || !*mode) return;
    fprintf(stderr, "[crash] PE2_TEST_CRASH=%s: crashing on purpose\n", mode);
    fflush(stderr);
    if (!strcmp(mode, "segv")) {
        volatile int* volatile nowhere = NULL;
        *nowhere = 1;
    } else if (!strcmp(mode, "abort")) abort();
    else if (!strcmp(mode, "stack")) crashSink = overflowStack(0);
    else if (!strcmp(mode, "fpe")) {
#ifdef _WIN32
        RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO, 0, 0, NULL);
#else
        raise(SIGFPE); /* integer division does not trap on arm64 */
#endif
    } else if (!strcmp(mode, "ill")) {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_trap();
#else
        abort();
#endif
    } else fprintf(stderr, "[crash] unknown PE2_TEST_CRASH mode (segv, abort, stack, fpe, ill)\n");
}
