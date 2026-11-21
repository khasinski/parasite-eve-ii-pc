#ifndef PORT_PAD_TRACE_H
#define PORT_PAD_TRACE_H
#include <stdint.h>
#include <stdio.h>
/* One port-zero VBlank sample per line: decimal index, then raw pad bytes in
 * hex. Status, controller ID, active-low buttons and analog axes are retained.
 * This is a native interchange format, not a DuckStation recording format. */
int pePadTraceHeader(FILE* file, int writing);
int pePadTraceWrite(FILE* file, uint64_t index, const char* pad, size_t length);
/* 1 = complete sample, 0 = clean EOF, -1 = invalid/truncated sample or I/O error.
 * Failure leaves the destination unchanged. */
int pePadTraceRead(FILE* file, uint64_t index, char* pad, size_t length);
/* Development stdin command: advance COUNT MASK, or quit. Outputs change
 * only for a valid advance. Returns 1=advance, 0=quit, -1=invalid. */
int pePadControlParse(const char* line, uint32_t* count, uint16_t* mask);
/* PE2_PAD_REPLAY/PE2_PAD_RECORD runs default PsyZ to emulated time
 * (PSYZ_AUDIO_CLOCK=video, PSYZ_RASTER_CLOCK=virtual) so that wall time,
 * the audio device and host scheduling cannot change what a trace does.
 * An explicitly set variable (e.g. PSYZ_AUDIO_CLOCK=host) is kept. Call it
 * before PsyZ or SDL start. Returns a PE_PAD_CLOCK_* mask of the variables set. */
#define PE_PAD_CLOCK_AUDIO 1
#define PE_PAD_CLOCK_RASTER 2
int pePadTraceDefaultClocks(void);
#endif
