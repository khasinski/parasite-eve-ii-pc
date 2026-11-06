#ifndef PE2_PS5_PATHS_H
#define PE2_PS5_PATHS_H
/* PS5 build only. Process-owned strings; explicit options take precedence. */
/* Post-CRT write policy: allowed existing /mnt/usb0..7 only. Explicit overrides never bypass checks; no /data/cwd fallback. */
int pePs5WritePathAllowed(const char* path);
const char* pePs5StorageReason(void);
const char* pePs5ConfigDirectory(void);
const char* pePs5CardDirectory(void);
int pePs5DiscDiscoveryError(void);
const char* pePs5DiscDiscoveryReason(void);
const char* pePs5FindDisc(int number, const char* companion);
#endif
