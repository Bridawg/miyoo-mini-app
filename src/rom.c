#include "rom.h"
#include "response.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <sys/stat.h>

void free_rom_fields(RomMRom* rom) {
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
}

void free_rom(RomMRom* rom) {
    if (!rom) return;
    free_rom_fields(rom);
    free(rom);
}

void free_rom_list(RomMRom* roms, int count) {
    for (int i = 0; i < count; i++) {
        free_rom_fields(&roms[i]);
    }
    free(roms);
}

int fetch_rom_list(const char* server_host, const char* username, const char* password,
                   int platform_id, RomMRom** rom_list, int* rom_count) {
    Response* resp = response_init();
    if (!resp) {
        fprintf(stderr, "Failed to initialize response buffer\n");
        return -1;
    }

    char url[1024];
    snprintf(url, sizeof(url), "%s/api/roms?platform_id=%d&limit=500", server_host, platform_id);

    char userpwd[512];
    snprintf(userpwd, sizeof(userpwd), "%s:%s", username, password);

    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl\n");
        response_free(resp);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        response_free(resp);
        return -1;
    }

    struct json_object* parsed = json_tokener_parse(response_get_memory(resp));
    response_free(resp);

    if (!parsed) {
        fprintf(stderr, "Failed to parse ROM JSON\n");
        return -1;
    }

    /* RomM returns { "items": [...], "total": N } or a bare array */
    struct json_object* items;
    if (!json_object_object_get_ex(parsed, "items", &items)) {
        items = parsed;
    }

    *rom_count = json_object_array_length(items);
    *rom_list = calloc(*rom_count, sizeof(RomMRom));
    if (!*rom_list) {
        fprintf(stderr, "Failed to allocate ROM list\n");
        json_object_put(parsed);
        return -1;
    }

    for (int i = 0; i < *rom_count; i++) {
        struct json_object* obj = json_object_array_get_idx(items, i);
        struct json_object* field;

        (*rom_list)[i].id          = json_object_get_int(json_object_object_get(obj, "id"));
        (*rom_list)[i].platform_id = json_object_get_int(json_object_object_get(obj, "platform_id"));

        field = json_object_object_get(obj, "file_size_bytes");
        (*rom_list)[i].file_size_bytes = field ? (unsigned long long)json_object_get_int64(field) : 0;

        field = json_object_object_get(obj, "name");
        (*rom_list)[i].name = field ? strdup(json_object_get_string(field)) : NULL;

        field = json_object_object_get(obj, "file_name");
        (*rom_list)[i].file_name = field ? strdup(json_object_get_string(field)) : NULL;

        field = json_object_object_get(obj, "platform_slug");
        (*rom_list)[i].platform_slug = field ? strdup(json_object_get_string(field)) : NULL;

        field = json_object_object_get(obj, "platform_name");
        (*rom_list)[i].platform_name = field ? strdup(json_object_get_string(field)) : NULL;
    }

    json_object_put(parsed);
    return 0;
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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
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
