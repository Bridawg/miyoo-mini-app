#include "theme.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THEME_CONFIG  "/mnt/SDCARD/miyoo/app/config.json"
#define ONION_DIR     "/mnt/SDCARD/miyoo/app/"
#define FALLBACK_FONT "/mnt/SDCARD/App/RomM/fonts/DejaVuSans.ttf"

static SDL_Color hex_color(const char* hex, SDL_Color fallback) {
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return fallback;
    unsigned int r = 0, g = 0, b = 0;
    sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b);
    SDL_Color c = {(Uint8)r, (Uint8)g, (Uint8)b, 0};
    return c;
}

static void apply_style(struct json_object* obj, ThemeStyle* s) {
    struct json_object* f;
    if (json_object_object_get_ex(obj, "font",  &f)) {
        const char* p = json_object_get_string(f);
        if (p) strncpy(s->font, p, THEME_PATH_MAX - 1);
    }
    if (json_object_object_get_ex(obj, "size",  &f))
        s->size = json_object_get_int(f);
    if (json_object_object_get_ex(obj, "color", &f))
        s->color = hex_color(json_object_get_string(f), s->color);
}

void theme_load(Theme* t) {
    /* Onion OS defaults ------------------------------------------------ */
    SDL_Color white = {255, 255, 255, 0};
    SDL_Color gray  = {104, 104, 104, 0};

    strncpy(t->title.font, ONION_DIR "Exo-2-Bold-Italic_Universal.ttf", THEME_PATH_MAX - 1);
    t->title.size  = 25;
    t->title.color = white;

    /* List font default is wqy-microhei (full CJK coverage) */
    strncpy(t->list.font, ONION_DIR "wqy-microhei.ttc", THEME_PATH_MAX - 1);
    t->list.size  = 25;
    t->list.color = white;

    t->selected_bg   = (SDL_Color){40,  40, 100, 0};
    t->selected_text = white;
    (void)gray;

    /* Parse config.json ------------------------------------------------ */
    FILE* f = fopen(THEME_CONFIG, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    size_t nread = fread(buf, 1, (size_t)sz, f);
    buf[nread] = '\0';
    fclose(f);

    struct json_object* root = json_tokener_parse(buf);
    free(buf);
    if (!root) return;

    struct json_object* section;
    if (json_object_object_get_ex(root, "title", &section)) apply_style(section, &t->title);
    if (json_object_object_get_ex(root, "list",  &section)) apply_style(section, &t->list);

    json_object_put(root);
}
