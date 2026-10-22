#include "coverdl.h"
#include "covers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ERRORS_IN_A_ROW 3

int coverdl_template_ok(const char *tmpl)
{
    const char *p;
    int specs = 0;

    if (!tmpl || (strncmp(tmpl, "https://", 8) != 0 && strncmp(tmpl, "http://", 7) != 0))
        return 0;
    for (p = tmpl; *p; p++) {
        if (*p != '%')
            continue;
        if (p[1] != 's')
            return 0;
        specs++;
        p++;
    }
    return specs == 1;
}

int coverdl_url(const char *tmpl, const char *serial, char *out, size_t size)
{
    const char *spec;
    size_t head, tail, len;

    if (!coverdl_template_ok(tmpl) || !serial || !serial[0])
        return -1;

    /* Spliced by hand rather than handing a user-editable template to printf. */
    spec = strstr(tmpl, "%s");
    head = (size_t)(spec - tmpl);
    tail = strlen(spec + 2);
    len = strlen(serial);
    if (head + len + tail + 1 > size)
        return -1;
    memcpy(out, tmpl, head);
    memcpy(out + head, serial, len);
    memcpy(out + head + len, spec + 2, tail + 1);
    return 0;
}

int coverdl_target(const game_t *g, char *out, size_t size)
{
    const char *slash = strrchr(g->path, '/');

    if (!slash || !g->serial[0])
        return -1;
    return snprintf(out, size, "%.*s/covers/%s.jpg", (int)(slash - g->path), g->path, g->serial) < (int)size ? 0 : -1;
}

int coverdl_needs_cover(const game_t *g)
{
    char paths[COVER_MAX_CANDIDATES][GAME_PATH_MAX];
    struct stat st;
    int i, n;

    if (!g->serial[0])
        return 0;
    n = covers_candidates(g, paths, COVER_MAX_CANDIDATES);
    for (i = 0; i < n; i++) {
        if (stat(paths[i], &st) == 0)
            return 0;
    }
    return 1;
}

int coverdl_is_jpeg(const unsigned char *data, size_t len)
{
    return len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

static int file_is_jpeg(const char *path)
{
    unsigned char head[3];
    FILE *fp = fopen(path, "rb");
    size_t n;

    if (!fp)
        return 0;
    n = fread(head, 1, sizeof(head), fp);
    fclose(fp);
    return coverdl_is_jpeg(head, n);
}

static int serial_seen(const library_t *lib, const int *todo, int count, const char *serial)
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(lib->games[todo[i]].serial, serial) == 0)
            return 1;
    }
    return 0;
}

void coverdl_run(const library_t *lib, const char *tmpl, coverdl_fetch_fn fetch,
                 coverdl_progress_fn progress, coverdl_saved_fn saved, void *user, coverdl_result *out)
{
    char url[GAME_PATH_MAX], target[GAME_PATH_MAX], dir[GAME_PATH_MAX], part[GAME_PATH_MAX + 8];
    int *todo, i, errors = 0;

    memset(out, 0, sizeof(*out));
    if (lib->count == 0 || !(todo = malloc(lib->count * sizeof(int))))
        return;

    for (i = 0; i < lib->count; i++) {
        const game_t *g = &lib->games[i];

        if (coverdl_needs_cover(g) && !serial_seen(lib, todo, out->total, g->serial))
            todo[out->total++] = i;
    }

    for (i = 0; i < out->total; i++) {
        const game_t *g = &lib->games[todo[i]];
        char *slash;
        int r;

        if (progress && progress(i, out->total, g, user)) {
            out->cancelled = 1;
            break;
        }
        if (coverdl_url(tmpl, g->serial, url, sizeof(url)) != 0 ||
            coverdl_target(g, target, sizeof(target)) != 0) {
            out->failed++;
            continue;
        }

        memcpy(dir, target, sizeof(dir));
        if ((slash = strrchr(dir, '/')) != NULL)
            *slash = '\0';
        mkdir(dir, 0777);

        /* Download under a temporary name so a broken transfer never shows up as a cover. */
        snprintf(part, sizeof(part), "%s.part", target);
        r = fetch(url, part, user);
        if (r == 0 && file_is_jpeg(part) && rename(part, target) == 0) {
            out->downloaded++;
            errors = 0;
            if (saved)
                saved(g, user);
            continue;
        }
        unlink(part);
        if (r == 1) {
            out->missing++;
            errors = 0;
        } else {
            out->failed++;
            if (++errors >= MAX_ERRORS_IN_A_ROW)
                break;
        }
    }
    free(todo);
}
