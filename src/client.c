#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <stdbool.h>
#include <sys/stat.h>

#include "platform.h"
#include "rom.h"
#include "theme.h"
#include "cover.h"
#include "menu_state.h"
#include "response.h"   /* ROMM_OK / ROMM_ERR_* fetch outcomes */

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
#define COVER_W      220
#define LIST_W       (SCREEN_W - COVER_W)
#define COVER_MAX_W  200
#define COVER_MAX_H  266
#define FRAME_MS      16
#define COVER_DELAY  300

/* Virtual keyboard ─────────────────────────────────────────────── */
#define KBD_SPC       '\x01'   /* insert space       */
#define KBD_DEL       '\x08'   /* delete last char   */
#define KBD_ROW_COUNT  8
#define KBD_CELL_W    46       /* 13 cols × 46 = 598, centred in 640 */
#define KBD_CELL_H_MIN 28      /* floor; actual pitch follows the font */
#define KBD_X0        21       /* (640 − 13×46) / 2                  */
#define KBD_Y0       118       /* starts just below the text box     */

/* Covers the full printable ASCII set — passwords routinely use punctuation
 * that a smaller grid simply cannot enter, and there is no other way to type
 * on this device. Keep every character reachable when editing these rows. */
static const char* kbd_rows[KBD_ROW_COUNT] = {
    "abcdefghijklm",
    "nopqrstuvwxyz",
    "ABCDEFGHIJKLM",
    "NOPQRSTUVWXYZ",
    "0123456789.:/",
    "-_@#!?$%&*+=~",
    "^`|\\<>[]{}()\"",
    ",;'\x01\x08",              /* SP and DEL at end of last row      */
};

static const char* settings_labels[] = {"Server URL", "Username", "Password"};

/* Onion OS skin paths */
#define SKIN_DIR    "/mnt/SDCARD/miyoo/app/skin/"
#define SKIN_BG     SKIN_DIR "bg-list-l.png"
#define SKIN_TOPBAR SKIN_DIR "miyoo-topbar.png"
#define SKIN_ICON_A SKIN_DIR "icon-A-54.png"
#define SKIN_ICON_B SKIN_DIR "icon-B-54.png"

/* Onion OS button mapping */
#define KEY_A      SDLK_SPACE
#define KEY_B      SDLK_LCTRL
#define KEY_START  SDLK_RETURN
#define KEY_SELECT SDLK_ESCAPE

/* Config path */
#define CONFIG_PATH "/mnt/SDCARD/App/RomM/config.txt"

/* ── Helpers ────────────────────────────────────────────────────── */

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
    if (s->renderer)    SDL_FreeSurface(s->renderer);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

int init_menu(MenuState* s) {
    memset(s, 0, sizeof(MenuState));
    s->cover_rom_id = -1;

    /* The Mini Plus mounts its panel 180 degrees from the original Mini, so the
     * framebuffer renders upside down — which also makes the d-pad look
     * inverted, since "down" moves the cursor toward the physical top.
     *
     * We rotate ourselves in flip() rather than via SDL_VIDEO_FBCON_ROTATION:
     * which libSDL actually loads at runtime is not knowable from here (Onion
     * searches /lib and /config/lib before miyoo/lib), and stock fbcon *fails
     * SDL_Init outright* on a rotation value it does not recognise. Doing it in
     * our own blit works against any driver and cannot break startup.
     *
     * ROMM_ROTATION=NONE disables it, e.g. on an original Mini. */
    {
        const char* rot = getenv("ROMM_ROTATION");
        s->rotate_180 = !(rot && !strcasecmp(rot, "NONE"));
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return -1;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); SDL_Quit(); return -1;
    }
    if (IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) == 0)
        fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());

    const SDL_VideoInfo* info = SDL_GetVideoInfo();
    if (!info) {
        fprintf(stderr, "SDL_GetVideoInfo failed\n");
        TTF_Quit(); SDL_Quit(); return -1;
    }
    s->display_width  = info->current_w;
    s->display_height = info->current_h;

    theme_load(&s->theme);
    s->title_font = try_open_font(s->theme.title.font, s->theme.title.size);
    s->list_font  = try_open_font(s->theme.list.font,  s->theme.list.size);
    if (!s->title_font || !s->list_font) {
        fprintf(stderr, "Could not load any font\n");
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return -1;
    }

    /* Themes choose the font size, so every box that holds a line of text has
     * to be sized from the actual rendered height — a hardcoded height clips
     * descenders on any theme whose font is larger than the author assumed. */
    s->line_h = TTF_FontHeight(s->list_font);
    if (s->line_h <= 0) s->line_h = s->theme.list.size;

    s->title_h = TTF_FontHeight(s->title_font);
    if (s->title_h <= 0) s->title_h = s->theme.title.size;

    s->kbd_cell_h = s->line_h + 4;
    if (s->kbd_cell_h < KBD_CELL_H_MIN) s->kbd_cell_h = KBD_CELL_H_MIN;
    /* Keep all 8 rows clear of the hint bar, shrinking the pitch if a very
     * large theme font would otherwise push the bottom row underneath it. */
    {
        int avail = (SCREEN_H - HINTBAR_H - 6) - KBD_Y0;
        if (s->kbd_cell_h * KBD_ROW_COUNT > avail)
            s->kbd_cell_h = avail / KBD_ROW_COUNT;
    }

    s->screen = SDL_SetVideoMode(s->display_width, s->display_height, 32, SDL_HWSURFACE);
    if (!s->screen) {
        fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError());
        TTF_CloseFont(s->title_font); TTF_CloseFont(s->list_font);
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return -1;
    }

    /* Goes to romm.log via launch.sh. The rotation only applies on a 32bpp
     * screen of matching size; say so plainly rather than silently not turning. */
    fprintf(stderr, "video: %dx%d %dbpp driver=%s rotate_180=%s%s\n",
            s->screen->w, s->screen->h, s->screen->format->BitsPerPixel,
            SDL_VideoDriverName((char[16]){0}, 16),
            s->rotate_180 ? "yes" : "no",
            (s->rotate_180 && s->screen->format->BytesPerPixel != 4)
                ? "  (INACTIVE: screen is not 32bpp)" : "");

    s->renderer = SDL_CreateRGBSurface(SDL_SWSURFACE,
                      s->display_width, s->display_height, 32, 0, 0, 0, 0);
    if (!s->renderer) {
        fprintf(stderr, "SDL_CreateRGBSurface: %s\n", SDL_GetError());
        TTF_CloseFont(s->title_font); TTF_CloseFont(s->list_font);
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return -1;
    }

    s->skin_bg     = IMG_Load(SKIN_BG);
    s->skin_topbar = IMG_Load(SKIN_TOPBAR);
    s->skin_icon_a = IMG_Load(SKIN_ICON_A);
    s->skin_icon_b = IMG_Load(SKIN_ICON_B);

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
    s->server_url[0] = s->username[0] = s->password[0] = '\0';
    return 0;
}

/* ── Config ─────────────────────────────────────────────────────── */

static int read_config(MenuState* s, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if      (!strncmp(line, "server_url=", 11)) snprintf(s->server_url, 256, "%s", line + 11);
        else if (!strncmp(line, "username=",    9)) snprintf(s->username,   256, "%s", line +  9);
        else if (!strncmp(line, "password=",    9)) snprintf(s->password,   256, "%s", line +  9);
    }
    fclose(f); return 0;
}

static void save_config(MenuState* s) {
    FILE* f = fopen(CONFIG_PATH, "w");
    if (!f) return;
    fprintf(f, "server_url=%s\nusername=%s\npassword=%s\n",
            s->server_url, s->username, s->password);
    fclose(f);
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
    int tw, th; TTF_SizeText(font, text, &tw, &th);
    if (tw <= max_w) { draw_text(s, font, text, x, y, col); return; }

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

/* Copy src to dst rotated 180 degrees. Both must be 32bpp and the same size;
 * the caller falls back to a plain blit otherwise. Reading each source row
 * forward and writing the mirrored row backward keeps this to one linear pass. */
static void blit_rot180(SDL_Surface* src, SDL_Surface* dst) {
    if (SDL_MUSTLOCK(dst) && SDL_LockSurface(dst) < 0) return;

    const int w = src->w, h = src->h;
    for (int y = 0; y < h; y++) {
        const Uint32* srow = (const Uint32*)((const Uint8*)src->pixels + (size_t)y * src->pitch);
        Uint32* drow = (Uint32*)((Uint8*)dst->pixels + (size_t)(h - 1 - y) * dst->pitch);
        for (int x = 0; x < w; x++)
            drow[w - 1 - x] = srow[x];
    }

    if (SDL_MUSTLOCK(dst)) SDL_UnlockSurface(dst);
}

static void flip(MenuState* s) {
    if (s->rotate_180
        && s->renderer->format->BytesPerPixel == 4
        && s->screen->format->BytesPerPixel == 4
        && s->renderer->w == s->screen->w
        && s->renderer->h == s->screen->h) {
        blit_rot180(s->renderer, s->screen);
    } else {
        SDL_BlitSurface(s->renderer, NULL, s->screen, NULL);
    }
    SDL_Flip(s->screen);
}

/* ── Render helpers ─────────────────────────────────────────────── */

static void draw_background(MenuState* s) {
    /* Always clear first. s->renderer persists between frames, and the skin PNG
     * may carry an alpha channel or be smaller than the screen — in either case
     * blitting it alone leaves the previous frame showing through, which strands
     * the old selection highlight on every cell the cursor has visited. */
    SDL_FillRect(s->renderer, NULL, SDL_MapRGB(s->renderer->format, 20, 20, 30));
    if (s->skin_bg)
        SDL_BlitSurface(s->skin_bg, NULL, s->renderer, NULL);
}

static void draw_topbar(MenuState* s, const char* title) {
    int tx = 16;

    if (s->skin_topbar) {
        /* miyoo-topbar.png is the Onion badge, NOT a full-width bar — it is
         * 148x42 in the default Silky theme. SDL_BlitSurface takes its size
         * from the source and ignores dst w/h, so this draws the badge at the
         * top left and the title has to start clear of it. Measure the surface
         * rather than hardcoding, so other themes' badges also fit. */
        SDL_Rect dst = {0, 0, 0, 0};
        SDL_BlitSurface(s->skin_topbar, NULL, s->renderer, &dst);
        tx = s->skin_topbar->w + 12;
    } else {
        SDL_Rect bar = {0, 0, s->display_width, TOPBAR_H};
        SDL_FillRect(s->renderer, &bar, SDL_MapRGB(s->renderer->format, 0, 0, 0));
    }

    int ty = (TOPBAR_H - s->title_h) / 2;
    if (ty < 0) ty = 0;
    draw_text_clipped(s, s->title_font, title, tx, ty,
                      s->display_width - tx - 16, s->theme.title.color);
}

static void draw_hintbar(MenuState* s, const char* a_label, const char* b_label) {
    int y = SCREEN_H - HINTBAR_H;
    SDL_Rect bar = {0, y, s->display_width, HINTBAR_H};
    SDL_FillRect(s->renderer, &bar, SDL_MapRGB(s->renderer->format, 0, 0, 0));

    SDL_Color white = {255, 255, 255, 0};
    int icon_y = y + (HINTBAR_H - 24) / 2;
    int cx = 16;

    if (s->skin_icon_a) {
        SDL_Rect src = {0, 0, 24, 24};
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

/* Draw right-aligned dim text inside the hint bar (call after draw_hintbar). */
static void draw_hintbar_right(MenuState* s, const char* text) {
    SDL_Color dim = {130, 130, 130, 0};
    int tw, th; TTF_SizeText(s->list_font, text, &tw, &th);
    int y = SCREEN_H - HINTBAR_H + (HINTBAR_H - th) / 2;
    draw_text(s, s->list_font, text, SCREEN_W - tw - 16, y, dim);
}

static void draw_list(MenuState* s, int list_w, int count,
                      const char* (*label)(MenuState*, int)) {
    SDL_Color dim = {180, 180, 180, 0};

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
    SDL_FillRect(s->renderer, &panel, SDL_MapRGB(s->renderer->format, 10, 10, 20));

    if (s->cover) {
        int cx = px + (COVER_W  - s->cover->w) / 2;
        int cy = TOPBAR_H + (CONTENT_H - s->cover->h) / 2;
        SDL_Rect dst = {cx, cy, s->cover->w, s->cover->h};
        SDL_BlitSurface(s->cover, NULL, s->renderer, &dst);
    } else {
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
    if (rom->id == s->cover_rom_id) return;
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

/* ── Settings screen ─────────────────────────────────────────────── */

static void enter_settings(MenuState* s) {
    strncpy(s->settings_buf[0], s->server_url, 255); s->settings_buf[0][255] = '\0';
    strncpy(s->settings_buf[1], s->username,   255); s->settings_buf[1][255] = '\0';
    strncpy(s->settings_buf[2], s->password,   255); s->settings_buf[2][255] = '\0';
    s->settings_field = 0;
    s->settings_mode  = 0;
    s->kbd_row        = 0;
    s->kbd_col        = 0;
    s->current_screen = SCREEN_SETTINGS;
}

static void kbd_append(MenuState* s, char ch) {
    char* buf = s->settings_buf[s->settings_field];
    size_t len = strlen(buf);
    if (len >= 255) return;
    buf[len] = ch; buf[len + 1] = '\0';
}

static void kbd_backspace(MenuState* s) {
    char* buf = s->settings_buf[s->settings_field];
    size_t len = strlen(buf);
    if (len > 0) buf[len - 1] = '\0';
}

static void render_settings(MenuState* s) {
    draw_background(s);

    SDL_Color white   = {255, 255, 255, 0};
    SDL_Color gray    = {140, 140, 140, 0};
    Uint32    sel_bg  = SDL_MapRGB(s->renderer->format,
                            s->theme.selected_bg.r,
                            s->theme.selected_bg.g,
                            s->theme.selected_bg.b);
    Uint32    dark_bg = SDL_MapRGB(s->renderer->format, 30, 30, 40);

    if (s->settings_mode == 0) {
        /* ── Field selection ── */
        draw_topbar(s, "Settings");

        for (int i = 0; i < 3; i++) {
            int ly = 60 + i * 100;
            int by = ly + 22;
            bool active = (i == s->settings_field);

            draw_text(s, s->list_font, settings_labels[i], 16, ly,
                      active ? white : gray);

            int box_h = s->line_h + 6;
            SDL_Rect box = {16, by, SCREEN_W - 32, box_h};
            SDL_FillRect(s->renderer, &box, active ? sel_bg : dark_bg);

            /* Show password as asterisks */
            char disp[260] = {0};
            if (i == 2) {
                int n = (int)strlen(s->settings_buf[2]);
                for (int j = 0; j < n && j < 255; j++) disp[j] = '*';
            } else {
                strncpy(disp, s->settings_buf[i], 255);
            }
            draw_text_clipped(s, s->list_font, disp[0] ? disp : " ",
                              22, by + (box_h - s->line_h) / 2,
                              SCREEN_W - 50, white);
        }

        draw_hintbar(s, "Edit", "Cancel");
        draw_hintbar_right(s, "Start: Save");

    } else {
        /* ── Keyboard mode ── */
        char title[64];
        snprintf(title, sizeof(title), "Edit: %s",
                 settings_labels[s->settings_field]);
        draw_topbar(s, title);

        /* Current value box */
        int box_h = s->line_h + 6;
        SDL_Rect box = {16, 58, SCREEN_W - 32, box_h};
        SDL_FillRect(s->renderer, &box, sel_bg);

        char disp[260] = {0};
        const char* src = s->settings_buf[s->settings_field];
        if (s->settings_field == 2) {
            int n = (int)strlen(src);
            for (int j = 0; j < n && j < 255; j++) disp[j] = '*';
        } else {
            strncpy(disp, src, 255);
        }
        strncat(disp, "|", sizeof(disp) - strlen(disp) - 1);
        draw_text_clipped(s, s->list_font, disp, 22, 58 + (box_h - s->line_h) / 2,
                          SCREEN_W - 50, white);

        /* Keyboard grid */
        for (int r = 0; r < KBD_ROW_COUNT; r++) {
            const char* row = kbd_rows[r];
            int ncols = (int)strlen(row);
            for (int c = 0; c < ncols; c++) {
                int kx = KBD_X0 + c * KBD_CELL_W;
                int ky = KBD_Y0 + r * s->kbd_cell_h;
                bool sel = (r == s->kbd_row && c == s->kbd_col);

                if (sel) {
                    SDL_Rect cell = {kx - 2, ky, KBD_CELL_W - 2, s->kbd_cell_h - 2};
                    SDL_FillRect(s->renderer, &cell, sel_bg);
                }

                char ch = row[c];
                char lbuf[4] = {ch, '\0', '\0', '\0'};
                const char* label = lbuf;
                if (ch == KBD_SPC) label = "SP";
                else if (ch == KBD_DEL) label = "<-";

                draw_text(s, s->list_font, label, kx + 4,
                          ky + (s->kbd_cell_h - s->line_h) / 2,
                          sel ? s->theme.selected_text : white);
            }
        }

        draw_hintbar(s, "Type", "Delete");
        draw_hintbar_right(s, "Start: Done");
    }

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
    draw_hintbar_right(s, "Menu: Settings");
    flip(s);
}

static void render_roms(MenuState* s) {
    draw_background(s);
    char title[128];
    snprintf(title, sizeof(title), "%s  (%d ROMs)",
             s->platforms[s->active_platform_idx].name,
             s->rom_count);
    draw_topbar(s, title);

    SDL_SetClipRect(s->renderer, &(SDL_Rect){0, TOPBAR_H, LIST_W, CONTENT_H});
    draw_list(s, LIST_W, s->rom_count, rom_label);
    SDL_SetClipRect(s->renderer, NULL);

    draw_cover_panel(s);
    draw_hintbar(s, "Download", "Back");
    flip(s);
}

/* ── Main ───────────────────────────────────────────────────────── */

/* Turn a fetch result into something the user can act on. A wrong password and
 * an unreachable server are very different problems and must not read alike. */
static const char* fetch_error_message(int rc) {
    switch (rc) {
        case ROMM_ERR_AUTH:    return "Wrong username or password";
        case ROMM_ERR_SERVER:  return "Server error - check the address";
        case ROMM_ERR_PARSE:   return "Unexpected reply - is this a RomM server?";
        case ROMM_ERR_NETWORK: /* fall through */
        default:               return "Could not reach RomM server";
    }
}

/* Load platforms; show error overlay for 2.5 s on failure. Returns 0 on success. */
static int load_platforms(MenuState* s) {
    render_message(s, "Loading platforms...");
    int rc = fetch_platform_list(s->server_url, s->username, s->password,
                                 &s->platforms, &s->platform_count);
    if (rc < 0) {
        s->platforms = NULL;
        s->platform_count = 0;
        render_message(s, fetch_error_message(rc));
        SDL_Delay(2500);
        return rc;
    }
    return 0;
}

int main(void) {
    MenuState s = {0};

    if (init_menu(&s) < 0) { fprintf(stderr, "init failed\n"); return 1; }

    if (read_config(&s, CONFIG_PATH) == 0 && s.server_url[0]) {
        /* Land in settings rather than on an empty list the user cannot explain
         * — bad credentials are usually a typo they want to go straight back to. */
        if (load_platforms(&s) < 0)
            enter_settings(&s);
        else
            s.current_screen = SCREEN_PLATFORMS;
    } else {
        /* First run or missing config — open settings directly */
        enter_settings(&s);
    }

    bool quit = false;
    SDL_Event ev;

    while (!quit) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type != SDL_KEYDOWN) continue;
            SDLKey key = ev.key.keysym.sym;

            /* ── Platform list ── */
            if (s.current_screen == SCREEN_PLATFORMS) {
                if      (key == KEY_START)  { quit = true; break; }
                else if (key == SDLK_UP)    nav_up(&s);
                else if (key == SDLK_DOWN)  nav_down(&s, s.platform_count);
                else if (key == KEY_SELECT) enter_settings(&s);
                else if (key == KEY_A && s.platform_count > 0) {
                    s.active_platform_idx = s.selected_index;
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Loading %s...",
                             s.platforms[s.selected_index].name);
                    render_message(&s, msg);
                    int rc = fetch_rom_list(s.server_url, s.username, s.password,
                                            s.platforms[s.selected_index].id,
                                            &s.roms, &s.rom_count);
                    if (rc == 0) {
                        s.current_screen = SCREEN_ROMS;
                        reset_cursor(&s);
                    } else {
                        s.roms = NULL;
                        s.rom_count = 0;
                        render_message(&s, fetch_error_message(rc));
                        SDL_Delay(1500);
                    }
                }

            /* ── ROM list ── */
            } else if (s.current_screen == SCREEN_ROMS) {
                if      (key == KEY_START) { quit = true; break; }
                else if (key == SDLK_UP)   nav_up(&s);
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
                    snprintf(msg, sizeof(msg), "Downloading %s...", name ? name : "ROM");
                    render_message(&s, msg);
                    render_message(&s, do_download(&s, rom) == 0
                                       ? "Download complete!" : "Download failed");
                    SDL_Delay(1500);
                }

            /* ── Settings ── */
            } else {
                if (s.settings_mode == 0) {
                    /* Field selection */
                    if (key == SDLK_UP && s.settings_field > 0)
                        s.settings_field--;
                    else if (key == SDLK_DOWN && s.settings_field < 2)
                        s.settings_field++;
                    else if (key == KEY_A) {
                        s.settings_mode = 1;
                        s.kbd_row = 0; s.kbd_col = 0;
                    } else if (key == KEY_START) {
                        /* Save and return to platforms */
                        snprintf(s.server_url, 256, "%s", s.settings_buf[0]);
                        snprintf(s.username,   256, "%s", s.settings_buf[1]);
                        snprintf(s.password,   256, "%s", s.settings_buf[2]);
                        save_config(&s);
                        if (s.platforms) {
                            free_platform_list(s.platforms, s.platform_count);
                            s.platforms = NULL; s.platform_count = 0;
                        }
                        reset_cursor(&s);
                        load_platforms(&s);
                        s.current_screen = SCREEN_PLATFORMS;
                    } else if (key == KEY_B || key == KEY_SELECT) {
                        /* Cancel — only go back if we already have a valid config */
                        if (s.server_url[0])
                            s.current_screen = SCREEN_PLATFORMS;
                    }
                } else {
                    /* Keyboard active */
                    const char* row  = kbd_rows[s.kbd_row];
                    int         ncols = (int)strlen(row);

                    if (key == SDLK_UP) {
                        if (s.kbd_row > 0) {
                            s.kbd_row--;
                            int n = (int)strlen(kbd_rows[s.kbd_row]);
                            if (s.kbd_col >= n) s.kbd_col = n - 1;
                        }
                    } else if (key == SDLK_DOWN) {
                        if (s.kbd_row < KBD_ROW_COUNT - 1) {
                            s.kbd_row++;
                            int n = (int)strlen(kbd_rows[s.kbd_row]);
                            if (s.kbd_col >= n) s.kbd_col = n - 1;
                        }
                    } else if (key == SDLK_LEFT) {
                        if (s.kbd_col > 0) s.kbd_col--;
                    } else if (key == SDLK_RIGHT) {
                        if (s.kbd_col < ncols - 1) s.kbd_col++;
                    } else if (key == KEY_A) {
                        char ch = row[s.kbd_col];
                        if      (ch == KBD_DEL) kbd_backspace(&s);
                        else if (ch == KBD_SPC) kbd_append(&s, ' ');
                        else                    kbd_append(&s, ch);
                    } else if (key == KEY_B) {
                        kbd_backspace(&s);
                    } else if (key == KEY_START) {
                        /* Done with this field — back to field selection */
                        s.settings_mode = 0;
                    } else if (key == KEY_SELECT) {
                        /* Cancel this field's edit — restore original value */
                        const char* orig = s.settings_field == 0 ? s.server_url
                                         : s.settings_field == 1 ? s.username
                                                                  : s.password;
                        strncpy(s.settings_buf[s.settings_field], orig, 255);
                        s.settings_buf[s.settings_field][255] = '\0';
                        s.settings_mode = 0;
                    }
                }
            }
        }

        maybe_load_cover(&s);

        s.cur_tick_count = SDL_GetTicks();
        if (s.cur_tick_count - s.last_tick_count >= FRAME_MS) {
            if      (s.current_screen == SCREEN_PLATFORMS) render_platforms(&s);
            else if (s.current_screen == SCREEN_ROMS)      render_roms(&s);
            else                                            render_settings(&s);
            s.last_tick_count = s.cur_tick_count;
        } else {
            SDL_Delay(1);
        }
    }

    cleanup_menu(&s);
    return 0;
}
