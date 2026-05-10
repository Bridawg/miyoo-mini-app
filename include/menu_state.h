#ifndef ROMM_MENU_STATE_H
#define ROMM_MENU_STATE_H

#include "SDL/SDL.h"
#include "SDL/SDL_ttf.h"

#include "platform.h"
#include "rom.h"
#include "theme.h"

typedef enum {
    SCREEN_PLATFORMS = 0,
    SCREEN_ROMS,
} Screen;

typedef struct {
    /* Display */
    int display_width;
    int display_height;
    SDL_Surface* screen;
    SDL_Surface* renderer;

    /* Theme */
    Theme        theme;
    TTF_Font*    title_font;
    TTF_Font*    list_font;

    /* Skin images (NULL if the file is absent) */
    SDL_Surface* skin_bg;       /* bg-list-l.png   — full-screen backdrop   */
    SDL_Surface* skin_topbar;   /* miyoo-topbar.png — top bar overlay        */
    SDL_Surface* skin_icon_a;   /* icon-A-54.png                             */
    SDL_Surface* skin_icon_b;   /* icon-B-54.png                             */

    /* Platform list */
    RomMPlatform* platforms;
    int           platform_count;
    int           active_platform_idx;

    /* ROM list */
    RomMRom* roms;
    int      rom_count;

    /* Cover art for the currently selected ROM */
    SDL_Surface* cover;
    int          cover_rom_id;      /* -1 = no cover loaded          */
    Uint32       selection_tick;    /* time the selection last moved */

    /* UI state */
    Screen current_screen;
    int    selected_index;
    int    scroll_offset;
    Uint32 last_tick_count;
    Uint32 cur_tick_count;

    /* Connection */
    char* server_url;
    char* username;
    char* password;
} MenuState;

#endif /* ROMM_MENU_STATE_H */
