#include "cover.h"
#include <curl/curl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL/SDL_image.h"

#define COVER_CACHE_DIR "/mnt/SDCARD/App/RomM/covers"

static void ensure_dir(const char* path) { mkdir(path, 0755); }

static int download_file(const char* url,
                         const char* username,
                         const char* password,
                         const char* dest) {
    FILE* fp = fopen(dest, "wb");
    if (!fp) return -1;

    CURL* curl = curl_easy_init();
    if (!curl) { fclose(fp); remove(dest); return -1; }

    char userpwd[512];
    snprintf(userpwd, sizeof(userpwd), "%s:%s",
             username ? username : "", password ? password : "");

    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_USERPWD,       userpwd);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH,      CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) { remove(dest); return -1; }
    return 0;
}

/* Nearest-neighbour scale to fit within (max_w x max_h). */
static SDL_Surface* scale_to_fit(SDL_Surface* src, int max_w, int max_h) {
    float sx = (float)max_w / src->w;
    float sy = (float)max_h / src->h;
    float s  = sx < sy ? sx : sy;
    if (s > 1.0f) s = 1.0f; /* never upscale */

    int dw = (int)(src->w * s);
    int dh = (int)(src->h * s);

    SDL_Surface* dst = SDL_CreateRGBSurface(
        SDL_SWSURFACE, dw, dh, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!dst) return NULL;

    SDL_Surface* conv = SDL_ConvertSurface(src, dst->format, SDL_SWSURFACE);
    if (!conv) { SDL_FreeSurface(dst); return NULL; }

    SDL_Rect dr = {0, 0, dw, dh};
    SDL_SoftStretch(conv, NULL, dst, &dr);
    SDL_FreeSurface(conv);
    return dst;
}

SDL_Surface* cover_get(const char* server_url,
                       const char* username,
                       const char* password,
                       int         rom_id,
                       const char* platform_slug,
                       const char* cover_path,
                       int         max_w,
                       int         max_h) {
    if (!cover_path || !cover_path[0]) return NULL;

    /* Build cache path */
    char slug_dir[512], cache_file[768];
    snprintf(slug_dir,   sizeof(slug_dir),   "%s/%s", COVER_CACHE_DIR,
             platform_slug ? platform_slug : "unknown");
    snprintf(cache_file, sizeof(cache_file), "%s/%d.jpg", slug_dir, rom_id);

    /* Download if not cached */
    struct stat st;
    if (stat(cache_file, &st) != 0) {
        ensure_dir(COVER_CACHE_DIR);
        ensure_dir(slug_dir);

        char url[1024];
        snprintf(url, sizeof(url), "%s/%s", server_url, cover_path);

        if (download_file(url, username, password, cache_file) != 0)
            return NULL;
    }

    SDL_Surface* raw = IMG_Load(cache_file);
    if (!raw) return NULL;

    SDL_Surface* scaled = scale_to_fit(raw, max_w, max_h);
    SDL_FreeSurface(raw);
    return scaled;
}
