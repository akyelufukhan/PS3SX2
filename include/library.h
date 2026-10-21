#ifndef PS3SX2_LIBRARY_H
#define PS3SX2_LIBRARY_H

#include "serial.h"

#define GAME_PATH_MAX  512
#define GAME_TITLE_MAX 128

typedef enum {
    GAME_FMT_ISO,      /* plain disc image (.iso): needs Cobra CFW to mount */
    GAME_FMT_CLASSIC,  /* encrypted PS2 Classic (.BIN.ENC): works on HEN too */
} game_format;

typedef struct {
    char path[GAME_PATH_MAX];
    char title[GAME_TITLE_MAX];
    char serial[SERIAL_LEN + 1];  /* empty when unknown */
    unsigned long long size;
    game_format format;
} game_t;

typedef struct {
    game_t *games;
    int count;
    int capacity;
} library_t;

void library_init(library_t *lib);
void library_free(library_t *lib);

/*
 * Adds every .iso / .BIN.ENC file found directly inside `dir`.
 * Returns how many games were added, or -1 if the directory can't be opened.
 */
int library_scan_dir(library_t *lib, const char *dir);

/* Scans PS2ISO on the internal drive and on every USB port. Returns the total added. */
int library_scan_default(library_t *lib);

/* Sorts games by title, case-insensitively. */
void library_sort(library_t *lib);

/*
 * Turns a file name into a display title: drops the extension and any serial
 * tag ("SLUS_209.46.Burnout 3.iso" -> "Burnout 3"), and turns '_' into spaces.
 */
void library_title_from_filename(const char *filename, char *out, int outsize);

/* Returns the format for a file name, or -1 if it isn't a PS2 game image. */
int library_format_of(const char *filename);

#endif
