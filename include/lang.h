#ifndef PS3SX2_LANG_H
#define PS3SX2_LANG_H

typedef enum {
    LANG_EN,
    LANG_TR,
    LANG_COUNT
} lang_id;

typedef enum {
    STR_LIBRARY,
    STR_GAME_COUNT,         /* printf format, takes the number of games */
    STR_LAUNCH,
    STR_EXIT,
    STR_RESCAN,
    STR_FAST_SCROLL,
    STR_CONTINUE,
    STR_CANCEL,
    STR_EMPTY_TITLE,
    STR_EMPTY_HINT,
    STR_COPYING,
    STR_READY_TITLE,
    STR_READY_HINT,
    STR_ERR_NO_PLACEHOLDER,
    STR_ERR_NO_PLACEHOLDER_HINT,
    STR_ERR_COPY,
    STR_ERR_CANCELLED,
    STR_ERR_ISO_TITLE,
    STR_ERR_ISO_HINT,
    STR_UNKNOWN_SERIAL,
    STR_COUNT
} str_id;

void lang_set(lang_id id);
lang_id lang_get(void);

/* UI string for the current language, UTF-8. */
const char *lang_str(str_id id);

#endif
