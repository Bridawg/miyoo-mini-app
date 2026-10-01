#ifndef ROMM_RESPONSE_H
#define ROMM_RESPONSE_H

#include <stdlib.h>

/* Fetch outcomes. curl_easy_perform() returning CURLE_OK only means the HTTP
 * exchange completed — a 401 is a perfectly successful transaction — so
 * callers must distinguish these rather than treating "no curl error" as
 * success. Returned by fetch_platform_list() and fetch_rom_list(). */
#define ROMM_OK           0
#define ROMM_ERR_NETWORK (-1)   /* could not resolve/connect/timed out    */
#define ROMM_ERR_AUTH    (-2)   /* 401/403 — bad username or password     */
#define ROMM_ERR_SERVER  (-3)   /* other non-2xx from the server          */
#define ROMM_ERR_PARSE   (-4)   /* 2xx, but not the JSON shape we expect  */

/* Network timeouts. Without these a bad host can hang for minutes on DNS or
 * a TCP connect, with no way for the user to tell it apart from a slow load. */
#define ROMM_CONNECT_TIMEOUT 8L
#define ROMM_TIMEOUT        20L

// Opaque pointer to hide implementation details
typedef struct Response Response;

// Public interface
Response* response_init(void);
void response_free(Response* resp);
size_t response_write_callback(void* contents, size_t size, size_t nmemb, void* userp);
void response_append(Response* resp, const char* data);

// Getters since the structure is opaque
const char* response_get_memory(const Response* resp);
size_t response_get_size(const Response* resp);

#endif // ROMM_RESPONSE_H
