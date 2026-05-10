#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "platform.h"
#include "rom.h"
#include "theme.h"
#include "cover.h"
#include "menu_state.h"

#include "SDL/SDL.h"
#include "SDL/SDL_ttf.h"
#include "SDL/SDL_image.h"

/* ── Layout constants (640×480) ─────────────────────────────────── */
#define SCREEN_W     640
#define SCREEN_H     480
#define TOPBAR_H      50
#define HINTBAR_H     50
#define CONTENT_H    (SCREEN_H - TOPBAR_H - HINTBAR_H)   /* 380 */
#define ITEM_H        34
#define MAX_ITEMS    (CONTENT_H / ITEM_H)                 /* 11  */
#define COVER_W      220   /* right-panel width in ROM screen       */
#define LIST_W       (SCREEN_W - COVER_W)                 /* 420  */
#define COVER_MAX_W  200
#define COVER_MAX_H  266   /* ~3:4 portrait box                     */
#define FRAME_MS      16   /* ~60 fps                               */
#define COVER_DELAY  300   /* ms stable before cover load triggers  */

/* Onion OS skin paths */
#define SKIN_DIR    "/mnt/SDCARD/miyoo/app/skin/"
#define SKIN_BG     SKIN_DIR "bg-list-l.png"
#define SKIN_TOPBAR SKIN_DIR "miyoo-topbar.png"
#define SKIN_ICON_A SKIN_DIR "icon-A-54.png"
#define SKIN_ICON_B SKIN_DIR "icon-B-54.png"

/* Onion OS button mapping */
#define KEY_A     SDLK_SPACE
#define KEY_B     SDLK_LCTRL
#define KEY_START SDLK_RETURN

/* ── Helpers ────────────────────────────────────────────────────── */

static SDL_Surface* try_img_load(const char* path) {
    SDL_Surface* s = IMG_Load(path);
    return s; /* NULL is fine — skin is optional */
}

static TTF_Font* try_open_font(const char* primary, int size) {
    static const char* fallbacks[] = {
        "/mnt/SDCARD/miyoo/app/Exo-2-Bold-Italic_Universal.ttf",
        "/mnt/SDCARD/miyoo/app/wqy-microhei.ttc",
        "/mnt/SDCARD/App/RomM/fonts/DejaVuSans.ttf",
        NULL
    };
    TTF_Font* f = TTF_OpenFont(primary, size);
    if (f) return f;
    for (int i = 0; fallbacks[i]; i++) {
        f = TTF_OpenFont(fallbacks[i], size);
        if (f) return f;
    }
    return NULL;
}

/* ── Cleanup / init ─────────────────────────────────────────────── */

void cleanup_menu(MenuState* s) {
    free(s->server_url);
    free(s->username);
    free(s->password);
    if (s->platforms)   free_platform_list(s->platforms, s->platform_count);
    if (s->roms)        free_rom_list(s->roms, s->rom_count);
    if (s->cover)       SDL_FreeSurface(s->cover);
    if (s->title_font)  TTF_CloseFont(s->title_font);
    if (s->list_font)   TTF_CloseFont(s->list_font);
    if (s->skin_bg)     SDL_FreeSurface(s->skin_bg);
    if (s->skin_topbar) SDL_FreeSurface(s->skin_topbar);
    if (s->skin_icon_a) SDL_FreeSurface(s->skin_icon_a);
    if (s->skin_icon_b) SDL_FreeSurface(s->skin_icon_b);
    /* s->screen is owned by SDL */
    if (s->renderer)    SDL_FreeSurface(s->renderer);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

int init_menu(MenuState* s) {
    memset(s, 0, sizeof(MenuState));
    s->cover_rom_id = -1;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return -1;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); SDL_Quit(); return -1;
    }
    if (IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());
    }

    const SDL_VideoInfo* info = SDL_GetVideoInfo();
    if (!info) {
        fprintf(stderr, "SDL_GetVideoInfo failed\n");
        TTF_Quit(); SDL_Quit(); return -1;
    }
    s->display_width  = info->current_w;
    s->display_height = info->current_h;

    /* Load theme then fonts */
    theme_load(&s->theme);
    s->title_font = try_open_font(s->theme.title.font, s->theme.title.size);
    s->list_font  = try_open_font(s->theme.list.font,  s->theme.list.size);
    if (!s->title_font || !s->list_font) {
        fprintf(stderr, "Could not load any font\n");
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return -1;
    }

    s->screen = SDL_SetVideoMode(s->display_width, s->display_height, 32, SDL_HWSURFACE);
    if (!s->screen) {
        fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError());
        TTF_CloseFont(s->title_font); TTF_CloseFont(s->list_font);
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return -1;
    }

    s->renderer = SDL_CreateRGBSurface(SDL_SWSURFACE,
                      s->display_width, s->display_height, 32, 0, 0, 0, 0);
    if (!s->renderer) {
        fprintf(stderr, "SDL_CreateRGBSurface: %s\n", SDL_GetError());
        TTF_CloseFont(s->title_font); TTF_CloseFont(s->list_font);
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return -1;
    }

    /* Load skin assets (failures are non-fatal) */
    s->skin_bg     = try_img_load(SKIN_BG);
    s->skin_topbar = try_img_load(SKIN_TOPBAR);
    s->skin_icon_a = try_img_load(SKIN_ICON_A);
    s->skin_icon_b = try_img_load(SKIN_ICON_B);

    SDL_FillRect(s->screen, NULL, SDL_MapRGB(s->screen->format, 0, 0, 0));
    SDL_Flip(s->screen);

    s->last_tick_count = SDL_GetTicks();
    s->cur_tick_count  = s->last_tick_count;

    s->server_url = malloc(256);
    s->username   = malloc(256);
    s->password   = malloc(256);
    if (!s->server_url || !s->username || !s->password) {
        free(s->server_url); free(s->username); free(s->password);
        SDL_FreeSurface(s->renderer);
        TTF_CloseFont(s->title_font); TTF_CloseFont(s->list_font);
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return -1;
    }
    return 0;
}

/* ── Config ─────────────────────────────────────────────────────── */

int read_config(MenuState* s, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open config: %s\n", path); return -1; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if      (!strncmp(line, "server_url=", 11)) snprintf(s->server_url, 256, "%s", line + 11);
        else if (!strncmp(line, "username=",    9)) snprintf(s->username,   256, "%s", line +  9);
        else if (!strncmp(line, "password=",    9)) snprintf(s->password,   256, "%s", line +  9);
    }
    fclose(f); return 0;
}

/* ── Drawing primitives ─────────────────────────────────────────── */

static void draw_text(MenuState* s, TTF_Font* font,
                      const char* text, int x, int y, SDL_Color col) {
    SDL_Surface* surf = TTF_RenderText_Solid(font, text[0] ? text : " ", col);
    if (!surf) return;
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_BlitSurface(surf, NULL, s->renderer, &dst);
    SDL_FreeSurface(surf);
}

static void draw_text_clipped(MenuState* s, TTF_Font* font,
                              const char* text, int x, int y,
                              int max_w, SDL_Color col) {
    /* Render once to check width; truncate with ellipsis if needed */
    int tw, th; TTF_SizeText(font, text, &tw, &th);
    if (tw <= max_w) { draw_text(s, font, text, x, y, col); return; }

    /* Binary-search the truncation point */
    char buf[512];
    strncpy(buf, text, sizeof(buf) - 4);
    int lo = 0, hi = (int)strlen(buf);
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        char tmp[512]; strncpy(tmp, buf, mid); tmp[mid] = 0;
        strcat(tmp, "...");
        TTF_SizeText(font, tmp, &tw, &th);
        if (tw <= max_w) lo = mid; else hi = mid;
    }
    strncpy(buf, text, lo); buf[lo] = 0; strcat(buf, "...");
    draw_text(s, font, buf, x, y, col);
}

static void flip(MenuState* s) {
    SDL_BlitSurface(s->renderer, NULL, s->screen, NULL);
    SDL_Flip(s->screen);
}

/* ── Render helpers ─────────────────────────────────────────────── */

static void draw_background(MenuState* s) {
    if (s->skin_bg) {
        SDL_BlitSurface(s->skin_bg, NULL, s->renderer, NULL);
    } else {
        SDL_FillRect(s->renderer, NULL,
                     SDL_MapRGB(s->renderer->format, 20, 20, 30));
    }
}

static void draw_topbar(MenuState* s, const char* title) {
    if (s->skin_topbar) {
        SDL_Rect dst = {0, 0, s->display_width, TOPBAR_H};
        SDL_BlitSurface(s->skin_topbar, NULL, s->renderer, &dst);
    } else {
        SDL_Rect bar = {0, 0, s->display_width, TOPBAR_H};
        SDL_FillRect(s->renderer, &bar, SDL_MapRGB(s->renderer->format, 0, 0, 0));
    }
    int ty = (TOPBAR_H - s->theme.title.size) / 2;
    draw_text(s, s->title_font, title, 16, ty, s->theme.title.color);
}

static void draw_hintbar(MenuState* s, const char* a_label, const char* b_label) {
    int y = SCREEN_H - HINTBAR_H;
    SDL_Rect bar = {0, y, s->display_width, HINTBAR_H};
    SDL_FillRect(s->renderer, &bar, SDL_MapRGB(s->renderer->format, 0, 0, 0));

    SDL_Color white = {255, 255, 255, 0};
    int icon_y = y + (HINTBAR_H - 24) / 2;
    int cx = 16;

    if (s->skin_icon_a) {
        SDL_Rect src = {0, 0, 24, 24};  /* use 24×24 crop of 54×54 icon */
        SDL_Rect dst = {cx, icon_y, 24, 24};
        SDL_BlitSurface(s->skin_icon_a, &src, s->renderer, &dst);
        cx += 28;
    }
    draw_text(s, s->list_font, a_label, cx, icon_y, white);
    cx += (int)strlen(a_label) * 10 + 24;

    if (b_label && s->skin_icon_b) {
        SDL_Rect src = {0, 0, 24, 24};
        SDL_Rect dst = {cx, icon_y, 24, 24};
        SDL_BlitSurface(s->skin_icon_b, &src, s->renderer, &dst);
        cx += 28;
        draw_text(s, s->list_font, b_label, cx, icon_y, white);
    }
}

static void draw_list(MenuState* s, int list_w, int count,
                      const char* (*label)(MenuState*, int)) {
    SDL_Color dim  = {180, 180, 180, 0};

    for (int i = 0; i < MAX_ITEMS; i++) {
        int idx = i + s->scroll_offset;
        if (idx >= count) break;

        int iy = TOPBAR_H + i * ITEM_H;
        bool sel = (idx == s->selected_index);

        if (sel) {
            SDL_Rect hi = {0, iy, list_w, ITEM_H};
            SDL_FillRect(s->renderer, &hi,
                SDL_MapRGB(s->renderer->format,
                    s->theme.selected_bg.r,
                    s->theme.selected_bg.g,
                    s->theme.selected_bg.b));
        }

        const char* text = label(s, idx);
        SDL_Color col = sel ? s->theme.selected_text
                            : (idx % 2 == 0 ? s->theme.list.color : dim);
        int ty = iy + (ITEM_H - s->theme.list.size) / 2;
        draw_text_clipped(s, s->list_font, text ? text : "?",
                          12, ty, list_w - 20, col);
    }

    /* Scroll indicator on the right edge */
    if (count > MAX_ITEMS) {
        int track_h = CONTENT_H;
        int thumb_h = track_h * MAX_ITEMS / count;
        int thumb_y = TOPBAR_H + track_h * s->scroll_offset / count;
        SDL_Rect thumb = {list_w - 4, thumb_y, 3, thumb_h};
        SDL_FillRect(s->renderer, &thumb,
                     SDL_MapRGB(s->renderer->format, 160, 160, 160));
    }
}

static void draw_cover_panel(MenuState* s) {
    int px = LIST_W;
    SDL_Rect panel = {px, TOPBAR_H, COVER_W, CONTENT_H};
    SDL_FillRect(s->renderer, &panel,
                 SDL_MapRGB(s->renderer->format, 10, 10, 20));

    if (s->cover) {
        /* Centre the cover in the panel */
        int cx = px + (COVER_W  - s->cover->w) / 2;
        int cy = TOPBAR_H + (CONTENT_H - s->cover->h) / 2;
        SDL_Rect dst = {cx, cy, s->cover->w, s->cover->h};
        SDL_BlitSurface(s->cover, NULL, s->renderer, &dst);
    } else {
        /* Placeholder */
        SDL_Color dim = {60, 60, 80, 0};
        int tx = px + COVER_W / 2 - 30;
        int ty = TOPBAR_H + CONTENT_H / 2 - 8;
        draw_text(s, s->list_font, "No cover", tx, ty, dim);
    }
}

/* ── Label callbacks ─────────────────────────────────────────────── */

static const char* platform_label(MenuState* s, int i) { return s->platforms[i].name; }
static const char* rom_label(MenuState* s, int i) {
    const char* n = s->roms[i].name;
    return (n && n[0]) ? n : s->roms[i].file_name;
}

/* ── Cover loading ──────────────────────────────────────────────── */

static void maybe_load_cover(MenuState* s) {
    if (s->current_screen != SCREEN_ROMS || s->rom_count == 0) return;

    Uint32 now = SDL_GetTicks();
    if (now - s->selection_tick < COVER_DELAY) return;

    RomMRom* rom = &s->roms[s->selected_index];
    if (rom->id == s->cover_rom_id) return; /* already loaded */

    if (s->cover) { SDL_FreeSurface(s->cover); s->cover = NULL; }
    s->cover_rom_id = rom->id;

    const char* path = rom->path_cover_s;
    if (!path || !path[0]) return;

    s->cover = cover_get(s->server_url, s->username, s->password,
                         rom->id,
                         rom->platform_slug ? rom->platform_slug : "unknown",
                         path, COVER_MAX_W, COVER_MAX_H);
}

/* ── Navigation ─────────────────────────────────────────────────── */

static void nav_up(MenuState* s) {
    if (s->selected_index > 0) {
        s->selected_index--;
        if (s->selected_index < s->scroll_offset) s->scroll_offset--;
        s->selection_tick = SDL_GetTicks();
    }
}

static void nav_down(MenuState* s, int count) {
    if (s->selected_index < count - 1) {
        s->selected_index++;
        if (s->selected_index >= s->scroll_offset + MAX_ITEMS) s->scroll_offset++;
        s->selection_tick = SDL_GetTicks();
    }
}

static void reset_cursor(MenuState* s) {
    s->selected_index = 0;
    s->scroll_offset  = 0;
    s->selection_tick = SDL_GetTicks();
}

/* ── Download ───────────────────────────────────────────────────── */

static void ensure_dir(const char* p) { mkdir(p, 0755); }

static int do_download(MenuState* s, RomMRom* rom) {
    const char* slug = rom->platform_slug ? rom->platform_slug : "unknown";
    const char* file = rom->file_name     ? rom->file_name     : "rom";

    char dir[512], dest[768], url[1024];
    snprintf(dir,  sizeof(dir),  "/mnt/SDCARD/Roms/%s", slug);
    snprintf(dest, sizeof(dest), "%s/%s", dir, file);
    snprintf(url,  sizeof(url),  "%s/api/roms/%d/content/%s",
             s->server_url, rom->id, file);

    ensure_dir("/mnt/SDCARD/Roms");
    ensure_dir(dir);

    return download_rom(url, s->username, s->password, dest);
}

/* ── Message overlay ─────────────────────────────────────────────── */

static void render_message(MenuState* s, const char* msg) {
    draw_background(s);
    SDL_Color white = {255, 255, 255, 0};
    int tw, th;
    TTF_SizeText(s->list_font, msg, &tw, &th);
    draw_text(s, s->list_font, msg,
              (s->display_width  - tw) / 2,
              (s->display_height - th) / 2, white);
    flip(s);
}

/* ── Screen renderers ────────────────────────────────────────────── */

static void render_platforms(MenuState* s) {
    draw_background(s);

    char title[64];
    snprintf(title, sizeof(title), "Platforms  (%d)", s->platform_count);
    draw_topbar(s, title);

    draw_list(s, SCREEN_W, s->platform_count, platform_label);
    draw_hintbar(s, "Select", NULL);
    flip(s);
}

static void render_roms(MenuState* s) {
    draw_background(s);

    char title[128];
    snprintf(title, sizeof(title), "%s  (%d ROMs)",
             s->platforms[s->active_platform_idx].name,
             s->rom_count);
    draw_topbar(s, title);

    /* Clip list rendering to the left panel */
    SDL_SetClipRect(s->renderer,
                    &(SDL_Rect){0, TOPBAR_H, LIST_W, CONTENT_H});
    draw_list(s, LIST_W, s->rom_count, rom_label);
    SDL_SetClipRect(s->renderer, NULL);

    draw_cover_panel(s);
    draw_hintbar(s, "Download", "Back");
    flip(s);
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    MenuState s = {0};

    if (init_menu(&s) < 0) { fprintf(stderr, "init failed\n"); return 1; }
    if (read_config(&s, "/mnt/SDCARD/App/RomM/config.txt") < 0) {
        cleanup_menu(&s); return 1;
    }

    render_message(&s, "Loading platforms...");

    if (fetch_platform_list(s.server_url, s.username, s.password,
                            &s.platforms, &s.platform_count) < 0) {
        render_message(&s, "Error: could not reach RomM server");
        SDL_Delay(2500);
        cleanup_menu(&s); return 1;
    }

    bool quit = false;
    SDL_Event ev;

    while (!quit) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type != SDL_KEYDOWN) continue;
            SDLKey key = ev.key.keysym.sym;

            if (key == KEY_START) { quit = true; break; }

            if (s.current_screen == SCREEN_PLATFORMS) {
                if      (key == SDLK_UP)   nav_up(&s);
                else if (key == SDLK_DOWN) nav_down(&s, s.platform_count);
                else if (key == KEY_A && s.platform_count > 0) {
                    s.active_platform_idx = s.selected_index;
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Loading %s...",
                             s.platforms[s.selected_index].name);
                    render_message(&s, msg);
                    if (fetch_rom_list(s.server_url, s.username, s.password,
                                       s.platforms[s.selected_index].id,
                                       &s.roms, &s.rom_count) == 0) {
                        s.current_screen = SCREEN_ROMS;
                        reset_cursor(&s);
                    } else {
                        render_message(&s, "Error loading ROMs");
                        SDL_Delay(1500);
                    }
                }

            } else { /* SCREEN_ROMS */
                if      (key == SDLK_UP)   nav_up(&s);
                else if (key == SDLK_DOWN) nav_down(&s, s.rom_count);
                else if (key == KEY_B) {
                    if (s.cover) { SDL_FreeSurface(s.cover); s.cover = NULL; }
                    s.cover_rom_id = -1;
                    free_rom_list(s.roms, s.rom_count);
                    s.roms = NULL; s.rom_count = 0;
                    s.current_screen = SCREEN_PLATFORMS;
                    s.selected_index = s.active_platform_idx;
                    s.scroll_offset  = s.active_platform_idx >= MAX_ITEMS
                                       ? s.active_platform_idx - MAX_ITEMS + 1 : 0;
                } else if (key == KEY_A && s.rom_count > 0) {
                    RomMRom* rom = &s.roms[s.selected_index];
                    const char* name = (rom->name && rom->name[0])
                                       ? rom->name : rom->file_name;
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Downloading %s...",
                             name ? name : "ROM");
                    render_message(&s, msg);
                    render_message(&s,
                        do_download(&s, rom) == 0
                            ? "Download complete!" : "Download failed");
                    SDL_Delay(1500);
                }
            }
        }

        /* Update cover art after debounce */
        maybe_load_cover(&s);

        /* Frame-rate limited render */
        s.cur_tick_count = SDL_GetTicks();
        if (s.cur_tick_count - s.last_tick_count >= FRAME_MS) {
            if (s.current_screen == SCREEN_PLATFORMS) render_platforms(&s);
            else                                       render_roms(&s);
            s.last_tick_count = s.cur_tick_count;
        } else {
            SDL_Delay(1);
        }
    }

    cleanup_menu(&s);
    return 0;
}
