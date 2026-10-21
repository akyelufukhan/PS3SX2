#ifndef PS3SX2_COVERS_H
#define PS3SX2_COVERS_H

#include "library.h"

#define COVER_MAX_CANDIDATES 4

/*
 * Fills `out` with the places a game's cover may live, best match first:
 *   <PS2ISO>/covers/<SERIAL>.png and .jpg  (shared folder, named by serial)
 *   <PS2ISO>/<file name>.png and .jpg      (next to the game, same name)
 * Returns how many paths were written.
 */
int covers_candidates(const game_t *g, char out[][GAME_PATH_MAX], int max);

#endif
