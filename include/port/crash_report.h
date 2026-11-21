#ifndef PE2_CRASH_REPORT_H
#define PE2_CRASH_REPORT_H
/* Crash reporter (src/port/crash_report.c).
 *
 * peCrashInstall installs the fatal-signal handlers (POSIX: SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGABRT on an
 * alternate signal stack; Windows: the unhandled-exception filter, abort and the CRT invalid-parameter handler).
 * Until peCrashSetOutput names a folder the report only goes to standard error. Afterwards each crash also writes
 * <folder>/parasite-eve-ii-crash-<session>.log (and, on Windows, a .dmp minidump next to it) and records its name in
 * <folder>/parasite-eve-ii-crash.last, which the next start reads once through peCrashTakePreviousReport.
 *
 * The handler only uses storage prepared here and async-signal-safe calls. Handlers that were installed before
 * (a sanitizer's, for example) are kept: once the report is written they run as if ours were not there. */

/* What a watched value is: an integer of 1, 2, 4 or 8 bytes, or a code address that is symbolised like a frame. */
enum { PE_CRASH_WATCH_UNSIGNED, PE_CRASH_WATCH_SIGNED, PE_CRASH_WATCH_CODE };

void peCrashInstall(const char* version);
/* folder: where reports go (NULL: standard error only). logPath: the log file whose tail the report appends (NULL:
 * none). session: the file-name stamp, normally the start time as YYYYMMDD-HHMMSS (NULL: computed now). */
void peCrashSetOutput(const char* folder, const char* logPath, const char* session);
/* Adds a value read and printed by the report; label and address must stay valid. Up to 32; returns 0 when full. */
int peCrashWatch(const char* label, const void* address, int bytes, int kind);
/* The report a crashed earlier run left in folder, once: copies its path to out and forgets it. Returns 1 if any. */
int peCrashTakePreviousReport(const char* folder, char* out, unsigned outSize);
/* Development and tests: PE2_TEST_CRASH=segv|abort|stack|fpe|ill crashes here on purpose; anything else is ignored. */
void peCrashTestTrigger(void);
/* The file name of the report this run would write (empty before peCrashSetOutput). */
const char* peCrashReportPath(void);
#endif
