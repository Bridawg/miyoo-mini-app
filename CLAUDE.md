# RomM Miyoo Mini App

Native C/SDL 1.2 app for the **Miyoo Mini Plus** (Onion OS) that browses and
downloads ROMs from a self-hosted RomM game-library server.

## Build

Requires **Docker** and, on first run, a **mounted Onion SD card** (two device
libraries are taken from it and then cached in `vendor/`).

```bash
# One-time: assemble the sysroot (~3 GB image pull on first run)
./scripts/setup-sysroot.sh
# ...or point at the card explicitly:
SDCARD=/media/you/Onion ./scripts/setup-sysroot.sh

# Build + package for SD card → romm-miyoo.zip
./scripts/build.sh
```

**Do not build with the host distro's `gcc-arm-linux-gnueabihf`.** The device
runs glibc 2.28; a modern distro's armhf toolchain (Ubuntu 24.04 "noble" and
newer) links `__libc_start_main@GLIBC_2.34` and `__stat64_time64@GLIBC_2.34`,
and the binary dies at exec with ``version `GLIBC_2.34' not found`` before
`main()` runs. `scripts/build.sh` prints the required GLIBC versions after
every build — they must stay ≤ 2.28. Cross-check against a known-good device
binary any time this is in doubt:

```bash
readelf -V --wide /mnt/SDCARD/.tmp_update/bin/curl | grep -oE 'GLIBC_[0-9.]+' | sort -uV
```

## Deploy

```bash
unzip -o romm-miyoo.zip -d /path/to/sdcard
```

App lives at `/mnt/SDCARD/App/RomM/` on device. ROMs download to
`/mnt/SDCARD/Roms/{platform_slug}/`. Cover art cached at
`/mnt/SDCARD/App/RomM/covers/{slug}/{rom_id}.jpg`.

## Target hardware

| | |
|---|---|
| Device | Miyoo Mini Plus (ARM Cortex-A7, armhf) |
| OS | Onion OS (buildroot, 640×480) |
| Display | 640×480, SDL 1.2 via sdl12-compat |
| Libraries | SDL_ttf, SDL_image, libjson-c, libcurl |

## RomM API

- `GET /api/platforms` → JSON array
- `GET /api/roms?platform_id={id}&limit=500` → `{items:[...]}`
- `GET /api/roms/{id}/content/{file_name}` → ROM download
- Auth: HTTP Basic via libcurl `CURLOPT_USERPWD` (not shell/popen)

## UI layout

```
TOPBAR   50px  — miyoo-topbar.png skin, title font
CONTENT 380px  — ITEM_H=34, MAX_ITEMS=11
HINTBAR  50px  — black bar, A/B icon hints
```

ROM screen: LIST_W=420px left panel + COVER_W=220px right (cover art).
Cover loads after 300 ms debounce (COVER_DELAY) to avoid scroll thrashing.

## Onion OS theme

Theme loaded from `/mnt/SDCARD/miyoo/app/config.json` at startup.
Skin assets from `/mnt/SDCARD/miyoo/app/skin/` (all optional/graceful).
Fallback font: `/mnt/SDCARD/App/RomM/fonts/DejaVuSans.ttf`.

## Button mapping (SDL keysyms)

| Button | SDLK |
|--------|------|
| A | `SDLK_SPACE` |
| B | `SDLK_LCTRL` |
| Start | `SDLK_RETURN` |
| Select | `SDLK_ESCAPE` |

## Settings screen

Opened via **Select** on the platform list. Two modes:
- **Field selection** — Up/Down, A to enter keyboard, Start to save
- **Keyboard mode** — d-pad navigates 8-row character grid, A types,
  B deletes, Start confirms field, Select cancels

No built-in Onion OS system keyboard exists — the grid is self-contained, and
it is the only way to type on the device. It therefore covers the **full
printable ASCII set**; anything dropped from `kbd_rows` becomes a password
nobody can enter. Config saved to `/mnt/SDCARD/App/RomM/config.txt`. First run
(no config) opens settings automatically.

## Key implementation notes

- Never call `SDL_FreeSurface(state->screen)` — SDL owns that surface
- Use `--allow-shlib-undefined` in LDFLAGS for transitive deps (OpenSSL
  etc.) already present on device
- Toolchain image: `ghcr.io/onionui/miyoomini-toolchain` — gcc 8.3, glibc
  2.28. Its SDL 1.2.0.11.4 / SDL_ttf 2.0.10.1 / SDL_image 1.2.0.8.4 are the
  same builds shipped in the card's `miyoo/lib`
- The image has **no json-c and no libcurl** (it ships cJSON), so both come
  off the SD card — `App/pico/lib/libjson-c.so.5` and
  `.tmp_update/lib/libcurl.so.4` — with upstream headers for the matching
  versions (json-c 0.15, curl 8.1.0)
- The image has **no `curl` binary**, so headers are fetched host-side
- The image exports `CROSS_COMPILE` only for *login* shells — pin it
  explicitly when invoking `make` non-interactively
- Toolchain keeps libs flat in `usr/lib`; the Makefile expects
  `usr/lib/arm-linux-gnueabihf`, so setup mirrors them as symlinks

## Runtime library paths

Onion exports:

```
LD_LIBRARY_PATH=/lib:/config/lib:/mnt/SDCARD/miyoo/lib:/mnt/SDCARD/.tmp_update/lib:...
```

| Library | Found in |
|---|---|
| `libSDL-1.2`, `libSDL_ttf`, `libSDL_image` | `miyoo/lib` |
| `libcurl.so.4` | `.tmp_update/lib` |
| `libc.so.6`, `libpthread.so.0` | device firmware `/lib` |
| `libjson-c.so.5` | **nowhere on that path** — shipped in `App/RomM/lib/` |

`libjson-c.so.5` exists on the card only inside `App/pico/lib`, which Onion
never searches, so `launch.sh` prepends `$APPDIR/lib` and the `package` target
bundles the library. Without that the app fails to load.
