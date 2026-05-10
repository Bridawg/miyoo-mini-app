#ifndef ROMM_COVER_H
#define ROMM_COVER_H

#include "SDL/SDL.h"

/* Returns a scaled SDL_Surface* for the ROM's cover art, or NULL.
   Checks the local cache first; downloads from the RomM server if
   the file is missing. The caller must SDL_FreeSurface() the result.
   max_w / max_h define the bounding box — aspect ratio is preserved. */
SDL_Surface* cover_get(const char* server_url,
                       const char* username,
                       const char* password,
                       int         rom_id,
                       const char* platform_slug,
                       const char* cover_path,
                       int         max_w,
                       int         max_h);

#endif /* ROMM_COVER_H */
