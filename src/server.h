/*
 * server.h - a tiny single-threaded HTTP/1.1 server on POSIX sockets.
 * It serves the embedded web assets and forwards /api/ requests to api.c.
 */
#ifndef SERVER_H
#define SERVER_H

#include "simulation.h"

/* Listens on 127.0.0.1:port until SIGINT/SIGTERM.
 * Returns 0 on clean shutdown, non-zero if the socket could not be set up. */
int server_run(Simulation *sim, int port);

#endif
