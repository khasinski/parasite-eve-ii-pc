/* SPDX-License-Identifier: CC0-1.0
 * Route game/SDL enumeration through the public native provider ABI bridge.
 * Card contents and atomic file replacement remain owned by PE2. */
#include <dirent.h>
DIR* pe_game_opendir(const char* path);
struct dirent* pe_game_readdir(DIR* directory);
DIR* __wrap_opendir(const char* path) { return pe_game_opendir(path); }
struct dirent* __wrap_readdir(DIR* directory) { return pe_game_readdir(directory); }
