/*
 * main.c - program entry point: parse options, create the simulation,
 * run the HTTP server.
 */
#include "server.h"
#include "simulation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Simulation g_sim;     /* ~60 KB: kept off the stack */

static void usage(const char *prog)
{
    printf("Usage: %s [--port N] [--tick-seconds N]\n", prog);
    printf("  --port N          TCP port on 127.0.0.1 (default 8080)\n");
    printf("  --tick-seconds N  real seconds per simulated minute while the\n"
           "                    simulation is Running (default %d, 0 = never\n"
           "                    auto-advance; use the Advance buttons)\n",
           DEFAULT_TICK_SECONDS);
}

static int parse_int_arg(const char *text, int min, int max, int *out)
{
    char *end;
    long v = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || v < min || v > max)
        return 0;
    *out = (int)v;
    return 1;
}

int main(int argc, char **argv)
{
    int port = 8080;
    int tick = DEFAULT_TICK_SECONDS;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], 1, 65535, &port)) {
                fprintf(stderr, "Invalid port\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--tick-seconds") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], 0, 3600, &tick)) {
                fprintf(stderr, "Invalid tick length\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    init_simulation(&g_sim);
    g_sim.tick_seconds = tick;
    return server_run(&g_sim, port);
}
