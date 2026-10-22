#include "lang.h"

#include <stddef.h>

static const char *const strings[LANG_COUNT][STR_COUNT] = {
    [LANG_EN] = {
        [STR_LIBRARY]                 = "PS2 Library",
        [STR_GAME_COUNT]              = "%d games",
        [STR_LAUNCH]                  = "Launch",
        [STR_EXIT]                    = "Exit",
        [STR_RESCAN]                  = "Rescan",
        [STR_FAST_SCROLL]             = "Fast scroll",
        [STR_CONTINUE]                = "Continue",
        [STR_CANCEL]                  = "Cancel",
        [STR_EMPTY_TITLE]             = "No PS2 games found",
        [STR_EMPTY_HINT]              = "Put .iso or .BIN.ENC files in /dev_hdd0/PS2ISO or in a USB drive's PS2ISO folder",
        [STR_COPYING]                 = "Copying to PS2 Classics Placeholder…",
        [STR_READY_TITLE]             = "Ready to play",
        [STR_READY_HINT]              = "Start \"PS2 Classics Placeholder\" from the XMB",
        [STR_ERR_NO_PLACEHOLDER]      = "PS2 Classics Placeholder isn't installed",
        [STR_ERR_NO_PLACEHOLDER_HINT] = "Install it first, the README explains how",
        [STR_ERR_COPY]                = "Couldn't copy the game. Check free space on the HDD.",
        [STR_ERR_CANCELLED]           = "Copy cancelled",
        [STR_READY_DISC_TITLE]        = "Disc ready",
        [STR_READY_DISC_HINT]         = "Start the PS2 disc from the Game column in the XMB",
        [STR_ERR_NO_COBRA]            = "Starting an .iso needs a CFW with Cobra",
        [STR_ERR_NO_COBRA_HINT]       = "On HEN, use the .BIN.ENC version of this game",
        [STR_ERR_ISO_NOT_ON_HDD]      = "PS2 ISOs can only be mounted from the internal HDD",
        [STR_ERR_ISO_NOT_ON_HDD_HINT] = "Move the game to /dev_hdd0/PS2ISO",
        [STR_ERR_MOUNT]               = "Cobra couldn't mount this disc image",
        [STR_UNKNOWN_SERIAL]          = "Unknown serial",
    },
    [LANG_TR] = {
        [STR_LIBRARY]                 = "PS2 Kütüphanesi",
        [STR_GAME_COUNT]              = "%d oyun",
        [STR_LAUNCH]                  = "Başlat",
        [STR_EXIT]                    = "Çıkış",
        [STR_RESCAN]                  = "Yeniden tara",
        [STR_FAST_SCROLL]             = "Hızlı kaydır",
        [STR_CONTINUE]                = "Devam",
        [STR_CANCEL]                  = "İptal",
        [STR_EMPTY_TITLE]             = "PS2 oyunu bulunamadı",
        [STR_EMPTY_HINT]              = ".iso veya .BIN.ENC dosyalarını /dev_hdd0/PS2ISO ya da USB belleğin PS2ISO klasörüne koy",
        [STR_COPYING]                 = "PS2 Classics Placeholder'a kopyalanıyor…",
        [STR_READY_TITLE]             = "Oynamaya hazır",
        [STR_READY_HINT]              = "XMB'den \"PS2 Classics Placeholder\" uygulamasını başlat",
        [STR_ERR_NO_PLACEHOLDER]      = "PS2 Classics Placeholder yüklü değil",
        [STR_ERR_NO_PLACEHOLDER_HINT] = "Önce onu yükle, nasıl yapılacağı README'de",
        [STR_ERR_COPY]                = "Oyun kopyalanamadı. HDD'deki boş alanı kontrol et.",
        [STR_ERR_CANCELLED]           = "Kopyalama iptal edildi",
        [STR_READY_DISC_TITLE]        = "Disk hazır",
        [STR_READY_DISC_HINT]         = "XMB'de Oyun sütunundaki PS2 diskini başlat",
        [STR_ERR_NO_COBRA]            = ".iso başlatmak için Cobra'lı bir CFW gerekiyor",
        [STR_ERR_NO_COBRA_HINT]       = "HEN'de bu oyunun .BIN.ENC sürümünü kullan",
        [STR_ERR_ISO_NOT_ON_HDD]      = "PS2 ISO'ları yalnızca dahili diskten takılabiliyor",
        [STR_ERR_ISO_NOT_ON_HDD_HINT] = "Oyunu /dev_hdd0/PS2ISO klasörüne taşı",
        [STR_ERR_MOUNT]               = "Cobra bu disk imajını takamadı",
        [STR_UNKNOWN_SERIAL]          = "Seri kod bilinmiyor",
    },
};

static lang_id current = LANG_EN;

void lang_set(lang_id id)
{
    if ((int)id >= 0 && id < LANG_COUNT)
        current = id;
}

lang_id lang_get(void)
{
    return current;
}

const char *lang_str(str_id id)
{
    const char *s;

    if ((int)id < 0 || id >= STR_COUNT)
        return "";
    s = strings[current][id];
    /* Fall back to English so a missing translation never draws nothing. */
    if (!s)
        s = strings[LANG_EN][id];
    return s ? s : "";
}
