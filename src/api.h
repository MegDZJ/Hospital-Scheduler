/*
 * api.h - JSON API on top of the simulation.
 *
 * api_handle_request() knows nothing about sockets, so it can be unit-tested
 * by calling it directly.  Response shape:
 *   success: {"success":true,"message":"...","data":{...}}
 *   error:   {"success":false,"error":"..."}
 */
#ifndef API_H
#define API_H

#include <stddef.h>
#include "simulation.h"

typedef struct {
    int   status;         /* HTTP status code                         */
    char *body;           /* malloc'ed JSON text, never NULL          */
    char  allow[16];      /* filled for 405 responses ("GET"/"POST")  */
} ApiResponse;

/* `path` must not contain a query string.  `body` may be NULL if len == 0. */
void api_handle_request(Simulation *sim, const char *method, const char *path,
                        const char *body, size_t body_len, ApiResponse *resp);

/* True if `path` is one of the /api/ routes (used by the server). */
int api_path_is_api(const char *path);

void api_response_free(ApiResponse *resp);

#endif
