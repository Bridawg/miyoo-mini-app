#ifndef ROMM_THEME_H
#define ROMM_THEME_H

#include "SDL/SDL.h"

#define THEME_PATH_MAX 512

typedef struct {
    char      font[THEME_PATH_MAX];
    int       size;
    SDL_Color color;
} ThemeStyle;

typedef struct {
    ThemeStyle title;
    ThemeStyle list;
    SDL_Color  selected_bg;   /* highlight rect behind selected item */
    SDL_Color  selected_text; /* text color for selected item        */
} Theme;

/* Reads /mnt/SDCARD/miyoo/app/config.json; falls back to Onion
   defaults when the file is absent, invalid, or fields are missing. */
void theme_load(Theme* t);

#endif /* ROMM_THEME_H */
