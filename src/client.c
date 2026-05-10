#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include "platform.h"
#include "rom.h"
#include "menu_state.h"

#include "SDL/SDL.h"
#include "SDL/SDL_ttf.h"

#define ITEM_HEIGHT      40
#define MAX_VISIBLE_ITEMS 10
#define HEADER_HEIGHT    30
#define FRAME_MS         16   /* ~60 fps */

/* Onion OS / Miyoo Mini Plus key mapping */
#define KEY_A      SDLK_SPACE
#define KEY_B      SDLK_LCTRL
#define KEY_START  SDLK_RETURN

/* ── cleanup / init ─────────────────────────────────────────────── */

void cleanup_menu(MenuState* state) {
    free(state->server_url);
    free(state->username);
    free(state->password);
    if (state->platforms) free_platform_list(state->platforms, state->platform_count);
    if (state->roms)      free_rom_list(state->roms, state->rom_count);
    if (state->font)      TTF_CloseFont(state->font);
    /* state->screen is owned by SDL; freed by SDL_Quit() */
    if (state->renderer)  SDL_FreeSurface(state->renderer);
    TTF_Quit();
    SDL_Quit();
}

int init_menu(MenuState* state) {
    memset(state, 0, sizeof(MenuState));

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return -1;
    }

    const SDL_VideoInfo* info = SDL_GetVideoInfo();
    if (!info) {
        fprintf(stderr, "SDL_GetVideoInfo failed\n");
        TTF_Quit();
        SDL_Quit();
        return -1;
    }
    state->display_width  = info->current_w;
    state->display_height = info->current_h;

    state->font = TTF_OpenFont("/mnt/SDCARD/App/RomM/fonts/DejaVuSans.ttf", 16);
    if (!state->font) {
        fprintf(stderr, "TTF_OpenFont failed: %s\n", TTF_GetError());
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    state->screen = SDL_SetVideoMode(state->display_width, state->display_height, 32, SDL_HWSURFACE);
    if (!state->screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        TTF_CloseFont(state->font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    state->renderer = SDL_CreateRGBSurface(SDL_SWSURFACE,
                          state->display_width, state->display_height, 32, 0, 0, 0, 0);
    if (!state->renderer) {
        fprintf(stderr, "SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
        TTF_CloseFont(state->font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    SDL_FillRect(state->screen, NULL, SDL_MapRGB(state->screen->format, 0, 0, 0));
    SDL_Flip(state->screen);

    state->last_tick_count = SDL_GetTicks();
    state->cur_tick_count  = state->last_tick_count;

    state->server_url = malloc(256);
    state->username   = malloc(256);
    state->password   = malloc(256);
    if (!state->server_url || !state->username || !state->password) {
        fprintf(stderr, "malloc failed for credential buffers\n");
        free(state->server_url);
        free(state->username);
        free(state->password);
        SDL_FreeSurface(state->renderer);
        TTF_CloseFont(state->font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    return 0;
}

/* ── rendering helpers ──────────────────────────────────────────── */

static void draw_text(MenuState* state, const char* text, int x, int y, SDL_Color color) {
    SDL_Surface* surf = TTF_RenderText_Solid(state->font, text, color);
    if (!surf) return;
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_BlitSurface(surf, NULL, state->renderer, &dst);
    SDL_FreeSurface(surf);
}

static void flip(MenuState* state) {
    SDL_BlitSurface(state->renderer, NULL, state->screen, NULL);
    SDL_Flip(state->screen);
}

static void render_message(MenuState* state, const char* msg) {
    SDL_FillRect(state->renderer, NULL, SDL_MapRGB(state->renderer->format, 0, 0, 0));
    SDL_Color white = {255, 255, 255, 0};
    draw_text(state, msg, 20, state->display_height / 2 - 8, white);
    flip(state);
}

static void render_list(MenuState* state,
                        const char* title,
                        int item_count,
                        const char* (*get_label)(MenuState*, int)) {
    SDL_FillRect(state->renderer, NULL, SDL_MapRGB(state->renderer->format, 0, 0, 0));

    SDL_Color white    = {255, 255, 255, 0};
    SDL_Color yellow   = {255, 255,   0, 0};
    SDL_Color dimwhite = {160, 160, 160, 0};

    /* header */
    draw_text(state, title, 10, 6, yellow);

    /* divider line */
    SDL_Rect divider = {0, HEADER_HEIGHT - 2, state->display_width, 1};
    SDL_FillRect(state->renderer, &divider, SDL_MapRGB(state->renderer->format, 80, 80, 80));

    for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
        int idx = i + state->scroll_offset;
        if (idx >= item_count) break;

        const char* label = get_label(state, idx);
        if (!label) continue;

        SDL_Color color = (idx == state->selected_index) ? yellow : white;
        int y = HEADER_HEIGHT + i * ITEM_HEIGHT + 6;

        if (idx == state->selected_index) {
            SDL_Rect highlight = {0, HEADER_HEIGHT + i * ITEM_HEIGHT,
                                  state->display_width, ITEM_HEIGHT};
            SDL_FillRect(state->renderer, &highlight,
                         SDL_MapRGB(state->renderer->format, 40, 40, 80));
        }

        draw_text(state, label, 20, y, color);
    }

    /* scroll hint */
    if (item_count > MAX_VISIBLE_ITEMS) {
        char hint[32];
        snprintf(hint, sizeof(hint), "%d/%d", state->selected_index + 1, item_count);
        draw_text(state, hint, state->display_width - 60,
                  state->display_height - 20, dimwhite);
    }

    flip(state);
}

/* label callbacks */
static const char* platform_label(MenuState* state, int idx) {
    return state->platforms[idx].name;
}

static const char* rom_label(MenuState* state, int idx) {
    const char* name = state->roms[idx].name;
    return (name && name[0]) ? name : state->roms[idx].file_name;
}

/* ── config ─────────────────────────────────────────────────────── */

int read_config(MenuState* state, const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Failed to open config: %s\n", filename);
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if      (strncmp(line, "server_url=", 11) == 0) snprintf(state->server_url, 256, "%s", line + 11);
        else if (strncmp(line, "username=",    9) == 0) snprintf(state->username,   256, "%s", line +  9);
        else if (strncmp(line, "password=",    9) == 0) snprintf(state->password,   256, "%s", line +  9);
    }

    fclose(f);
    return 0;
}

/* ── navigation helpers ─────────────────────────────────────────── */

static void nav_up(MenuState* state) {
    if (state->selected_index > 0) {
        state->selected_index--;
        if (state->selected_index < state->scroll_offset)
            state->scroll_offset--;
    }
}

static void nav_down(MenuState* state, int count) {
    if (state->selected_index < count - 1) {
        state->selected_index++;
        if (state->selected_index >= state->scroll_offset + MAX_VISIBLE_ITEMS)
            state->scroll_offset++;
    }
}

static void reset_cursor(MenuState* state) {
    state->selected_index = 0;
    state->scroll_offset  = 0;
}

/* ── download helper ─────────────────────────────────────────────── */

static void ensure_dir(const char* path) {
    mkdir(path, 0755);
}

static int do_download(MenuState* state, RomMRom* rom) {
    const char* slug = rom->platform_slug ? rom->platform_slug : "unknown";
    const char* file = rom->file_name     ? rom->file_name     : "rom";

    char dir[512];
    snprintf(dir, sizeof(dir), "/mnt/SDCARD/Roms/%s", slug);
    ensure_dir("/mnt/SDCARD/Roms");
    ensure_dir(dir);

    char dest[768];
    snprintf(dest, sizeof(dest), "%s/%s", dir, file);

    char url[1024];
    snprintf(url, sizeof(url), "%s/api/roms/%d/content/%s",
             state->server_url, rom->id, file);

    return download_rom(url, state->username, state->password, dest);
}

/* ── main ───────────────────────────────────────────────────────── */

int main(void) {
    MenuState state = {0};

    if (init_menu(&state) < 0) {
        fprintf(stderr, "Failed to initialize\n");
        return 1;
    }

    if (read_config(&state, "/mnt/SDCARD/App/RomM/config.txt") < 0) {
        cleanup_menu(&state);
        return 1;
    }

    render_message(&state, "Loading platforms...");

    if (fetch_platform_list(state.server_url, state.username, state.password,
                            &state.platforms, &state.platform_count) < 0) {
        render_message(&state, "Error: could not reach RomM server");
        SDL_Delay(2000);
        cleanup_menu(&state);
        return 1;
    }

    bool quit = false;
    SDL_Event event;

    while (!quit) {
        /* event handling */
        while (SDL_PollEvent(&event)) {
            if (event.type != SDL_KEYDOWN) continue;
            SDLKey key = event.key.keysym.sym;

            if (key == KEY_START) {
                quit = true;
                break;
            }

            if (state.current_screen == SCREEN_PLATFORMS) {
                if      (key == SDLK_UP)   nav_up(&state);
                else if (key == SDLK_DOWN) nav_down(&state, state.platform_count);
                else if (key == KEY_A && state.platform_count > 0) {
                    state.active_platform_idx = state.selected_index;

                    char msg[128];
                    snprintf(msg, sizeof(msg), "Loading %s...",
                             state.platforms[state.selected_index].name);
                    render_message(&state, msg);

                    if (fetch_rom_list(state.server_url, state.username, state.password,
                                       state.platforms[state.selected_index].id,
                                       &state.roms, &state.rom_count) == 0) {
                        state.current_screen = SCREEN_ROMS;
                        reset_cursor(&state);
                    } else {
                        render_message(&state, "Error loading ROMs");
                        SDL_Delay(1500);
                    }
                }

            } else if (state.current_screen == SCREEN_ROMS) {
                if      (key == SDLK_UP)   nav_up(&state);
                else if (key == SDLK_DOWN) nav_down(&state, state.rom_count);
                else if (key == KEY_B) {
                    /* back to platform list */
                    free_rom_list(state.roms, state.rom_count);
                    state.roms      = NULL;
                    state.rom_count = 0;
                    state.current_screen  = SCREEN_PLATFORMS;
                    state.selected_index  = state.active_platform_idx;
                    state.scroll_offset   = state.active_platform_idx >= MAX_VISIBLE_ITEMS
                                            ? state.active_platform_idx - MAX_VISIBLE_ITEMS + 1
                                            : 0;
                } else if (key == KEY_A && state.rom_count > 0) {
                    RomMRom* rom = &state.roms[state.selected_index];
                    const char* display = (rom->name && rom->name[0]) ? rom->name : rom->file_name;

                    char msg[256];
                    snprintf(msg, sizeof(msg), "Downloading %s...", display ? display : "ROM");
                    render_message(&state, msg);

                    if (do_download(&state, rom) == 0) {
                        render_message(&state, "Download complete!");
                    } else {
                        render_message(&state, "Download failed");
                    }
                    SDL_Delay(1500);
                }
            }
        }

        /* render at ~60 fps */
        state.cur_tick_count = SDL_GetTicks();
        if (state.cur_tick_count - state.last_tick_count >= FRAME_MS) {
            if (state.current_screen == SCREEN_PLATFORMS) {
                char title[128];
                snprintf(title, sizeof(title), "Platforms (%d)", state.platform_count);
                render_list(&state, title, state.platform_count, platform_label);
            } else {
                char title[128];
                snprintf(title, sizeof(title), "%s (%d ROMs)",
                         state.platforms[state.active_platform_idx].name,
                         state.rom_count);
                render_list(&state, title, state.rom_count, rom_label);
            }
            state.last_tick_count = state.cur_tick_count;
        } else {
            SDL_Delay(1);
        }
    }

    cleanup_menu(&state);
    return 0;
}
