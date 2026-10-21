/*
 * Host-side unit tests for the platform-independent parts of PS3SX2.
 * Build and run with `make -C tests`; no PS3 toolchain needed.
 */
#include "coverflow.h"
#include "covers.h"
#include "iso9660.h"
#include "lang.h"
#include "launcher.h"
#include "library.h"
#include "serial.h"
#include "text.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    const char *a_ = (actual), *e_ = (expected); \
    checks++; \
    if (strcmp(a_, e_) != 0) { failures++; fprintf(stderr, "%s:%d: got \"%s\", expected \"%s\"\n", __FILE__, __LINE__, a_, e_); } \
} while (0)

static char tmp_root[512];

/* ------------------------------------------------------------------ helpers */

static void write_file(const char *path, const void *data, size_t size)
{
    FILE *fp = fopen(path, "wb");

    if (!fp || fwrite(data, 1, size, fp) != size) {
        perror(path);
        exit(2);
    }
    fclose(fp);
}

static char *read_file(const char *path, size_t *size)
{
    FILE *fp = fopen(path, "rb");
    char *buf;
    long n;

    if (!fp)
        return NULL;
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = malloc(n + 1);
    if (fread(buf, 1, n, fp) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(fp);
    *size = (size_t)n;
    return buf;
}

static int file_equals(const char *path, const void *data, size_t size)
{
    size_t n;
    char *buf = read_file(path, &n);
    int same = buf && n == size && memcmp(buf, data, size) == 0;

    free(buf);
    return same;
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void remove_tree(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;

    if (!d) {
        unlink(path);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char child[1024];

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        remove_tree(child);
    }
    closedir(d);
    rmdir(path);
}

static void path_join(char *out, size_t size, const char *a, const char *b)
{
    snprintf(out, size, "%s/%s", a, b);
}

static void fill_pattern(unsigned char *buf, size_t size, unsigned seed)
{
    size_t i;

    for (i = 0; i < size; i++) {
        seed = seed * 1103515245u + 12345u;
        buf[i] = (unsigned char)(seed >> 16);
    }
}

/* -------------------------------------------------------------- ISO builder */

typedef struct {
    const char *name;  /* "SYSTEM.CNF;1" */
    const void *data;
    unsigned size;
    int is_dir;
} iso_entry;

static void put_le32(unsigned char *p, unsigned v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = v >> 24;
}

static void put_be32(unsigned char *p, unsigned v)
{
    p[0] = v >> 24; p[1] = (v >> 16) & 0xff; p[2] = (v >> 8) & 0xff; p[3] = v & 0xff;
}

static int put_dir_record(unsigned char *p, const char *name, int name_len, unsigned lba, unsigned size, int is_dir)
{
    int len = 33 + name_len + ((33 + name_len) & 1);

    memset(p, 0, len);
    p[0] = (unsigned char)len;
    put_le32(p + 2, lba);  put_be32(p + 6, lba);
    put_le32(p + 10, size); put_be32(p + 14, size);
    p[25] = is_dir ? 0x02 : 0x00;
    p[32] = (unsigned char)name_len;
    memcpy(p + 33, name, name_len);
    return len;
}

/* Writes an ISO9660 image with the given root entries, using any sector layout. */
static void make_iso(const char *path, long sector_size, long data_offset, const iso_entry *entries, int count)
{
    enum { PVD = 16, TERM = 17, ROOT = 18, DATA = 19 };
    unsigned char block[2048];
    unsigned lbas[16];
    unsigned next = DATA, total;
    unsigned char *img;
    int i, pos;

    for (i = 0; i < count; i++) {
        lbas[i] = next;
        next += (entries[i].size + 2047) / 2048 + (entries[i].size == 0);
    }
    total = next;
    img = calloc(total, sector_size);

#define PUT_BLOCK(lba) memcpy(img + (long)(lba) * sector_size + data_offset, block, 2048)

    memset(block, 0, sizeof(block));
    block[0] = 1;
    memcpy(block + 1, "CD001", 5);
    block[6] = 1;
    put_dir_record(block + 156, "\0", 1, ROOT, 2048, 1);
    PUT_BLOCK(PVD);

    memset(block, 0, sizeof(block));
    block[0] = 255;
    memcpy(block + 1, "CD001", 5);
    PUT_BLOCK(TERM);

    memset(block, 0, sizeof(block));
    pos = put_dir_record(block, "\0", 1, ROOT, 2048, 1);
    pos += put_dir_record(block + pos, "\1", 1, ROOT, 2048, 1);
    for (i = 0; i < count; i++)
        pos += put_dir_record(block + pos, entries[i].name, (int)strlen(entries[i].name), lbas[i], entries[i].size, entries[i].is_dir);
    PUT_BLOCK(ROOT);

    for (i = 0; i < count; i++) {
        unsigned off;

        for (off = 0; off < entries[i].size; off += 2048) {
            unsigned n = entries[i].size - off < 2048 ? entries[i].size - off : 2048;

            memset(block, 0, sizeof(block));
            memcpy(block, (const char *)entries[i].data + off, n);
            PUT_BLOCK(lbas[i] + off / 2048);
        }
    }
#undef PUT_BLOCK

    write_file(path, img, (size_t)total * sector_size);
    free(img);
}

static const char gta_cnf[] = "BOOT2 = cdrom0:\\SLUS_209.46;1\r\nVER = 1.03\r\nVMODE = NTSC\r\n";

/* -------------------------------------------------------------------- tests */

static void test_iso_layouts(void)
{
    static const long layouts[][2] = { { 2048, 0 }, { 2352, 24 }, { 2352, 16 } };
    unsigned char big[5000];
    char path[600], buf[8192], serial[SERIAL_LEN + 1];
    size_t i;

    fill_pattern(big, sizeof(big), 7);

    for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        iso_entry entries[] = {
            { "DATA", "", 0, 1 },
            { "SLUS_209.46;1", "ELF", 3, 0 },
            { "BIG.DAT;1", big, sizeof(big), 0 },
            { "SYSTEM.CNF;1", gta_cnf, sizeof(gta_cnf) - 1, 0 },
        };
        FILE *fp;
        int n;

        snprintf(path, sizeof(path), "%s/layout%zu.iso", tmp_root, i);
        make_iso(path, layouts[i][0], layouts[i][1], entries, 4);
        fp = fopen(path, "rb");

        n = iso_read_root_file(fp, "SYSTEM.CNF", buf, sizeof(buf));
        CHECK(n == (int)sizeof(gta_cnf) - 1);
        CHECK_STR(buf, gta_cnf);
        CHECK(serial_from_system_cnf(buf, serial) == 0);
        CHECK_STR(serial, "SLUS-20946");

        /* Case-insensitive name, data spanning several blocks. */
        n = iso_read_root_file(fp, "big.dat", buf, sizeof(buf));
        CHECK(n == (int)sizeof(big));
        CHECK(memcmp(buf, big, sizeof(big)) == 0);

        /* Truncation keeps the result NUL-terminated. */
        n = iso_read_root_file(fp, "SYSTEM.CNF", buf, 6);
        CHECK(n == 5);
        CHECK_STR(buf, "BOOT2");

        CHECK(iso_read_root_file(fp, "MISSING.TXT", buf, sizeof(buf)) == -1);
        CHECK(iso_read_root_file(fp, "DATA", buf, sizeof(buf)) == -1);  /* directories don't count */
        fclose(fp);
    }
}

static void test_iso_rejects_garbage(void)
{
    unsigned char junk[64 * 1024];
    char path[600], buf[256];
    FILE *fp;

    fill_pattern(junk, sizeof(junk), 3);
    snprintf(path, sizeof(path), "%s/junk.iso", tmp_root);
    write_file(path, junk, sizeof(junk));
    fp = fopen(path, "rb");
    CHECK(iso_read_root_file(fp, "SYSTEM.CNF", buf, sizeof(buf)) == -1);
    CHECK(buf[0] == '\0');
    fclose(fp);

    /* Shorter than the volume descriptor. */
    write_file(path, junk, 100);
    fp = fopen(path, "rb");
    CHECK(iso_read_root_file(fp, "SYSTEM.CNF", buf, sizeof(buf)) == -1);
    fclose(fp);
}

static void test_serial_from_system_cnf(void)
{
    char s[SERIAL_LEN + 1];

    CHECK(serial_from_system_cnf(gta_cnf, s) == 0);
    CHECK_STR(s, "SLUS-20946");

    CHECK(serial_from_system_cnf("  boot2=cdrom0:\\slus_210.59;1\n", s) == 0);
    CHECK_STR(s, "SLUS-21059");

    CHECK(serial_from_system_cnf("VER = 1.00\nBOOT2 = cdrom0:\\DATA\\SCES_531.33;1\n", s) == 0);
    CHECK_STR(s, "SCES-53133");

    /* PS1 discs use BOOT, not BOOT2. */
    CHECK(serial_from_system_cnf("BOOT = cdrom:\\SCUS_944.55;1\r\n", s) == -1);
    CHECK(serial_from_system_cnf("BOOT2 = cdrom0:\\MAIN.ELF;1\n", s) == -1);
    CHECK(serial_from_system_cnf("", s) == -1);
}

static void test_serial_from_filename(void)
{
    char s[SERIAL_LEN + 1];

    CHECK(serial_from_filename("SLUS_209.46.Grand Theft Auto.iso", s) == 0);
    CHECK_STR(s, "SLUS-20946");
    CHECK(serial_from_filename("Tekken 5 [SLUS-21059].iso", s) == 0);
    CHECK_STR(s, "SLUS-21059");
    CHECK(serial_from_filename("SCES53133.BIN.ENC", s) == 0);
    CHECK_STR(s, "SCES-53133");
    CHECK(serial_from_filename("ico (scus-97113).iso", s) == 0);
    CHECK_STR(s, "SCUS-97113");

    CHECK(serial_from_filename("God of War II.iso", s) == -1);
    CHECK(serial_from_filename("MYGAMES12345.iso", s) == -1);
    CHECK(serial_from_filename("ABCD-123456.iso", s) == -1);
}

static void test_serial_region(void)
{
    CHECK_STR(serial_region("SLUS-20946"), "NTSC-U");
    CHECK_STR(serial_region("SCES-53133"), "PAL");
    CHECK_STR(serial_region("SLPM-65002"), "NTSC-J");
    CHECK_STR(serial_region("SCAJ-20001"), "NTSC-J");
    CHECK_STR(serial_region("SLKA-25001"), "NTSC-K");
    CHECK_STR(serial_region(""), "");
}

static void test_title_from_filename(void)
{
    char t[GAME_TITLE_MAX];

    library_title_from_filename("SLUS_209.46.Grand Theft Auto San Andreas.iso", t, sizeof(t));
    CHECK_STR(t, "Grand Theft Auto San Andreas");
    library_title_from_filename("Tekken 5 [SLUS-21059].iso", t, sizeof(t));
    CHECK_STR(t, "Tekken 5");
    library_title_from_filename("Burnout_3_Takedown.BIN.ENC", t, sizeof(t));
    CHECK_STR(t, "Burnout 3 Takedown");
    library_title_from_filename("  Ico  (SCUS-97113) .ISO", t, sizeof(t));
    CHECK_STR(t, "Ico");
    library_title_from_filename("SCES53133.BIN.ENC", t, sizeof(t));
    CHECK_STR(t, "");

    library_title_from_filename("A very long title.iso", t, 7);
    CHECK_STR(t, "A very");
}

static void test_library_scan(void)
{
    char dir[600], path[700];
    unsigned char payload[3000];
    static const char gow_cnf[] = "BOOT2 = cdrom0:\\SCUS_974.81;1\nVMODE = NTSC\n";
    iso_entry entries[] = { { "SYSTEM.CNF;1", gow_cnf, sizeof(gow_cnf) - 1, 0 } };
    library_t lib;
    struct stat st;

    path_join(dir, sizeof(dir), tmp_root, "PS2ISO");
    mkdir(dir, 0777);
    fill_pattern(payload, sizeof(payload), 11);

    /* The serial inside the ISO wins over the file name. */
    path_join(path, sizeof(path), dir, "God of War II.iso");
    make_iso(path, 2048, 0, entries, 1);
    path_join(path, sizeof(path), dir, "Burnout 3 [SLUS-21050].BIN.ENC");
    write_file(path, payload, sizeof(payload));
    path_join(path, sizeof(path), dir, "SCES53133.bin.enc");
    write_file(path, payload, 1000);
    /* Ignored: macOS metadata, other files, folders. */
    path_join(path, sizeof(path), dir, "._God of War II.iso");
    write_file(path, payload, 10);
    path_join(path, sizeof(path), dir, "notes.txt");
    write_file(path, payload, 10);
    path_join(path, sizeof(path), dir, "covers");
    mkdir(path, 0777);
    path_join(path, sizeof(path), dir, "folder.iso");
    mkdir(path, 0777);

    library_init(&lib);
    CHECK(library_scan_dir(&lib, dir) == 3);
    library_sort(&lib);
    CHECK(lib.count == 3);

    if (lib.count == 3) {
        CHECK_STR(lib.games[0].title, "Burnout 3");
        CHECK_STR(lib.games[0].serial, "SLUS-21050");
        CHECK(lib.games[0].format == GAME_FMT_CLASSIC);
        CHECK(lib.games[0].size == sizeof(payload));

        CHECK_STR(lib.games[1].title, "God of War II");
        CHECK_STR(lib.games[1].serial, "SCUS-97481");
        CHECK(lib.games[1].format == GAME_FMT_ISO);
        CHECK(stat(lib.games[1].path, &st) == 0 && lib.games[1].size == (unsigned long long)st.st_size);

        CHECK_STR(lib.games[2].title, "SCES-53133");
        CHECK_STR(lib.games[2].serial, "SCES-53133");
        CHECK(lib.games[2].format == GAME_FMT_CLASSIC);
    }

    path_join(path, sizeof(path), tmp_root, "does-not-exist");
    CHECK(library_scan_dir(&lib, path) == -1);
    CHECK(lib.count == 3);
    library_free(&lib);
    CHECK(lib.count == 0 && lib.games == NULL);
}

typedef struct {
    unsigned long long last_done, last_total;
    int calls, cancel_after;
} progress_log;

static int on_progress(unsigned long long done, unsigned long long total, void *user)
{
    progress_log *p = user;

    p->calls++;
    p->last_done = done;
    p->last_total = total;
    return p->cancel_after && p->calls >= p->cancel_after;
}

static void test_launch_ps2classic(void)
{
    char root[600], placeholder[700], usrdir[800], iso[900], config[900], tmp[900], path[700];
    static unsigned char game[2500000];
    static const char cfg[] = "netemu config bytes";
    game_t g;
    progress_log log;

    path_join(root, sizeof(root), tmp_root, "launch");
    mkdir(root, 0777);
    path_join(placeholder, sizeof(placeholder), root, "PS2U10000");
    path_join(usrdir, sizeof(usrdir), placeholder, "USRDIR");
    path_join(iso, sizeof(iso), usrdir, "ISO.BIN.ENC");
    path_join(config, sizeof(config), usrdir, "CONFIG");
    path_join(tmp, sizeof(tmp), usrdir, "ISO.BIN.ENC.tmp");
    fill_pattern(game, sizeof(game), 5);

    memset(&g, 0, sizeof(g));
    path_join(g.path, sizeof(g.path), root, "Ico.BIN.ENC");
    write_file(g.path, game, sizeof(game));
    g.format = GAME_FMT_CLASSIC;

    /* No placeholder installed yet. */
    CHECK(launch_ps2classic(&g, placeholder, NULL, NULL) == LAUNCH_ERR_NO_PLACEHOLDER);

    /* Placeholder with a previous game and its CONFIG still in place. */
    mkdir(placeholder, 0777);
    mkdir(usrdir, 0777);
    write_file(iso, "old game", 8);
    write_file(config, "old config", 10);
    path_join(path, sizeof(path), root, "Ico.CONFIG");
    write_file(path, cfg, sizeof(cfg));

    memset(&log, 0, sizeof(log));
    CHECK(launch_ps2classic(&g, placeholder, on_progress, &log) == LAUNCH_OK);
    CHECK(file_equals(iso, game, sizeof(game)));
    CHECK(file_equals(config, cfg, sizeof(cfg)));
    CHECK(!exists(tmp));
    CHECK(log.calls >= 2);  /* copied in 1 MB chunks */
    CHECK(log.last_done == sizeof(game) && log.last_total == sizeof(game));

    /* "<name>.BIN.ENC.CONFIG" works too. */
    unlink(path);
    path_join(path, sizeof(path), root, "Ico.BIN.ENC.CONFIG");
    write_file(path, cfg, 5);
    CHECK(launch_ps2classic(&g, placeholder, NULL, NULL) == LAUNCH_OK);
    CHECK(file_equals(config, cfg, 5));

    /* A game without a CONFIG must not inherit the previous one. */
    unlink(path);
    CHECK(launch_ps2classic(&g, placeholder, NULL, NULL) == LAUNCH_OK);
    CHECK(file_equals(iso, game, sizeof(game)));
    CHECK(!exists(config));

    /* Cancelling leaves nothing that looks playable. */
    memset(&log, 0, sizeof(log));
    log.cancel_after = 1;
    CHECK(launch_ps2classic(&g, placeholder, on_progress, &log) == LAUNCH_ERR_CANCELLED);
    CHECK(!exists(iso));
    CHECK(!exists(tmp));

    /* Missing source file. */
    path_join(g.path, sizeof(g.path), root, "missing.BIN.ENC");
    CHECK(launch_ps2classic(&g, placeholder, NULL, NULL) == LAUNCH_ERR_COPY);
    CHECK(!exists(iso));

    /* Plain ISOs need Cobra, which isn't wired up yet. */
    g.format = GAME_FMT_ISO;
    CHECK(launcher_start(&g, NULL, NULL) == LAUNCH_ERR_NOT_IMPLEMENTED);
}

static size_t utf8_encode(unsigned cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

static void test_text(void)
{
    char out[64];
    unsigned c;

    utf8_to_cp1254("Başlat", out, sizeof(out));
    CHECK_STR(out, "Ba\xFElat");
    utf8_to_cp1254("Çıkış İĞŞ ğ", out, sizeof(out));
    CHECK_STR(out, "\xC7\xFDk\xFD\xFE \xDD\xD0\xDE \xF0");
    utf8_to_cp1254("Kütüphane…", out, sizeof(out));
    CHECK_STR(out, "K\xFCt\xFCphane\x85");
    utf8_to_cp1254("日本", out, sizeof(out));
    CHECK_STR(out, "??");
    utf8_to_cp1254("a\xFF" "b\xC3", out, sizeof(out));  /* invalid lead byte, cut-off sequence */
    CHECK_STR(out, "a?b?");
    CHECK(utf8_to_cp1254("abcdef", out, 4) == 3);
    CHECK_STR(out, "abc");

    /* Every assigned byte survives a UTF-8 round trip. */
    for (c = 0x20; c <= 0xFF; c++) {
        unsigned cp = cp1254_to_unicode((unsigned char)c);
        char utf8[4] = { 0 };

        if (!cp)
            continue;
        utf8_encode(cp, utf8);
        utf8_to_cp1254(utf8, out, sizeof(out));
        CHECK((unsigned char)out[0] == c && out[1] == '\0');
    }

    CHECK(cp1254_ascii_fallback(0xFD) == 'i');
    CHECK(cp1254_ascii_fallback('a') == 0);
}

static void test_lang(void)
{
    int l, s;
    char out[512];

    for (l = 0; l < LANG_COUNT; l++) {
        lang_set((lang_id)l);
        CHECK(lang_get() == (lang_id)l);
        for (s = 0; s < STR_COUNT; s++) {
            const char *str = lang_str((str_id)s);

            CHECK(str[0] != '\0');
            /* Everything in the UI must be drawable with the Windows-1254 font. */
            utf8_to_cp1254(str, out, sizeof(out));
            CHECK(strchr(out, '?') == NULL);
        }
        CHECK(strstr(lang_str(STR_GAME_COUNT), "%d") != NULL);
    }
    lang_set(LANG_TR);
    CHECK_STR(lang_str(STR_LAUNCH), "Başlat");
    lang_set(LANG_EN);
    CHECK_STR(lang_str(STR_LAUNCH), "Launch");
    CHECK_STR(lang_str(STR_COUNT), "");
}

static int near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static void test_coverflow(void)
{
    cf_pose p, q, center, side;
    float d, pos;
    int i;

    coverflow_pose(0.0f, &center);
    CHECK(near(center.x, 0) && near(center.angle, 0) && near(center.shade, 1) && near(center.alpha, 1));

    coverflow_pose(1.0f, &side);
    CHECK(center.z < side.z);  /* selected cover is closest */
    CHECK(side.x > 0 && side.shade < 1);

    for (d = 0.25f; d < CF_VISIBLE; d += 0.5f) {
        coverflow_pose(d, &p);
        coverflow_pose(-d, &q);
        CHECK(near(p.x, -q.x) && near(p.angle, -q.angle) && near(p.z, q.z));
    }

    /* No jumps at the slot boundary. */
    coverflow_pose(0.999f, &p);
    coverflow_pose(1.001f, &q);
    CHECK(near(p.x, q.x) && near(p.z, q.z) && near(p.angle, q.angle) && near(p.shade, q.shade));

    /* Ordered left to right. */
    for (d = -CF_VISIBLE; d < CF_VISIBLE; d += 0.5f) {
        coverflow_pose(d, &p);
        coverflow_pose(d + 0.5f, &q);
        CHECK(p.x < q.x);
    }

    coverflow_pose(CF_VISIBLE, &p);
    CHECK(near(p.alpha, 0));
    coverflow_pose(CF_VISIBLE - 0.5f, &p);
    CHECK(p.alpha > 0 && p.alpha < 1);

    pos = 0.0f;
    for (i = 0; i < 120; i++)
        pos = coverflow_step(pos, 7.0f);
    CHECK(pos == 7.0f);
}

static void test_covers(void)
{
    char out[COVER_MAX_CANDIDATES][GAME_PATH_MAX];
    game_t g;

    memset(&g, 0, sizeof(g));
    strcpy(g.path, "/dev_hdd0/PS2ISO/Tekken 5.iso");
    strcpy(g.serial, "SLUS-21059");
    CHECK(covers_candidates(&g, out, COVER_MAX_CANDIDATES) == 4);
    CHECK_STR(out[0], "/dev_hdd0/PS2ISO/covers/SLUS-21059.png");
    CHECK_STR(out[1], "/dev_hdd0/PS2ISO/covers/SLUS-21059.jpg");
    CHECK_STR(out[2], "/dev_hdd0/PS2ISO/Tekken 5.png");
    CHECK_STR(out[3], "/dev_hdd0/PS2ISO/Tekken 5.jpg");

    strcpy(g.path, "/dev_usb000/PS2ISO/Ico.BIN.ENC");
    g.serial[0] = '\0';
    CHECK(covers_candidates(&g, out, COVER_MAX_CANDIDATES) == 2);
    CHECK_STR(out[0], "/dev_usb000/PS2ISO/Ico.png");
    CHECK(covers_candidates(&g, out, 1) == 1);
}

int main(void)
{
    const char *base = getenv("TMPDIR");

    snprintf(tmp_root, sizeof(tmp_root), "%s/ps3sx2-tests-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(tmp_root)) {
        perror("mkdtemp");
        return 2;
    }

    test_iso_layouts();
    test_iso_rejects_garbage();
    test_serial_from_system_cnf();
    test_serial_from_filename();
    test_serial_region();
    test_title_from_filename();
    test_library_scan();
    test_launch_ps2classic();
    test_text();
    test_lang();
    test_coverflow();
    test_covers();

    remove_tree(tmp_root);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
