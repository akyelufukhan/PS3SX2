ps2_netemu CONFIG files for PS3SX2
==================================

Community-made custom configs that fix freezes, black screens, FMV and other
issues in PS2 games running under ps2_netemu. When PS3SX2 starts an .iso and
no <game>.iso.CONFIG sits next to it, it copies the matching file from here,
picked by the disc's boot file name (e.g. SLES_542.03.CONFIG for PES 6).

Source: webMAN MOD's PS2 CONFIG database, CUSTOM folder
  https://github.com/aldostools/webMAN-MOD/tree/master/_Projects_/updater/PS2CONFIG
  (commit 70ad8ef, 2026-10-21), distributed there under the GNU GPL v3.
The configs are the work of the PS3 scene; see
  https://www.psdevwiki.com/ps3/PS2_Custom_Configs
for authors and what each command does.

Only the custom configs are included. Official configs extracted from Sony's
PS2 Classics are not; PS3SX2 still uses them if webMAN MOD's database is
installed on the console.
