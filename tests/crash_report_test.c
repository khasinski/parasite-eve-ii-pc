/* Crash reporter (src/port/crash_report.c): children crash on purpose through PE2_TEST_CRASH, the same entry the game
 * uses, and the report they leave must name the version, the crash, the watched game state, a backtrace and the
 * tail of the log; the next start must see it once. A handler installed before the reporter must still run. */
#include "port/crash_report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
int main(void) { puts("crash_report_test: POSIX only (fork); skipped"); return 0; }
#else
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static unsigned char watchedStage = 3;
static int watchedTick = -5;
static void* watchedCallback;
static char folder[512];

static void markedCallback(void) {}

static void previousHandler(int sig) {
    (void)sig;
    char path[600];
    snprintf(path, sizeof path, "%s/previous-handler", folder);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
    _exit(42);
}

static char* readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = malloc((size_t)n + 1);
    CHECK(text && fread(text, 1, (size_t)n, f) == (size_t)n);
    text[n] = 0;
    fclose(f);
    return text;
}

/* Runs one crashing child and returns its wait status. */
static int crashChild(const char* mode, int chain) {
    char log[600], err[600], stamp[64];
    snprintf(log, sizeof log, "%s/parasite-eve-ii.log", folder);
    snprintf(err, sizeof err, "%s/stderr-%s.txt", folder, mode);
    snprintf(stamp, sizeof stamp, "test-%s", mode);
    fflush(NULL);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        int fd = open(err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(90);
        dup2(fd, STDERR_FILENO);
        if (chain) signal(SIGSEGV, previousHandler);
        /* A log larger than the tail the report keeps: its first line must not appear, its last one must. */
        FILE* f = fopen(log, "w");
        if (!f) _exit(91);
        fprintf(f, "first-log-line\n");
        for (int i = 0; i < 2000; i++) fprintf(f, "[test] filler line %d\n", i);
        fprintf(f, "last-log-line-marker\n");
        fclose(f);
        watchedCallback = (void*)&markedCallback;
        peCrashInstall("test-version 1.2.3");
        peCrashWatch("stage", &watchedStage, 1, PE_CRASH_WATCH_UNSIGNED);
        peCrashWatch("pad tick", &watchedTick, sizeof watchedTick, PE_CRASH_WATCH_SIGNED);
        peCrashWatch("last task callback", &watchedCallback, sizeof watchedCallback, PE_CRASH_WATCH_CODE);
        peCrashSetOutput(folder, log, stamp);
        setenv("PE2_TEST_CRASH", mode, 1);
        peCrashTestTrigger();
        _exit(92); /* not reached */
    }
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(!(WIFEXITED(status) && WEXITSTATUS(status) >= 90 && WEXITSTATUS(status) <= 92));
    CHECK(!(WIFEXITED(status) && WEXITSTATUS(status) == 0));
    return status;
}

static void checkReport(const char* mode, const char* crash) {
    char path[600], err[600], previous[4096];
    snprintf(path, sizeof path, "%s/parasite-eve-ii-crash-test-%s.log", folder, mode);
    snprintf(err, sizeof err, "%s/stderr-%s.txt", folder, mode);
    char* report = readFile(path);
    if (!report) { fprintf(stderr, "%s: no crash report at %s\n", mode, path); exit(1); }
    CHECK(strstr(report, "Parasite Eve II PC port crash report\n") == report);
    CHECK(strstr(report, "Version: test-version 1.2.3\n"));
    CHECK(strstr(report, "System: "));
    CHECK(strstr(report, "Image: "));
    CHECK(strstr(report, "Crash: signal "));
    if (!strstr(report, crash)) { fprintf(stderr, "%s: expected %s in\n%s", mode, crash, report); exit(1); }
    CHECK(strstr(report, "\nGame state:\n"));
    CHECK(strstr(report, "  stage: 3\n"));
    CHECK(strstr(report, "  pad tick: -5\n"));
    CHECK(strstr(report, "  last task callback: 0x"));
    CHECK(strstr(report, "\nBacktrace ("));
    CHECK(strstr(report, "crash_report_test")); /* frames name the image */
    CHECK(strstr(report, "\nLast log output ("));
    CHECK(strstr(report, "last-log-line-marker\n"));
    CHECK(!strstr(report, "first-log-line"));
    if (!strcmp(mode, "segv")) CHECK(strstr(report, "Fault address: 0x0\n"));
    free(report);
    /* The normal log / terminal gets a marker line naming the report, then the report itself. */
    char* text = readFile(err);
    CHECK(text);
    CHECK(strstr(text, "*** CRASH *** signal "));
    CHECK(strstr(text, "crash report written to "));
    CHECK(strstr(text, path));
    CHECK(strstr(text, "*** END OF CRASH REPORT ***"));
    free(text);
    /* The next start names it once. */
    CHECK(peCrashTakePreviousReport(folder, previous, sizeof previous) == 1);
    CHECK(!strcmp(previous, path));
    CHECK(peCrashTakePreviousReport(folder, previous, sizeof previous) == 0);
}

static void removeFolder(void) {
    DIR* d = opendir(folder);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[1200];
        snprintf(path, sizeof path, "%s/%s", folder, e->d_name);
        unlink(path);
    }
    closedir(d);
    rmdir(folder);
}

int main(void) {
    const char* tmp = getenv("TMPDIR");
    snprintf(folder, sizeof folder, "%s/pe2-crash-test-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    CHECK(mkdtemp(folder));

    /* No report yet. */
    char previous[4096];
    CHECK(peCrashTakePreviousReport(folder, previous, sizeof previous) == 0);

    /* Invalid write; the handler installed before the reporter runs afterwards (exit 42). */
    int status = crashChild("segv", 1);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 42);
    {
        char path[600];
        snprintf(path, sizeof path, "%s/previous-handler", folder);
        CHECK(access(path, F_OK) == 0);
    }
    checkReport("segv", "SIGSEGV");

    /* abort(): the default action follows, so the process still dies of SIGABRT. */
    status = crashChild("abort", 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    checkReport("abort", "SIGABRT");

    /* Stack overflow: only the alternate signal stack leaves room for the report. A sanitizer's handler may end the
     * process with an exit status instead of the signal. */
    crashChild("stack", 0);
    {
        char path[600];
        snprintf(path, sizeof path, "%s/parasite-eve-ii-crash-test-stack.log", folder);
        char* report = readFile(path);
        CHECK(report);
        CHECK(strstr(report, "SIGSEGV") || strstr(report, "SIGBUS"));
        free(report);
    }
    checkReport("stack", "signal");

    /* Trap instruction: SIGILL on x86-64, SIGTRAP on arm64. */
    crashChild("ill", 0);
    checkReport("ill", "signal");

    removeFolder();
    puts("crash_report_test: ok");
    return 0;
}
#endif
