/* SPDX-License-Identifier: MIT */
#ifndef PE2_PS5_APP_DISCOVERY_H
#define PE2_PS5_APP_DISCOVERY_H
#include <stddef.h>
/* Read-only actual active-binary comparison; 1 unique,0 absent,-1 IO,-2 ambiguity.
 * Output empty except success; no cwd/argv/environment path guesses. */
int pePs5DiscoverApplication(char *out,size_t capacity);
const char *pePs5ApplicationDiscoveryReason(void);
#endif
