/*
 * Host-side unit tests for the platform-independent parts of PS3SX2.
 * Build and run with `make -C tests`; no PS3 toolchain needed.
 */
#include "canvas.h"
#include "coverdl.h"
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

static void test_serial_boot_name(void)
{
    char b[12];

    CHECK(serial_boot_name("SLES-54203", b) == 0);
    CHECK_STR(b, "SLES_542.03");
    CHECK(serial_boot_name("slus20946", b) == 0);
    CHECK_STR(b, "SLUS_209.46");
    CHECK(serial_boot_name("", b) == -1);
    CHECK(serial_boot_name("ICO", b) == -1);
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

}

static void test_place_iso_config(void)
{
    static const unsigned char pes6[12] = { 0x3d, 0, 0, 0, 'W', 'D', 0, 0, 0x2a, 0, 0, 0 };
    char root[600], bundled[700], webman[700], isodir[700], cfg[900], dst[GAME_PATH_MAX + 8];
    const char *dirs[2];
    game_t g;

    path_join(root, sizeof(root), tmp_root, "cfg");
    path_join(bundled, sizeof(bundled), root, "bundled");
    path_join(webman, sizeof(webman), root, "webman");
    path_join(isodir, sizeof(isodir), root, "PS2ISO");
    mkdir(root, 0777);
    mkdir(bundled, 0777);
    mkdir(webman, 0777);
    mkdir(isodir, 0777);
    dirs[0] = bundled;
    dirs[1] = webman;

    path_join(cfg, sizeof(cfg), bundled, "SLES_542.03.CONFIG");
    write_file(cfg, pes6, sizeof(pes6));
    path_join(cfg, sizeof(cfg), webman, "SLES_542.03.CONFIG");
    write_file(cfg, "webman", 6);
    path_join(cfg, sizeof(cfg), webman, "SLUS_209.46.CONFIG");
    write_file(cfg, "gta", 3);

    memset(&g, 0, sizeof(g));
    g.format = GAME_FMT_ISO;
    path_join(g.path, sizeof(g.path), isodir, "PES 6.iso");
    strcpy(g.serial, "SLES-54203");
    snprintf(dst, sizeof(dst), "%s.CONFIG", g.path);

    /* No folders configured: nothing happens. */
    launcher_set_config_dirs(NULL, 0);
    CHECK(launcher_place_iso_config(&g) == 0);
    CHECK(!exists(dst));

    /* First folder wins. */
    launcher_set_config_dirs(dirs, 2);
    CHECK(launcher_place_iso_config(&g) == 1);
    CHECK(file_equals(dst, pes6, sizeof(pes6)));

    /* A CONFIG the user put there is never replaced. */
    write_file(dst, "mine", 4);
    CHECK(launcher_place_iso_config(&g) == 1);
    CHECK(file_equals(dst, "mine", 4));

    /* Falls back to later folders. */
    path_join(g.path, sizeof(g.path), isodir, "GTA.iso");
    strcpy(g.serial, "SLUS-20946");
    snprintf(dst, sizeof(dst), "%s.CONFIG", g.path);
    CHECK(launcher_place_iso_config(&g) == 1);
    CHECK(file_equals(dst, "gta", 3));

    /* No match, or no serial: no file appears. */
    path_join(g.path, sizeof(g.path), isodir, "Ico.iso");
    snprintf(dst, sizeof(dst), "%s.CONFIG", g.path);
    strcpy(g.serial, "SCUS-97113");
    CHECK(launcher_place_iso_config(&g) == 0);
    g.serial[0] = '\0';
    CHECK(launcher_place_iso_config(&g) == 0);
    CHECK(!exists(dst));

    launcher_set_config_dirs(NULL, 0);
}

static int fake_cobra, fake_mount_result, fake_mount_calls;
static char fake_mounted[GAME_PATH_MAX];

static int fake_can_mount(void)
{
    return fake_cobra;
}

static int fake_mount(const char *path)
{
    fake_mount_calls++;
    snprintf(fake_mounted, sizeof(fake_mounted), "%s", path);
    return fake_mount_result;
}

static void test_launch_ps2iso(void)
{
    static const launch_platform console = { fake_can_mount, fake_mount };
    game_t g;

    memset(&g, 0, sizeof(g));
    g.format = GAME_FMT_ISO;
    strcpy(g.path, "/dev_hdd0/PS2ISO/Ico.iso");

    /* No Cobra layer at all (as on a PC), then HEN or OFW. */
    launcher_set_platform(NULL);
    CHECK(launcher_start(&g, NULL, NULL) == LAUNCH_ERR_NO_COBRA);
    launcher_set_platform(&console);
    fake_cobra = 0;
    CHECK(launcher_start(&g, NULL, NULL) == LAUNCH_ERR_NO_COBRA);
    CHECK(fake_mount_calls == 0);

    /* Cobra: USB images are refused before anything is touched. */
    fake_cobra = 1;
    strcpy(g.path, "/dev_usb000/PS2ISO/Ico.iso");
    CHECK(launcher_start(&g, NULL, NULL) == LAUNCH_ERR_ISO_NOT_ON_HDD);
    CHECK(fake_mount_calls == 0);

    strcpy(g.path, "/dev_hdd0/PS2ISO/Ico.iso");
    CHECK(launcher_start(&g, NULL, NULL) == LAUNCH_OK_DISC);
    CHECK(fake_mount_calls == 1);
    CHECK_STR(fake_mounted, "/dev_hdd0/PS2ISO/Ico.iso");

    fake_mount_result = -1;
    CHECK(launcher_start(&g, NULL, NULL) == LAUNCH_ERR_MOUNT);

    /* .BIN.ENC never goes through Cobra. */
    g.format = GAME_FMT_CLASSIC;
    fake_mount_calls = 0;
    launcher_start(&g, NULL, NULL);
    CHECK(fake_mount_calls == 0);

    launcher_set_platform(NULL);
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
    CHECK(unicode_ascii_fallback(0x015E) == 'S');
    CHECK(unicode_ascii_fallback(0x00FC) == 0);  /* ü is in every Latin font */
}

static void test_utf8_decode(void)
{
    const char *s = "aş€\xF0\x9F\x8E\xAE";
    const char *bad = "\xC3" "x" "\xFF";

    CHECK(utf8_decode(&s) == 'a');
    CHECK(utf8_decode(&s) == 0x015F);
    CHECK(utf8_decode(&s) == 0x20AC);
    CHECK(utf8_decode(&s) == 0x1F3AE);
    CHECK(utf8_decode(&s) == 0);
    CHECK(utf8_decode(&s) == 0);  /* stays at the end */

    CHECK(utf8_decode(&bad) == 0xFFFD);  /* cut-off sequence doesn't swallow the next byte */
    CHECK(utf8_decode(&bad) == 'x');
    CHECK(utf8_decode(&bad) == 0xFFFD);
    CHECK(utf8_decode(&bad) == 0);
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
        CHECK(strstr(lang_str(STR_COVERS_DONE), "%d") != NULL);
        CHECK(strstr(lang_str(STR_COVERS_MISSING), "%d") != NULL);
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
    CHECK(near(center.focus, 1) && near(side.focus, 0));
    coverflow_pose(-0.5f, &p);
    CHECK(near(p.focus, 0.5f));
    CHECK(side.x > 0 && side.shade < 1);
    CHECK(side.angle > 0);  /* checked in RPCS3: positive faces right-hand covers to the center */

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

static void test_canvas(void)
{
    uint32_t px[20 * 10];
    canvas c = { px, 20, 10 };
    static const unsigned char glyph[2 * 3] = { 255, 0, 128, 0, 0, 255 };

    canvas_fill(&c, 0x00102030);
    CHECK(px[0] == 0xff102030 && px[199] == 0xff102030);  /* always opaque */

    canvas_rect(&c, 18, 8, 10, 10, 0xffffffff);  /* clipped at the bottom-right corner */
    CHECK(px[8 * 20 + 18] == 0xffffffff && px[9 * 20 + 19] == 0xffffffff);
    CHECK(px[7 * 20 + 18] == 0xff102030 && px[8 * 20 + 17] == 0xff102030);

    canvas_rect(&c, -5, -5, 6, 6, 0x80ffffff);  /* half alpha, clipped at the top-left */
    CHECK(px[0] == 0xff878f97);
    CHECK(px[1] == 0xff102030);

    canvas_fill(&c, 0);
    canvas_disc(&c, 10, 5, 3, 0xff00ff00);
    CHECK(px[5 * 20 + 10] == 0xff00ff00);
    CHECK(px[0] == 0xff000000);

    canvas_fill(&c, 0);
    canvas_ring(&c, 10, 5, 4, 1, 0xffff0000);
    CHECK(px[5 * 20 + 10] == 0xff000000);  /* hole in the middle */
    CHECK((px[5 * 20 + 14] & 0x00ff0000) != 0);  /* on the ring */

    canvas_disc(&c, -50, -50, 3, 0xffffffff);  /* entirely outside: no crash, no change */
    canvas_ring(&c, 100, 100, 5, 2, 0xffffffff);
    CHECK(px[0] == 0xff000000);

    canvas_fill(&c, 0);
    canvas_mask(&c, 19, 9, glyph, 2, 3, 3, 0xffffff);  /* mostly off-canvas */
    CHECK(px[9 * 20 + 19] == 0xffffffff);
    canvas_mask(&c, 0, 0, glyph, 3, 2, 3, 0xffffff);
    CHECK(px[0] == 0xffffffff && px[1] == 0xff000000 && px[2] == 0xff808080 && px[22] == 0xffffffff);
}

static const unsigned char jpeg_bytes[] = { 0xFF, 0xD8, 0xFF, 0xE0, 'J', 'F', 'I', 'F' };

/* Fake network: serial in the URL decides the answer. */
static int fake_fetch_calls;

static int fake_fetch(const char *url, const char *dst, void *user)
{
    (void)user;
    fake_fetch_calls++;
    if (strstr(url, "DOWN"))
        return -1;
    if (strstr(url, "SLUS-99999"))
        return 1;                                   /* 404 */
    if (strstr(url, "SLUS-88888")) {
        write_file(dst, "<html>", 6);               /* captive portal, not a JPEG */
        return 0;
    }
    write_file(dst, jpeg_bytes, sizeof(jpeg_bytes));
    return 0;
}

static int cancel_at;

static int fake_cover_progress(int done, int total, const game_t *g, void *user)
{
    (void)total;
    (void)g;
    (void)user;
    return cancel_at && done >= cancel_at;
}

static void test_coverdl(void)
{
    char url[256], target[GAME_PATH_MAX], dir[600], path[700];
    game_t games[6];
    library_t lib;
    coverdl_result r;
    int i;

    CHECK(coverdl_template_ok(COVERDL_DEFAULT_URL));
    CHECK(!coverdl_template_ok("ftp://example.com/%s.jpg"));
    CHECK(!coverdl_template_ok("https://example.com/%s/%s.jpg"));
    CHECK(!coverdl_template_ok("https://example.com/%d.jpg"));
    CHECK(!coverdl_template_ok("https://example.com/cover.jpg"));
    CHECK(!coverdl_template_ok("https://example.com/%%s.jpg"));

    CHECK(coverdl_url(COVERDL_DEFAULT_URL, "SLES-54203", url, sizeof(url)) == 0);
    CHECK_STR(url, "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/SLES-54203.jpg");
    CHECK(coverdl_url("http://h/%s", "SLES-54203", url, 12) == -1);
    CHECK(coverdl_url(COVERDL_DEFAULT_URL, "", url, sizeof(url)) == -1);

    CHECK(coverdl_is_jpeg(jpeg_bytes, sizeof(jpeg_bytes)));
    CHECK(!coverdl_is_jpeg((const unsigned char *)"<html>", 6));
    CHECK(!coverdl_is_jpeg(jpeg_bytes, 2));

    path_join(dir, sizeof(dir), tmp_root, "coverdl");
    mkdir(dir, 0777);
    memset(games, 0, sizeof(games));
    for (i = 0; i < 6; i++)
        games[i].format = GAME_FMT_ISO;
    path_join(games[0].path, GAME_PATH_MAX, dir, "PES.iso");       strcpy(games[0].serial, "SLES-54203");
    path_join(games[1].path, GAME_PATH_MAX, dir, "PES copy.iso");  strcpy(games[1].serial, "SLES-54203");
    path_join(games[2].path, GAME_PATH_MAX, dir, "GTA.iso");       strcpy(games[2].serial, "SLUS-20946");
    path_join(games[3].path, GAME_PATH_MAX, dir, "Ico.BIN.ENC");   /* no serial */
    path_join(games[4].path, GAME_PATH_MAX, dir, "Rare.iso");      strcpy(games[4].serial, "SLUS-99999");
    path_join(games[5].path, GAME_PATH_MAX, dir, "Portal.iso");    strcpy(games[5].serial, "SLUS-88888");
    lib.games = games;
    lib.count = 6;
    lib.capacity = 6;

    CHECK(coverdl_target(&games[0], target, sizeof(target)) == 0);
    snprintf(path, sizeof(path), "%s/covers/SLES-54203.jpg", dir);
    CHECK_STR(target, path);
    CHECK(coverdl_target(&games[3], target, sizeof(target)) == -1);

    /* GTA already has a cover next to the ISO. */
    path_join(path, sizeof(path), dir, "GTA.png");
    write_file(path, "png", 3);
    CHECK(coverdl_needs_cover(&games[0]));
    CHECK(!coverdl_needs_cover(&games[2]));
    CHECK(!coverdl_needs_cover(&games[3]));

    fake_fetch_calls = 0;
    cancel_at = 0;
    coverdl_run(&lib, COVERDL_DEFAULT_URL, fake_fetch, fake_cover_progress, NULL, &r);
    CHECK(r.total == 3);  /* PES once, Rare, Portal */
    CHECK(r.downloaded == 1 && r.missing == 1 && r.failed == 1 && !r.cancelled);
    CHECK(fake_fetch_calls == 3);
    snprintf(path, sizeof(path), "%s/covers/SLES-54203.jpg", dir);
    CHECK(file_equals(path, jpeg_bytes, sizeof(jpeg_bytes)));
    snprintf(path, sizeof(path), "%s/covers/SLUS-88888.jpg", dir);
    CHECK(!exists(path));
    snprintf(path, sizeof(path), "%s/covers/SLUS-88888.jpg.part", dir);
    CHECK(!exists(path));
    CHECK(!coverdl_needs_cover(&games[1]));  /* the copy shares the downloaded cover */

    /* Nothing left that can be fetched except the two the archive lacks. */
    coverdl_run(&lib, COVERDL_DEFAULT_URL, fake_fetch, NULL, NULL, &r);
    CHECK(r.total == 2 && r.downloaded == 0);

    /* Cancelling stops before the next game. */
    cancel_at = 1;
    fake_fetch_calls = 0;
    coverdl_run(&lib, COVERDL_DEFAULT_URL, fake_fetch, fake_cover_progress, NULL, &r);
    CHECK(r.cancelled && fake_fetch_calls == 1);
    cancel_at = 0;

    /* No network: gives up after three errors in a row. */
    for (i = 0; i < 6; i++) {
        strcpy(games[i].serial, "DOWN-00000");
        games[i].serial[9] = (char)('0' + i);
    }
    fake_fetch_calls = 0;
    coverdl_run(&lib, "https://example.com/%s.jpg", fake_fetch, NULL, NULL, &r);
    CHECK(r.failed == 3 && fake_fetch_calls == 3);

    /* A broken template fails every game without touching the network. */
    strcpy(games[0].serial, "SLES-54204");
    fake_fetch_calls = 0;
    coverdl_run(&lib, "https://example.com/cover.jpg", fake_fetch, NULL, NULL, &r);
    CHECK(fake_fetch_calls == 0 && r.downloaded == 0);
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
    test_serial_boot_name();
    test_title_from_filename();
    test_library_scan();
    test_launch_ps2classic();
    test_launch_ps2iso();
    test_place_iso_config();
    test_text();
    test_utf8_decode();
    test_canvas();
    test_lang();
    test_coverflow();
    test_covers();
    test_coverdl();

    remove_tree(tmp_root);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
