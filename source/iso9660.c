#include "iso9660.h"

#include <ctype.h>
#include <string.h>

#define ISO_BLOCK       2048
#define ISO_PVD_LBA     16
#define ISO_MAX_DIR_LBA 64  /* root directories of PS2 discs are tiny; cap it for broken images */

typedef struct {
    long sector_size;
    long data_offset;
} iso_layout;

static const iso_layout layouts[] = {
    { 2048, 0 },   /* cooked image */
    { 2352, 24 },  /* raw MODE2/XA form 1 (sync 12 + header 4 + subheader 8) */
    { 2352, 16 },  /* raw MODE1 (sync 12 + header 4) */
};

static unsigned le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static int read_block(FILE *fp, const iso_layout *l, unsigned lba, unsigned char *out)
{
    if (fseek(fp, (long)lba * l->sector_size + l->data_offset, SEEK_SET) != 0)
        return -1;
    return fread(out, 1, ISO_BLOCK, fp) == ISO_BLOCK ? 0 : -1;
}

static int is_pvd(const unsigned char *b)
{
    return b[0] == 1 && memcmp(b + 1, "CD001", 5) == 0;
}

/* Compares a directory record name ("SYSTEM.CNF;1") against a plain name ("system.cnf"). */
static int name_matches(const unsigned char *rec_name, int rec_len, const char *name)
{
    int i;

    for (i = 0; i < rec_len && rec_name[i] != ';'; i++) {
        if (name[i] == '\0' || toupper(rec_name[i]) != toupper((unsigned char)name[i]))
            return 0;
    }
    return name[i] == '\0';
}

static int find_in_dir(FILE *fp, const iso_layout *l, unsigned dir_lba, unsigned dir_size,
                       const char *name, unsigned *file_lba, unsigned *file_size)
{
    unsigned char block[ISO_BLOCK];
    unsigned blocks = (dir_size + ISO_BLOCK - 1) / ISO_BLOCK;
    unsigned b;

    if (blocks > ISO_MAX_DIR_LBA)
        blocks = ISO_MAX_DIR_LBA;

    for (b = 0; b < blocks; b++) {
        int pos = 0;

        if (read_block(fp, l, dir_lba + b, block) != 0)
            return -1;

        /* Records never cross a block boundary; a zero length means "skip to next block". */
        while (pos < ISO_BLOCK) {
            int rec_len = block[pos];
            int name_len;

            if (rec_len < 34 || pos + rec_len > ISO_BLOCK)
                break;

            name_len = block[pos + 32];
            if (33 + name_len <= rec_len && !(block[pos + 25] & 0x02) &&
                name_matches(block + pos + 33, name_len, name)) {
                *file_lba = le32(block + pos + 2);
                *file_size = le32(block + pos + 10);
                return 0;
            }
            pos += rec_len;
        }
    }
    return -1;
}

int iso_read_root_file(FILE *fp, const char *name, char *buf, size_t bufsize)
{
    unsigned char block[ISO_BLOCK];
    const iso_layout *l = NULL;
    unsigned file_lba, file_size, done = 0;
    size_t i;

    if (!fp || !buf || bufsize == 0)
        return -1;
    buf[0] = '\0';

    for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        if (read_block(fp, &layouts[i], ISO_PVD_LBA, block) == 0 && is_pvd(block)) {
            l = &layouts[i];
            break;
        }
    }
    if (!l)
        return -1;

    /* The root directory record lives at offset 156 of the primary volume descriptor. */
    if (find_in_dir(fp, l, le32(block + 156 + 2), le32(block + 156 + 10), name, &file_lba, &file_size) != 0)
        return -1;

    if (file_size > bufsize - 1)
        file_size = (unsigned)(bufsize - 1);

    while (done < file_size) {
        unsigned chunk = file_size - done;

        if (chunk > ISO_BLOCK)
            chunk = ISO_BLOCK;
        if (read_block(fp, l, file_lba + done / ISO_BLOCK, block) != 0)
            return -1;
        memcpy(buf + done, block, chunk);
        done += chunk;
    }
    buf[done] = '\0';
    return (int)done;
}
