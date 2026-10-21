#ifndef PS3SX2_TEXT_H
#define PS3SX2_TEXT_H

#include <stddef.h>

/*
 * Tiny3D's libfont draws 8-bit character codes. We bake the font as
 * Windows-1254 (Latin-1 plus Turkish ğ ş ı İ Ğ Ş), so UTF-8 strings - UI text
 * and game titles alike - are converted before they're drawn.
 */

/*
 * Decodes one UTF-8 character and advances *s past it. Invalid or cut-off
 * sequences give U+FFFD. Returns 0 at the end of the string.
 */
unsigned utf8_decode(const char **s);

/* Converts UTF-8 to Windows-1254; unmappable characters become '?'. Always NUL-terminates. */
size_t utf8_to_cp1254(const char *in, char *out, size_t outsize);

/* Unicode code point for a Windows-1254 byte, or 0 if the byte is unassigned. */
unsigned cp1254_to_unicode(unsigned char c);

/* ASCII stand-in for the Turkish-only letters (ğ -> g), for fonts that lack them; 0 otherwise. */
char unicode_ascii_fallback(unsigned cp);
char cp1254_ascii_fallback(unsigned char c);

#endif
