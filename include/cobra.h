#ifndef PS3SX2_COBRA_H
#define PS3SX2_COBRA_H

/*
 * Just enough of Cobra, the payload built into most CFWs, to mount a PS2 ISO.
 * PS3 only: these are lv2 syscalls.
 */

typedef enum {
    PAYLOAD_NONE,   /* OFW, or a payload we don't know */
    PAYLOAD_HEN,    /* PS3HEN: answers some Cobra calls but can't mount PS2 ISOs */
    PAYLOAD_COBRA,  /* CFW with Cobra */
} payload_kind;

payload_kind cobra_detect(void);

/*
 * Unmounts any emulated disc, mounts `path` as a PS2 DVD and tells the system a
 * disc was inserted, so it shows up in the XMB. Returns 0 on success.
 */
int cobra_mount_ps2_iso(const char *path);

#endif
