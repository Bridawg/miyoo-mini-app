#include "rom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>

void free_rom(RomMRom* rom) {
    if (!rom) return;

    free(rom->platform_slug);
    free(rom->platform_name);
    free(rom->file_name);
    free(rom->file_name_no_tags);
    free(rom->file_name_no_ext);
    free(rom->file_extension);
    free(rom->file_path);
    free(rom->name);
    free(rom->slug);
    free(rom->summary);
    free(rom->path_cover_s);
    free(rom->path_cover_l);
    free(rom->url_cover);
    free(rom->revision);
    free(rom->full_path);
    free(rom->created_at);
    free(rom->updated_at);

    free(rom);
}

int download_rom(const char* url, const char* username, const char* password, const char* destination) {
    if (!url || !destination) return -1;

    FILE* fp = fopen(destination, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing\n", destination);
        return -1;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl\n");
        fclose(fp);
        remove(destination);
        return -1;
    }

    char userpwd[512];
    snprintf(userpwd, sizeof(userpwd), "%s:%s",
             username ? username : "",
             password ? password : "");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL); // default fwrite
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        fprintf(stderr, "Download failed: %s\n", curl_easy_strerror(res));
        remove(destination);
        return -1;
    }

    return 0;
}
