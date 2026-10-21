#ifndef PS3SX2_ISO9660_H
#define PS3SX2_ISO9660_H

#include <stddef.h>
#include <stdio.h>

/*
 * Minimal ISO9660 reader, just enough to pull SYSTEM.CNF out of a PS2 disc image.
 *
 * Understands cooked 2048-byte images (DVD games, most .iso dumps) and raw
 * 2352-byte CD dumps in MODE1 or MODE2/XA form 1 (many PS2 CD games).
 */

/*
 * Reads the file `name` (e.g. "SYSTEM.CNF") from the image's root directory.
 * The name is matched case-insensitively and the ";1" version suffix is ignored.
 * At most bufsize - 1 bytes are copied and the buffer is always NUL-terminated.
 * Returns the number of bytes copied, or -1 if the image or file can't be read.
 */
int iso_read_root_file(FILE *fp, const char *name, char *buf, size_t bufsize);

#endif
