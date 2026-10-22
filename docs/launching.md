# How PS3SX2 starts a PS2 game

PS3 Slim and Super Slim have no PS2 chips. The firmware ships a software emulator,
`ps2_netemu`, which Sony uses for PS2 Classics from the PlayStation Store. PS3SX2 is
a front-end: it lists your games and hands the chosen one to that emulator. There are
two ways to do the hand-off, and which one works depends on how the console is
modded.

## Route 1: PS2 Classics (`.BIN.ENC`) — implemented

Works on **HEN and CFW**, so it's the only route on a Super Slim.

A `.BIN.ENC` file is a PS2 disc image encrypted the way Sony's PS2 Classics are.
The **PS2 Classics Placeholder** (title ID `PS2U10000`) is an installed game that
plays whatever image sits in its folder. Launching therefore means copying:

1. Delete the previous `ISO.BIN.ENC` and `CONFIG` from
   `/dev_hdd0/game/PS2U10000/USRDIR/` (frees space, and a stale `CONFIG` from another
   game can break this one).
2. Copy the game to `ISO.BIN.ENC.tmp`, then rename it to `ISO.BIN.ENC`, so a copy that
   was cut off never looks playable.
3. If `<game>.BIN.ENC.CONFIG` or `<game>.CONFIG` exists next to the game, copy it to
   `USRDIR/CONFIG`. These are `ps2_netemu` compatibility settings.
4. Tell the user to start "PS2 Classics Placeholder" from the XMB, then exit.

The code is `launch_ps2classic()` in [`source/launcher.c`](../source/launcher.c), and
`tests/tests.c` covers each of those steps.

Why not raw `.iso` on HEN? HEN doesn't provide the Cobra disc emulation needed to mount
a PS2 ISO. webMAN MOD refuses for the same reason ("HEN does not support PS2 ISO").

## Route 2: plain `.iso` through Cobra — implemented, untested on a console

Works only on **CFW with Cobra**, and needs nothing besides PS3SX2: Cobra ships with the
CFW and `ps2_netemu` with the firmware. The firmware is shown the ISO as if a PS2 DVD were
in the drive, and on Slim consoles it runs it with `ps2_netemu`.

`launch_ps2iso()` in [`source/launcher.c`](../source/launcher.c) holds the rules, and
[`source/cobra.c`](../source/cobra.c) talks to Cobra through lv2 syscall 8. The sequence is
adapted from IRISMAN (`source/cobra.c`, `source/mount_game.h`; GPL-3.0 like PS3SX2):

1. Detect the payload once, on the first `.iso` launch. PS3HEN echoes opcode `0x1337`
   (the same check webMAN MOD uses) and can't mount PS2 images, so it gets the
   "needs a CFW with Cobra" message. Cobra answers `GET_VERSION` (`0x7000`).
2. Refuse images outside `/dev_hdd0/`. Cobra mounts PS2 ISOs from the internal drive only;
   IRISMAN falls back to a VSH plugin for USB drives and webMAN MOD copies them over first.
3. Delete `/dev_hdd0/classic_ps2` and `/dev_hdd0/tmp/loadoptical`, flags Cobra 8.x leaves
   behind that would make the next disc boot as a PS2 Classic or a real optical disc.
4. Unmount any emulated disc and fake an eject event on the Blu-ray drive.
5. Mount the ISO as a PS2 DVD with one data track from LBA 0
   (`MOUNT_PS2_DISCFILE`, `0x7028`), then fake an insert event.
6. Tell the user to start the PS2 disc from the XMB, and exit.

`tests/tests.c` covers steps 1–3 and 5 with a fake console; the syscalls themselves can only
be checked on a real CFW console (RPCS3 doesn't emulate Cobra).

## Ideas for later

- Start the placeholder or the mounted disc directly instead of going back to the XMB.
- Mount `.iso` games from USB drives, either by copying them to the internal drive first or
  through a VSH plugin like IRISMAN does.
- Ship a `ps2_netemu` CONFIG database keyed by serial, like webMAN MOD's PS2CONFIG.
- Split images (`.iso.0`, `.iso.1`, …) for FAT32 USB drives.
