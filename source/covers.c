#include "covers.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *const exts[] = { "png", "jpg" };

int covers_candidates(const game_t *g, char out[][GAME_PATH_MAX], int max)
{
    const char *slash = strrchr(g->path, '/');
    int dir_len = slash ? (int)(slash - g->path) : 0;
    int base_len = (int)strlen(g->path);
    int n = 0;
    size_t i;

    if (base_len > 8 && strcasecmp(g->path + base_len - 8, ".BIN.ENC") == 0)
        base_len -= 8;
    else if (base_len > 4 && strcasecmp(g->path + base_len - 4, ".iso") == 0)
        base_len -= 4;

    if (g->serial[0]) {
        for (i = 0; i < 2 && n < max; i++)
            snprintf(out[n++], GAME_PATH_MAX, "%.*s/covers/%s.%s", dir_len, g->path, g->serial, exts[i]);
    }
    for (i = 0; i < 2 && n < max; i++)
        snprintf(out[n++], GAME_PATH_MAX, "%.*s.%s", base_len, g->path, exts[i]);
    return n;
}
