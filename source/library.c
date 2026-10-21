#include "library.h"
#include "iso9660.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *const scan_roots[] = {
    "/dev_hdd0/PS2ISO",
    "/dev_usb000/PS2ISO", "/dev_usb001/PS2ISO", "/dev_usb002/PS2ISO", "/dev_usb003/PS2ISO",
    "/dev_usb004/PS2ISO", "/dev_usb005/PS2ISO", "/dev_usb006/PS2ISO", "/dev_usb007/PS2ISO",
};

static int ends_with_ci(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

int library_format_of(const char *filename)
{
    /* Skips hidden files, including the "._name" metadata macOS leaves on FAT drives. */
    if (filename[0] == '.')
        return -1;
    if (ends_with_ci(filename, ".iso"))
        return GAME_FMT_ISO;
    if (ends_with_ci(filename, ".bin.enc"))
        return GAME_FMT_CLASSIC;
    return -1;
}

void library_init(library_t *lib)
{
    memset(lib, 0, sizeof(*lib));
}

void library_free(library_t *lib)
{
    free(lib->games);
    library_init(lib);
}

static game_t *add_game(library_t *lib)
{
    game_t *g;

    if (lib->count == lib->capacity) {
        int cap = lib->capacity ? lib->capacity * 2 : 32;
        game_t *grown = realloc(lib->games, cap * sizeof(game_t));

        if (!grown)
            return NULL;
        lib->games = grown;
        lib->capacity = cap;
    }
    g = &lib->games[lib->count++];
    memset(g, 0, sizeof(*g));
    return g;
}

static void read_iso_serial(const char *path, char serial[SERIAL_LEN + 1])
{
    char cnf[1024];
    FILE *fp = fopen(path, "rb");

    if (!fp)
        return;
    if (iso_read_root_file(fp, "SYSTEM.CNF", cnf, sizeof(cnf)) > 0)
        serial_from_system_cnf(cnf, serial);
    fclose(fp);
}

void library_title_from_filename(const char *filename, char *out, int outsize)
{
    char buf[GAME_PATH_MAX];
    int i, n, len, w;

    snprintf(buf, sizeof(buf), "%s", filename);
    len = (int)strlen(buf);
    if (ends_with_ci(buf, ".bin.enc"))
        buf[len - 8] = '\0';
    else if (ends_with_ci(buf, ".iso"))
        buf[len - 4] = '\0';

    /* OPL-style "SLUS_209.46.Title" prefix. */
    n = serial_match(buf, NULL);
    if (n && (buf[n] == '\0' || strchr(". _-", buf[n])))
        memmove(buf, buf + n + (buf[n] ? 1 : 0), strlen(buf + n + (buf[n] ? 1 : 0)) + 1);

    /* "[SLUS-20946]" or "(SLUS-20946)" anywhere. */
    for (i = 0; buf[i]; i++) {
        if (buf[i] != '[' && buf[i] != '(')
            continue;
        n = serial_match(buf + i + 1, NULL);
        if (n && (buf[i + 1 + n] == ']' || buf[i + 1 + n] == ')')) {
            memmove(buf + i, buf + i + n + 2, strlen(buf + i + n + 2) + 1);
            i--;
        }
    }

    /* '_' to space, collapse repeated spaces, trim. */
    for (i = 0, w = 0; buf[i]; i++) {
        char c = buf[i] == '_' ? ' ' : buf[i];

        if (c == ' ' && (w == 0 || buf[w - 1] == ' '))
            continue;
        buf[w++] = c;
    }
    while (w > 0 && buf[w - 1] == ' ')
        w--;
    buf[w] = '\0';

    snprintf(out, outsize, "%s", buf);
}

int library_scan_dir(library_t *lib, const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int added = 0;

    if (!d)
        return -1;

    while ((e = readdir(d)) != NULL) {
        char path[GAME_PATH_MAX];
        struct stat st;
        int fmt = library_format_of(e->d_name);
        game_t *g;

        if (fmt < 0)
            continue;
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path))
            continue;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        if (!(g = add_game(lib)))
            break;

        strcpy(g->path, path);
        g->size = (unsigned long long)st.st_size;
        g->format = (game_format)fmt;

        /* .BIN.ENC is encrypted, so only plain ISOs can tell us their serial. */
        if (fmt == GAME_FMT_ISO)
            read_iso_serial(path, g->serial);
        if (!g->serial[0])
            serial_from_filename(e->d_name, g->serial);

        library_title_from_filename(e->d_name, g->title, sizeof(g->title));
        if (!g->title[0])
            snprintf(g->title, sizeof(g->title), "%s", g->serial[0] ? g->serial : e->d_name);
        added++;
    }
    closedir(d);
    return added;
}

int library_scan_default(library_t *lib)
{
    int total = 0;
    size_t i;

    for (i = 0; i < sizeof(scan_roots) / sizeof(scan_roots[0]); i++) {
        int n = library_scan_dir(lib, scan_roots[i]);

        if (n > 0)
            total += n;
    }
    return total;
}

static int by_title(const void *a, const void *b)
{
    const game_t *ga = a, *gb = b;
    int r = strcasecmp(ga->title, gb->title);

    return r ? r : strcmp(ga->path, gb->path);
}

void library_sort(library_t *lib)
{
    if (lib->count > 1)
        qsort(lib->games, lib->count, sizeof(game_t), by_title);
}
