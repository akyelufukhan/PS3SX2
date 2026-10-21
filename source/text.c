#include "text.h"

/* Windows-1254 bytes 0x80-0x9F; 0 marks unassigned ones. */
static const unsigned short cp1254_high[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0,      0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0,      0x0178,
};

unsigned cp1254_to_unicode(unsigned char c)
{
    if (c < 0x80)
        return c;
    if (c < 0xA0)
        return cp1254_high[c - 0x80];

    /* Above 0x9F it's Latin-1 except for six Turkish letters. */
    switch (c) {
    case 0xD0: return 0x011E;  /* Ğ */
    case 0xDD: return 0x0130;  /* İ */
    case 0xDE: return 0x015E;  /* Ş */
    case 0xF0: return 0x011F;  /* ğ */
    case 0xFD: return 0x0131;  /* ı */
    case 0xFE: return 0x015F;  /* ş */
    default:   return c;
    }
}

char unicode_ascii_fallback(unsigned cp)
{
    switch (cp) {
    case 0x011E: return 'G';
    case 0x0130: return 'I';
    case 0x015E: return 'S';
    case 0x011F: return 'g';
    case 0x0131: return 'i';
    case 0x015F: return 's';
    default:     return 0;
    }
}

char cp1254_ascii_fallback(unsigned char c)
{
    return unicode_ascii_fallback(cp1254_to_unicode(c));
}

static char unicode_to_cp1254(unsigned cp)
{
    unsigned c;

    if (cp < 0x80)
        return (char)cp;
    for (c = 0x80; c <= 0xFF; c++) {
        if (cp1254_to_unicode((unsigned char)c) == cp)
            return (char)c;
    }
    return '?';
}

unsigned utf8_decode(const char **str)
{
    const unsigned char *s = (const unsigned char *)*str;
    unsigned cp;
    int extra;

    if (*s == 0)
        return 0;

    if (*s < 0x80) {
        cp = *s;
        extra = 0;
    } else if ((*s & 0xE0) == 0xC0) {
        cp = *s & 0x1F;
        extra = 1;
    } else if ((*s & 0xF0) == 0xE0) {
        cp = *s & 0x0F;
        extra = 2;
    } else if ((*s & 0xF8) == 0xF0) {
        cp = *s & 0x07;
        extra = 3;
    } else {
        *str = (const char *)s + 1;
        return 0xFFFD;
    }
    s++;
    while (extra > 0 && (*s & 0xC0) == 0x80) {
        cp = (cp << 6) | (*s++ & 0x3F);
        extra--;
    }
    *str = (const char *)s;
    return extra ? 0xFFFD : cp;
}

size_t utf8_to_cp1254(const char *in, char *out, size_t outsize)
{
    size_t n = 0;
    unsigned cp;

    if (outsize == 0)
        return 0;

    while (n + 1 < outsize && (cp = utf8_decode(&in)) != 0)
        out[n++] = unicode_to_cp1254(cp);
    out[n] = '\0';
    return n;
}
