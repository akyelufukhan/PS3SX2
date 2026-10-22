/*
 * Minimal Cobra client for mounting PS2 ISOs.
 *
 * Opcodes, structures and the eject/insert sequence are adapted from IRISMAN's
 * source/cobra.c and source/mount_game.h (https://github.com/aldostools/IRISMAN,
 * GPL-3.0), which in turn follow cobralib from the Cobra payload. PS3SX2 is
 * GPL-3.0 as well.
 */
#include "cobra.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <ppu-lv2.h>

#define SYSCALL8_OPCODE_GET_VERSION        0x7000
#define SYSCALL8_OPCODE_GET_DISC_TYPE      0x7020
#define SYSCALL8_OPCODE_FAKE_STORAGE_EVENT 0x7022
#define SYSCALL8_OPCODE_MOUNT_PS2_DISCFILE 0x7028
#define SYSCALL8_OPCODE_UMOUNT_DISCFILE    0x702C
#define SYSCALL8_OPCODE_PS3HEN             0x1337  /* PS3HEN echoes this back; webMAN MOD checks the same way */

#define BDVD_DRIVE 0x101000000000006ULL

/* Storage events Cobra fakes on the Blu-ray drive. */
#define EVENT_EJECT_1  4
#define EVENT_EJECT_2  8
#define EVENT_INSERT_1 7
#define EVENT_INSERT_2 3

typedef struct {
    uint8_t reserved;
    uint8_t adr_control;
    uint8_t track_number;
    uint8_t reserved2;
    uint32_t track_start_addr;
} __attribute__((packed)) scsi_track;

LV2_SYSCALL sys8_call(u64 opcode)
{
    lv2syscall1(8, opcode);
    return_to_user_prog(s32);
}

LV2_SYSCALL sys8_get_version(u32 *version)
{
    lv2syscall2(8, SYSCALL8_OPCODE_GET_VERSION, (u64)version);
    return_to_user_prog(s32);
}

LV2_SYSCALL sys8_get_disc_type(u32 *real, u32 *effective, u32 *fake)
{
    lv2syscall4(8, SYSCALL8_OPCODE_GET_DISC_TYPE, (u64)real, (u64)effective, (u64)fake);
    return_to_user_prog(s32);
}

LV2_SYSCALL sys8_fake_storage_event(u64 event, u64 param, u64 device)
{
    lv2syscall4(8, SYSCALL8_OPCODE_FAKE_STORAGE_EVENT, event, param, device);
    return_to_user_prog(s32);
}

LV2_SYSCALL sys8_mount_ps2_discfile(u32 count, u32 *files, u32 track_count, scsi_track *tracks)
{
    lv2syscall5(8, SYSCALL8_OPCODE_MOUNT_PS2_DISCFILE, count, (u64)files, track_count, (u64)tracks);
    return_to_user_prog(s32);
}

LV2_SYSCALL sys8_umount_discfile(void)
{
    lv2syscall1(8, SYSCALL8_OPCODE_UMOUNT_DISCFILE);
    return_to_user_prog(s32);
}

payload_kind cobra_detect(void)
{
    u32 version = 0x99999999;

    if ((u32)sys8_call(SYSCALL8_OPCODE_PS3HEN) == SYSCALL8_OPCODE_PS3HEN)
        return PAYLOAD_HEN;
    /* Cobra overwrites the value; OFW fails the call or leaves it untouched. */
    if (sys8_get_version(&version) >= 0 && version != 0x99999999)
        return PAYLOAD_COBRA;
    return PAYLOAD_NONE;
}

int cobra_mount_ps2_iso(const char *path)
{
    /* Cobra takes 32-bit pointers; PSL1GHT user memory lives below 4 GB. */
    u32 files[1] = { (u32)(u64)path };
    scsi_track track = { 0, 0x14, 1, 0, 0 };  /* a single data track from LBA 0 */
    u32 ejected = 0;
    s32 ret;

    /* Take out whatever disc is emulated now, and let the XMB see it leave. */
    sys8_umount_discfile();
    usleep(4000);
    sys8_get_disc_type(&ejected, NULL, NULL);
    sys8_fake_storage_event(EVENT_EJECT_1, 0, BDVD_DRIVE);
    sys8_fake_storage_event(EVENT_EJECT_2, 0, BDVD_DRIVE);

    ret = sys8_mount_ps2_discfile(1, files, 1, &track);
    if (ret != 0)
        return ret;

    /* Report the "new" disc so the XMB lists it in the Game column. */
    sys8_fake_storage_event(EVENT_INSERT_1, 0, BDVD_DRIVE);
    return sys8_fake_storage_event(EVENT_INSERT_2, (u64)ejected << 32, BDVD_DRIVE);
}
