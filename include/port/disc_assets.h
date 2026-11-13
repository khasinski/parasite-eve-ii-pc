#ifndef PORT_DISC_ASSETS_H
#define PORT_DISC_ASSETS_H
#include <stddef.h>
#include <stdint.h>

/* Synchronous, read-only access to the game's CDF containers on a user's disc image. It parses the cue sheet, the
 * ISO 9660 root directory and the STAGE0/STAGEn CDF tables itself, so it works before the game's asynchronous CD
 * queue runs and never touches it. */
typedef struct PeDisc PeDisc;

PeDisc* peDiscOpen(const char* cuePath);
void    peDiscClose(PeDisc* disc);
/* Supported USA release, verified by executable and PE_DISK marker. 0 if unknown. */
int     peDiscNumber(PeDisc* disc);
/* Why the last peDiscOpen returned NULL or the last peDiscNumber returned 0, as one sentence fragment
 * naming the file involved. Not thread safe. */
const char* peDiscError(void);

/* Reads the package chunk (LZSS room package, chunk type 0) of one CDF file and returns its decoded bytes
 * (malloc'd, caller frees). stage 0: `folder` is ignored and `file` is the full STAGE0 file id (e.g. 120300).
 * stages 1..5: `folder` is the folder id (e.g. 101) and `file` the index inside it. Returns NULL if the file or
 * chunk is not present on this disc, its extent is invalid, or allocation fails.
 * On failure, *size is zero. */
uint8_t* peDiscReadPackage(PeDisc* disc, int stage, int folder, int file, size_t* size);

/* Reads a whole file from the ISO 9660 root directory (e.g. "SLUS_010.42").
 * malloc'd; caller frees. Returns NULL with *size zero on failure. */
uint8_t* peDiscReadFile(PeDisc* disc, const char* name, size_t* size);

/* One-shot LZSS decoder for the game's package/image streams (see tools/peassets/lzss.py). */
uint8_t* peLzssDecode(const uint8_t* in, size_t inSize, size_t* outSize);
#endif
