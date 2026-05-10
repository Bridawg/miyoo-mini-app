# RomM Miyoo Mini App

Native C/SDL 1.2 app for the **Miyoo Mini Plus** (Onion OS) that browses and
downloads ROMs from a self-hosted RomM game-library server.

## Build

```bash
# One-time sysroot setup (ARM cross-compile dependencies)
./scripts/setup-sysroot.sh

# Build
make CROSS_COMPILE=arm-linux-gnueabihf-

# Build + package for SD card
make CROSS_COMPILE=arm-linux-gnueabihf- package
# → produces romm-miyoo.zip; extract to SD card root
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
- **Keyboard mode** — d-pad navigates 6-row character grid, A types,
  B deletes, Start confirms field, Select cancels

No built-in Onion OS system keyboard exists — the grid is self-contained.
Config saved to `/mnt/SDCARD/App/RomM/config.txt`. First run (no config)
opens settings automatically.

## Key implementation notes

- Never call `SDL_FreeSurface(state->screen)` — SDL owns that surface
- Use `--allow-shlib-undefined` in LDFLAGS for transitive deps (OpenSSL
  etc.) already present on device
- `sysroot/lib` must symlink to `usr/lib` (usrmerge — libc.so linker
  script uses absolute `/lib` path)
- SDL 1.2.15 headers pulled from source release; sdl12-compat-dev has none
- armhf packages: use `ports.ubuntu.com`, `apt-get download` + `dpkg -x`
  (not `apt install`) to avoid host dependency conflicts
- SDL_image package name is `libsdl-image1.2` (not `libsdl-image1.2-0`)
