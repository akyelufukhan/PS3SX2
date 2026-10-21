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

## Route 2: plain `.iso` through Cobra — planned

Works only on **CFW with Cobra**. The firmware is shown the ISO as if a PS2 DVD were in
the drive, and on non-backward-compatible models it picks `ps2_netemu` by itself.

The plan is to port this from webMAN MOD, which is GPL-3.0 like PS3SX2:

- `include/mount/mount_cobra.h`, the `/PS2ISO` branch: mounts with
  `mount_ps_disc_image(..., EMU_PS2_DVD)` and sends a fake disc-insert event.
- `include/mount/ps2_classic.h`: `enable_ps2netemu_cobra()` switches between `ps2_emu`
  and `ps2_netemu` on backward-compatible fat models (Cobra syscall 8).

Until then `launch_ps2iso()` returns `LAUNCH_ERR_NOT_IMPLEMENTED` and the UI says so.

## Ideas for later

- Start the placeholder directly instead of asking the user to do it from the XMB.
- Ship a `ps2_netemu` CONFIG database keyed by serial, like webMAN MOD's PS2CONFIG.
- Split images (`.iso.0`, `.iso.1`, …) for FAT32 USB drives.
