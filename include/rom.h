#ifndef ROMM_ROM_H
#define ROMM_ROM_H

#include <stdbool.h>

// Structure to hold ROM information
typedef struct RomMRom {
    int id;
    int* igdb_id;              // Nullable
    int* sgdb_id;             // object type
    int* moby_id;             // object type
    int platform_id;
    char* platform_slug;
    char* platform_name;
    char* file_name;
    char* file_name_no_tags;
    char* file_name_no_ext;
    char* file_extension;
    char* file_path;
    unsigned long long file_size_bytes;
    char* name;
    char* slug;
    char* summary;
    int* first_release_date;    // Nullable
    char* path_cover_s;
    char* path_cover_l;
    bool has_cover;
    char* url_cover;
    char* revision;
    bool multi;
    void** files;              // Array of object pointers
    int files_count;
    char* full_path;
    char* created_at;          // ISO 8601 datetime string
    char* updated_at;          // ISO 8601 datetime string
} RomMRom;

// Function declarations for memory management
void free_rom(RomMRom* rom);
void free_rom_fields(RomMRom* rom);
void free_rom_list(RomMRom* roms, int count);

// Function declarations for operations
int fetch_rom_list(const char* server_url, const char* username, const char* password,
                   int platform_id, RomMRom** rom_list, int* rom_count);
int download_rom(const char* url, const char* username, const char* password, const char* destination);

#endif /* ROMM_ROM_H */
