#ifndef PS3SX2_SERIAL_H
#define PS3SX2_SERIAL_H

/* A PS2 game serial in its display form, e.g. "SLUS-20946". */
#define SERIAL_LEN 10

/*
 * Extracts the serial from the text of a disc's SYSTEM.CNF
 * ("BOOT2 = cdrom0:\SLUS_209.46;1" -> "SLUS-20946"). Returns 0 on success.
 */
int serial_from_system_cnf(const char *cnf, char out[SERIAL_LEN + 1]);

/*
 * Finds a serial anywhere in a file name, in any of the usual spellings:
 * "SLUS_209.46.Game.iso" (OPL), "Game [SLUS-20946].iso", "SLUS20946.BIN.ENC".
 * Returns 0 on success.
 */
int serial_from_filename(const char *name, char out[SERIAL_LEN + 1]);

/*
 * If `s` starts with a serial, writes it to `out` (when not NULL) and returns
 * how many characters it spans; returns 0 otherwise.
 */
int serial_match(const char *s, char out[SERIAL_LEN + 1]);

/*
 * The disc's boot file name for a serial, "SLES-54203" -> "SLES_542.03", which is
 * how ps2_netemu CONFIG files are named. Returns 0 on success.
 */
int serial_boot_name(const char *serial, char out[12]);

/* Video region guessed from the serial prefix: "NTSC-U", "PAL", "NTSC-J", "NTSC-K", "NTSC-C" or "". */
const char *serial_region(const char *serial);

#endif
