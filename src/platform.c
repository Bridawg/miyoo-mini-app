#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <json-c/json.h>
#include <curl/curl.h>
#include "platform.h"
#include "response.h"

void free_firmware(RomMPlatformFirmware* firmware) {
    if (!firmware) return;

    free(firmware->file_name);
    free(firmware->file_name_no_tags);
    free(firmware->file_name_no_ext);
    free(firmware->file_extension);
    free(firmware->file_path);
    free(firmware->full_path);
    free(firmware->crc_hash);
    free(firmware->md5_hash);
    free(firmware->sha1_hash);
    free(firmware->created_at);
    free(firmware->updated_at);
}

void free_platform(RomMPlatform* platform) {
    if (!platform) return;

    free(platform->slug);
    free(platform->fs_slug);
    free(platform->name);
    free(platform->logo_path);
    free(platform->created_at);
    free(platform->updated_at);
}

void free_platform_list(RomMPlatform* platforms, int count) {
    for (int i = 0; i < count; i++) {
        free_platform(&platforms[i]);
    }
    free(platforms);
}

int fetch_platform_list(const char* server_host, const char* username, const char* password,
                        RomMPlatform** platform_list, int* platform_count) {
    Response* resp = response_init();
    if (!resp) {
        fprintf(stderr, "Failed to initialize response buffer\n");
        return -1;
    }

    char url[1024];
    snprintf(url, sizeof(url), "%s/api/platforms", server_host);

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
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, ROMM_CONNECT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, ROMM_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        response_free(resp);
        return ROMM_ERR_NETWORK;
    }

    /* CURLE_OK only says the exchange completed. Check what came back. */
    if (http_code == 401 || http_code == 403) {
        fprintf(stderr, "auth rejected by server (HTTP %ld)\n", http_code);
        response_free(resp);
        return ROMM_ERR_AUTH;
    }
    if (http_code < 200 || http_code > 299) {
        fprintf(stderr, "server returned HTTP %ld\n", http_code);
        response_free(resp);
        return ROMM_ERR_SERVER;
    }

    struct json_object* parsed_json = json_tokener_parse(response_get_memory(resp));
    response_free(resp);

    if (!parsed_json) {
        fprintf(stderr, "Failed to parse JSON response\n");
        return ROMM_ERR_PARSE;
    }

    /* json_object_array_length() asserts on a non-array, and an error body is
     * an object — so the type must be checked before it is trusted. */
    if (!json_object_is_type(parsed_json, json_type_array)) {
        fprintf(stderr, "expected a JSON array of platforms\n");
        json_object_put(parsed_json);
        return ROMM_ERR_PARSE;
    }

    *platform_count = json_object_array_length(parsed_json);
    if (*platform_count == 0) {   /* legitimately empty; malloc(0) may return NULL */
        *platform_list = NULL;
        json_object_put(parsed_json);
        return ROMM_OK;
    }

    *platform_list = malloc(*platform_count * sizeof(RomMPlatform));
    if (!*platform_list) {
        fprintf(stderr, "Failed to allocate platform list\n");
        json_object_put(parsed_json);
        return -1;
    }

    for (int i = 0; i < *platform_count; i++) {
        struct json_object* obj = json_object_array_get_idx(parsed_json, i);
        struct json_object* field;

        (*platform_list)[i].id = json_object_get_int(json_object_object_get(obj, "id"));

        field = json_object_object_get(obj, "slug");
        (*platform_list)[i].slug = field ? strdup(json_object_get_string(field)) : NULL;

        field = json_object_object_get(obj, "fs_slug");
        (*platform_list)[i].fs_slug = field ? strdup(json_object_get_string(field)) : NULL;

        field = json_object_object_get(obj, "name");
        (*platform_list)[i].name = field ? strdup(json_object_get_string(field)) : NULL;

        (*platform_list)[i].rom_count = json_object_get_int(json_object_object_get(obj, "rom_count"));

        field = json_object_object_get(obj, "logo_path");
        (*platform_list)[i].logo_path = field ? strdup(json_object_get_string(field)) : NULL;

        field = json_object_object_get(obj, "created_at");
        (*platform_list)[i].created_at = field ? strdup(json_object_get_string(field)) : NULL;

        field = json_object_object_get(obj, "updated_at");
        (*platform_list)[i].updated_at = field ? strdup(json_object_get_string(field)) : NULL;

        field = json_object_object_get(obj, "igdb_id");
        (*platform_list)[i].igdb_id = field ? json_object_get_int(field) : -1;

        field = json_object_object_get(obj, "sgdb_id");
        (*platform_list)[i].sgdb_id = field ? json_object_get_int(field) : -1;

        field = json_object_object_get(obj, "moby_id");
        (*platform_list)[i].moby_id = field ? json_object_get_int(field) : -1;

        (*platform_list)[i].firmware = NULL;
        (*platform_list)[i].firmware_count = 0;
    }

    json_object_put(parsed_json);
    return 0;
}
