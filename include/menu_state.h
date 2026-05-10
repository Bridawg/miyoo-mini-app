#ifndef ROMM_MENU_STATE_H
#define ROMM_MENU_STATE_H

#include "SDL/SDL.h"
#include "SDL/SDL_ttf.h"

#include "platform.h"
#include "rom.h"

typedef enum {
    SCREEN_PLATFORMS = 0,
    SCREEN_ROMS,
} Screen;

typedef struct {
    int display_width;
    int display_height;
    SDL_Surface* screen;
    SDL_Surface* renderer;
    TTF_Font* font;
    /* Platform list */
    RomMPlatform* platforms;
    int platform_count;
    int active_platform_idx;   /* which platform we're browsing ROMs for */
    /* ROM list */
    RomMRom* roms;
    int rom_count;
    /* UI state */
    Screen current_screen;
    int selected_index;
    int scroll_offset;
    Uint32 last_tick_count;
    Uint32 cur_tick_count;
    /* Connection */
    char* server_url;
    char* username;
    char* password;
} MenuState;

#endif // ROMM_MENU_STATE_H
