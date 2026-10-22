# PS3SX2

**English** | [Türkçe](README.tr.md)

A cover-flow launcher for PS2 games on the **PS3 Slim and Super Slim**.

![PS3SX2 running in RPCS3 with the Turkish UI](docs/screenshot.png)

<sub>Running in RPCS3 with a test library. Games without artwork get a generated cover.</sub>

Those models dropped the PS2 chips, so they can't play PS2 discs. The firmware still
carries Sony's PS2 software emulator, `ps2_netemu`, which runs the PS2 Classics sold on
the PlayStation Store. PS3SX2 lists the games in your `PS2ISO` folder as a shelf of
covers. Pick one and it hands the game to that emulator.

The name reads "PS3 Slim × PS2": S for Slim, X for the ✕ button that starts a game,
2 for PS2.

> **Status: works on a real console.** On a CECH-2504A with CFW, `.iso` games start through
> Cobra, and CONFIG fixes and cover downloads work. The HEN route (`.BIN.ENC` through the PS2
> Classics Placeholder) hasn't been tried on a console yet; reports are welcome.

## Features

- Scans `PS2ISO` on the internal drive and on every USB port (`/dev_usb000` to `/dev_usb007`)
- Reads each game's serial (`SLUS-20946`) straight from the disc image's `SYSTEM.CNF`,
  or from the file name when the image is encrypted
- 3D cover carousel: the selected cover faces you, the rest stand at an angle on either side
- Title, serial, region, size and format under the selected cover
- Covers in PNG or JPG, any size
- English and Turkish UI, picked from the console's system language
- On CFW with Cobra, mounts `.iso` games as a PS2 disc by itself: no webMAN MOD or other
  tools needed, just this one package
- Downloads missing covers by serial in the background, or on demand with □
- Ships the community's `ps2_netemu` fixes for about 1,400 games and applies the right one
  automatically when an `.iso` starts
- Loads `.BIN.ENC` games into the PS2 Classics Placeholder (HEN and CFW); you then start
  the placeholder from the XMB

## Compatibility

| Console | Firmware | `.iso` | `.BIN.ENC` (PS2 Classic) |
|---|---|---|---|
| Slim, CFW-capable (e.g. CECH-25xx) | CFW with Cobra | Works (tested on a CECH-2504A) | Written, untested |
| Slim | HEN | Not possible on HEN (tagged in the list) | Written, untested |
| Super Slim (CECH-4xxx) | HEN | Not possible on HEN (tagged in the list) | Written, untested |

HEN can't mount PS2 ISOs, so on HEN every game has to be a `.BIN.ENC`; see
[HEN and the PS2 Classics Placeholder](#hen-and-the-ps2-classics-placeholder).
[docs/launching.md](docs/launching.md) explains both routes in detail.

## What you need on the console

**CFW with Cobra:** only PS3SX2. Cobra comes with the CFW and `ps2_netemu` with the
firmware. Put `.iso` games in `/dev_hdd0/PS2ISO`; Cobra mounts PS2 images from the
internal drive only.

**HEN:** the **PS2 Classics Placeholder**, installed separately, and your games as `.BIN.ENC`.
These can sit on the internal drive or a USB drive. The next section explains both.

```
/dev_hdd0/PS2ISO/                       (.BIN.ENC can also go in /dev_usb000/PS2ISO/)
├── God of War II.iso
├── God of War II.iso.CONFIG                 ps2_netemu fixes, added automatically
├── Burnout 3 [SLUS-21050].BIN.ENC
├── Burnout 3 [SLUS-21050].BIN.ENC.CONFIG    optional ps2_netemu settings
└── covers/
    ├── SCUS-97481.png
    └── SLUS-21050.jpg
```

### HEN and the PS2 Classics Placeholder

HEN can't mount PS2 disc images, so on HEN PS3SX2 plays games the way Sony's PS2 Classics
store releases work:

- **PS2 Classics Placeholder** is Sony's PS2 Classics app, installed as title `PS2U10000`
  in `/dev_hdd0/game/PS2U10000`. It plays whatever encrypted image sits in its folder as
  `USRDIR/ISO.BIN.ENC`. When you press ✕ on a `.BIN.ENC` game, PS3SX2 copies it there (with
  its `.CONFIG`, if it has one); you then start the placeholder from the XMB.
- **`.BIN.ENC`** is a PS2 disc image encrypted for that app. Make it from a disc you own with
  a PC tool such as PS2 Classics GUI.
- **Why PS3SX2 doesn't include the placeholder:** a PS3 package installs into a single game
  folder, so it can't carry a second app, and the placeholder is made of Sony's signed files
  and license data, which an open-source project can't redistribute. Install it the way your
  HEN guide describes; PS3SX2 tells you when it's missing.
- On HEN, `.iso` games carry an amber **"ISO · not playable on HEN"** tag, so you know before
  pressing ✕.

### Game fixes (CONFIG)

`ps2_netemu` doesn't run every game perfectly: some freeze when you skip an intro video,
show a black screen, or need pressure-sensitive buttons. The PS3 scene has written fixes for
about 1,400 games as small CONFIG files, and PS3SX2 carries them in its package. When you
start an `.iso`, it puts the one matching the game's serial next to the image as
`<game>.iso.CONFIG`, where Cobra hands it to the emulator.

- A CONFIG you put there yourself is never replaced.
- If webMAN MOD's CONFIG database is installed, its configs are used for games ours don't cover.
- The configs come from [webMAN MOD](https://github.com/aldostools/webMAN-MOD/tree/master/_Projects_/updater/PS2CONFIG);
  [psdevwiki](https://www.psdevwiki.com/ps3/PS2_Custom_Configs) explains what they change.

Example: Pro Evolution Soccer 6 freezes after skipping its intro without the fix and plays
fine with it.

### Covers

PS3SX2 looks for a cover in this order:

1. `PS2ISO/covers/<SERIAL>.png` or `.jpg`, for example `covers/SLUS-21050.jpg`
2. An image with the same name as the game, next to it: `Burnout 3 [SLUS-21050].png`

A game without a cover gets a generated one: a colored case with its title, region and
serial. Images are shrunk to 256×360 on load.

**Downloading covers:** when the console is online, PS3SX2 fetches missing covers by itself
in the background as soon as it starts, by serial, from the
[xlenore/ps2-covers](https://github.com/xlenore/ps2-covers) archive that PCSX2 users rely on.
You can keep browsing meanwhile: each cover appears as it arrives, and the header shows
"Covers 3/5". They're saved as `covers/<SERIAL>.jpg` next to the games, so they show up
offline from then on. Games the archive doesn't have keep their generated cover. □ runs the
same download in the foreground, with a progress bar and a summary, e.g. to retry.

- Box art is copyrighted, so neither this repository nor the package contains any; the
  console downloads it when you ask.
- To use another archive, put its URL in `/dev_hdd0/PS2ISO/covers_url.txt` with `%s` where
  the serial goes, e.g. `https://example.com/ps2/%s.jpg`.
- Connections are encrypted and verified against Mozilla's list of certificate authorities,
  so the console's date and time need to be right.

### File names

A tidy title is built from the file name. Serial tags are removed and `_` becomes a
space, so all of these show up as "Burnout 3":

```
Burnout 3.iso
Burnout 3 [SLUS-21050].BIN.ENC
SLUS_210.50.Burnout_3.iso        Open PS2 Loader style
```

For `.BIN.ENC` files the serial can only come from the file name, so keep it in there
if you want covers by serial.

## Controls

| Button | Action |
|---|---|
| Left / Right | Previous / next game (hold to scroll) |
| L1 / R1 | Jump 5 games |
| ✕ | Start the selected game |
| △ | Rescan the folders |
| □ | Download missing covers now (they also download on their own) |
| ○ | Exit, or cancel a copy or download |

## Building

The unit tests run on any machine with a C compiler, no PS3 toolchain needed:

```bash
make test
```

The PS3 package needs [PSL1GHT](https://github.com/ps3dev/PSL1GHT) (an open source
PS3 SDK), [ps3libraries](https://github.com/bucanero/ps3libraries) for FreeType, and
[Tiny3D](https://github.com/wargio/tiny3D) with its libfont:

```bash
export PS3DEV=/usr/local/ps3dev
export PSL1GHT=$PS3DEV
make pkg
```

That produces `ps3sx2.pkg`. Every push to GitHub also builds it: open the
**Actions** tab, pick a run and download the `ps3sx2-…` artifact.

### Source layout

| Path | What's in it |
|---|---|
| `source/main.c` | Main loop, controller input, launch flow |
| `source/gfx.c` | Tiny3D rendering: carousel, text, cover textures |
| `source/library.c` | Folder scanning, titles from file names |
| `source/iso9660.c` | Reads `SYSTEM.CNF` from cooked and raw disc images |
| `source/serial.c` | Finds serials in `SYSTEM.CNF` and file names, region lookup |
| `source/launcher.c` | PS2 Classics copy and the rules for mounting ISOs |
| `source/cobra.c` | Cobra syscalls that mount a PS2 ISO (adapted from IRISMAN) |
| `source/coverflow.c` | Carousel layout and animation math |
| `source/canvas.c` | Small software painter for generated covers |
| `source/coverdl.c`, `source/net.c`, `source/autocovers.c` | Missing-cover downloads, in the background or on demand; HTTPS through curl and mbedTLS |
| `source/covers.c` | Where to look for each game's cover |
| `source/lang.c`, `source/text.c` | English and Turkish strings, UTF-8 to font encoding |
| `tests/` | Host unit tests for everything above except `main.c`, `gfx.c`, `cobra.c`, `net.c` and `autocovers.c` |

Only `main.c`, `gfx.c`, `cobra.c`, `net.c` and `autocovers.c` touch PS3 APIs. Keep new logic in a separate module
so it can be tested with `make test`.

## Roadmap

- [x] Run in RPCS3 and tune the carousel against the design
- [ ] Run on a real console and start a game end to end
- [x] Start plain `.iso` games on CFW through Cobra (needs a test on a console)
- [ ] Mount `.iso` games from USB drives
- [ ] Start the placeholder directly instead of going back to the XMB
- [ ] Split `.iso.0` / `.iso.1` images on FAT32 USB drives
- [x] Bundled `ps2_netemu` CONFIG database (community configs)
- [ ] Cover reflections, analog stick, settings screen
- [ ] More languages

## Contributing

Issues and pull requests are welcome. Before opening a pull request:

- run `make test` and make sure it passes
- put PS3-independent logic in its own module with tests
- to add a language, add it to `lang_id` and fill in its column in `source/lang.c`.
  The tests check that every string can be drawn by the font.

## Legal

PS3SX2 is not affiliated with or endorsed by Sony Interactive Entertainment.
"PlayStation", "PS2" and "PS3" are trademarks of their owners.

This repository has no games, firmware, BIOS, emulator binaries or cover art,
and none should be added to it. Use games from discs you own.

## License

[GNU GPL v3](LICENSE).

## Thanks

- [PSL1GHT](https://github.com/ps3dev/PSL1GHT) and the ps3dev community
- [Tiny3D](https://github.com/wargio/tiny3D) by Hermes, for rendering and fonts
- [webMAN MOD](https://github.com/aldostools/webMAN-MOD) by aldostools, whose source
  documents how PS2 games are mounted
- [IRISMAN](https://github.com/aldostools/IRISMAN) by aldostools and contributors (GPL-3.0),
  whose Cobra code `source/cobra.c` is adapted from
- Everyone who wrote and collected the `ps2_netemu` custom configs shipped in
  `pkgfiles/USRDIR/ps2config`
- [Apollo Save Tool](https://github.com/bucanero/apollo-ps3) by bucanero, whose CI setup and
  network code this project's are based on
- [xlenore/ps2-covers](https://github.com/xlenore/ps2-covers), the cover archive downloads
  come from
- Mozilla's root certificates as packaged by [curl](https://curl.se/docs/caextract.html)
  (`pkgfiles/USRDIR/cacert.pem`, MPL 2.0)
