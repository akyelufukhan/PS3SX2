#include "serial.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

int serial_match(const char *s, char out[SERIAL_LEN + 1])
{
    char digits[5];
    int i, n = 0, p = 0;

    for (i = 0; i < 4; i++) {
        if (!isalpha((unsigned char)s[p + i]))
            return 0;
    }
    p = 4;
    if (s[p] == '_' || s[p] == '-')
        p++;

    /* Five digits, optionally split as "209.46". */
    while (n < 5) {
        if (n == 3 && s[p] == '.')
            p++;
        if (!isdigit((unsigned char)s[p]))
            return 0;
        digits[n++] = s[p++];
    }
    if (isdigit((unsigned char)s[p]))
        return 0;

    if (out) {
        for (i = 0; i < 4; i++)
            out[i] = (char)toupper((unsigned char)s[i]);
        out[4] = '-';
        memcpy(out + 5, digits, 5);
        out[SERIAL_LEN] = '\0';
    }
    return p;
}

int serial_from_system_cnf(const char *cnf, char out[SERIAL_LEN + 1])
{
    const char *line = cnf;

    while (line && *line) {
        const char *p = line;
        const char *next = strchr(line, '\n');

        while (*p == ' ' || *p == '\t')
            p++;
        /* BOOT2 is the PS2 boot line; "BOOT" alone would be a PS1 disc. */
        if (strncasecmp(p, "BOOT2", 5) == 0) {
            const char *end = next ? next : p + strlen(p);
            const char *name = p + 5;
            const char *q;

            for (q = p + 5; q < end && *q != ';'; q++) {
                if (*q == '\\' || *q == '/' || *q == ':' || *q == '=' || *q == ' ')
                    name = q + 1;
            }
            return serial_match(name, out) ? 0 : -1;
        }
        line = next ? next + 1 : NULL;
    }
    return -1;
}

int serial_from_filename(const char *name, char out[SERIAL_LEN + 1])
{
    const char *p;

    for (p = name; *p; p++) {
        /* Only start at a word boundary so "MYGAME12345" doesn't match. */
        if (p > name && isalnum((unsigned char)p[-1]))
            continue;
        if (serial_match(p, out))
            return 0;
    }
    return -1;
}

int serial_boot_name(const char *serial, char out[12])
{
    char norm[SERIAL_LEN + 1];

    if (!serial || !serial_match(serial, norm))
        return -1;
    /* "SLES-54203" -> "SLES_542.03" */
    memcpy(out, norm, 4);
    out[4] = '_';
    memcpy(out + 5, norm + 5, 3);
    out[8] = '.';
    memcpy(out + 9, norm + 8, 2);
    out[11] = '\0';
    return 0;
}

const char *serial_region(const char *serial)
{
    if (!serial || strlen(serial) < 4)
        return "";

    switch (serial[2]) {
    case 'U': return "NTSC-U";  /* SLUS, SCUS */
    case 'E': return "PAL";     /* SLES, SCES, SCED */
    case 'P':                   /* SLPS, SLPM, SCPS, PBPX */
    case 'A': return "NTSC-J";  /* SCAJ, SLAJ (Asia) */
    case 'K': return "NTSC-K";  /* SLKA, SCKA */
    case 'C': return "NTSC-C";  /* SCCS */
    default:  return "";
    }
}
