#include "server.h"
#include "api.h"
#include "../generated/embedded_assets.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_HEADER_BYTES  8192
#define MAX_BODY_BYTES    16384
#define REQUEST_BUF_SIZE  (MAX_HEADER_BYTES + MAX_BODY_BYTES + 1)

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

static const char *reason_phrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 431: return "Request Header Fields Too Large";
    case 501: return "Not Implemented";
    default:  return "Internal Server Error";
    }
}

static int send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

static void send_response(int fd, int status, const char *content_type,
                          const char *allow, const char *body, size_t len)
{
    char header[512];
    int n;

    n = snprintf(header, sizeof header,
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %zu\r\n"
                 "Cache-Control: no-store\r\n"
                 "X-Content-Type-Options: nosniff\r\n"
                 "%s%s%s"
                 "Connection: close\r\n\r\n",
                 status, reason_phrase(status), content_type, len,
                 allow && allow[0] ? "Allow: " : "",
                 allow && allow[0] ? allow : "",
                 allow && allow[0] ? "\r\n" : "");
    if (n < 0 || (size_t)n >= sizeof header)
        return;
    if (send_all(fd, header, (size_t)n) == 0 && len > 0)
        send_all(fd, body, len);
}

static void send_json_error(int fd, int status, const char *message)
{
    char body[200];
    int n = snprintf(body, sizeof body,
                     "{\"success\":false,\"error\":\"%s\"}", message);
    send_response(fd, status, "application/json; charset=utf-8", NULL,
                  body, (size_t)n);
}

/* ------------------------------------------------------------------ */
/* Receiving one complete request (headers + Content-Length body)      */
/* ------------------------------------------------------------------ */

typedef struct {
    char   method[8];
    char   path[256];
    char  *body;          /* points into the request buffer */
    size_t body_len;
} Request;

/* Returns 0 on success, otherwise an HTTP status to answer with
 * (or -1 when the client vanished and nothing should be sent). */
static int read_request(int fd, char *buf, Request *req)
{
    size_t len = 0;
    size_t header_end = 0;          /* offset of the byte after \r\n\r\n */
    size_t content_length = 0;
    int    have_headers = 0;
    char   version[16];
    char  *line_end;
    char  *q;

    for (;;) {
        ssize_t n;

        if (have_headers && len >= header_end + content_length)
            break;
        if (len >= REQUEST_BUF_SIZE - 1)
            return have_headers ? 413 : 431;

        n = recv(fd, buf + len, REQUEST_BUF_SIZE - 1 - len, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return len > 0 ? 408 : -1;
            return -1;
        }
        if (n == 0)
            return len > 0 ? 400 : -1;     /* closed early */
        len += (size_t)n;
        buf[len] = '\0';

        if (!have_headers) {
            char *end = strstr(buf, "\r\n\r\n");
            if (end == NULL) {
                if (len > MAX_HEADER_BYTES)
                    return 431;
                continue;
            }
            header_end = (size_t)(end - buf) + 4;
            if (header_end > MAX_HEADER_BYTES)
                return 431;
            have_headers = 1;

            /* Look for Content-Length / Transfer-Encoding in the headers. */
            line_end = strstr(buf, "\r\n");
            q = line_end;
            while (q != NULL && q < buf + header_end - 2) {
                q += 2;                          /* start of a header line */
                if (strncasecmp(q, "Content-Length:", 15) == 0) {
                    char *endp;
                    unsigned long v = strtoul(q + 15, &endp, 10);
                    if (endp == q + 15)
                        return 400;
                    if (v > MAX_BODY_BYTES)
                        return 413;
                    content_length = (size_t)v;
                } else if (strncasecmp(q, "Transfer-Encoding:", 18) == 0) {
                    return 501;
                }
                q = strstr(q, "\r\n");
            }
            if (header_end + content_length > REQUEST_BUF_SIZE - 1)
                return 413;
        }
    }

    /* Parse the request line: METHOD SP PATH SP VERSION */
    {
        char *sp1, *sp2, *eol = strstr(buf, "\r\n");
        size_t plen;

        if (eol == NULL)
            return 400;
        *eol = '\0';
        sp1 = strchr(buf, ' ');
        if (sp1 == NULL)
            return 400;
        sp2 = strchr(sp1 + 1, ' ');
        if (sp2 == NULL)
            return 400;
        if ((size_t)(sp1 - buf) >= sizeof req->method)
            return 400;
        memcpy(req->method, buf, (size_t)(sp1 - buf));
        req->method[sp1 - buf] = '\0';

        plen = (size_t)(sp2 - (sp1 + 1));
        if (plen == 0 || plen >= sizeof req->path)
            return 400;
        memcpy(req->path, sp1 + 1, plen);
        req->path[plen] = '\0';

        snprintf(version, sizeof version, "%s", sp2 + 1);
        if (strncmp(version, "HTTP/1.", 7) != 0)
            return 400;
        *eol = '\r';                       /* restore for neatness */
    }

    /* Drop a query string and any fragment. */
    q = strpbrk(req->path, "?#");
    if (q != NULL)
        *q = '\0';
    if (req->path[0] != '/')
        return 400;

    req->body = buf + header_end;
    req->body_len = content_length;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

static void handle_connection(int fd, Simulation *sim, char *buf)
{
    Request req;
    int err;

    memset(&req, 0, sizeof req);
    err = read_request(fd, buf, &req);
    if (err != 0) {
        if (err > 0)
            send_json_error(fd, err, reason_phrase(err));
        return;
    }

    if (api_path_is_api(req.path)) {
        ApiResponse resp;
        api_handle_request(sim, req.method, req.path, req.body, req.body_len,
                           &resp);
        send_response(fd, resp.status, "application/json; charset=utf-8",
                      resp.allow, resp.body, strlen(resp.body));
        api_response_free(&resp);
    } else if (strcmp(req.method, "GET") == 0) {
        const EmbeddedAsset *a = find_embedded_asset(req.path);
        if (a != NULL)
            send_response(fd, 200, a->content_type, NULL,
                          (const char *)a->data, a->size);
        else if (strcmp(req.path, "/favicon.ico") == 0)
            send_response(fd, 204, "image/x-icon", NULL, "", 0);
        else
            send_json_error(fd, 404, "Not found");
    } else {
        static const char msg[] =
            "{\"success\":false,\"error\":\"Method not allowed\"}";
        send_response(fd, 405, "application/json; charset=utf-8", "GET",
                      msg, strlen(msg));
    }

    /* One short line per request; no patient data is ever logged. */
    printf("%s %s\n", req.method, req.path);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Main loop                                                           */
/* ------------------------------------------------------------------ */

static double monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int server_run(Simulation *sim, int port)
{
    struct sockaddr_in addr;
    struct sigaction sa;
    int listen_fd, yes = 1;
    char *buf;
    double last_tick;

    buf = malloc(REQUEST_BUF_SIZE);
    if (buf == NULL) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        free(buf);
        return 1;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);   /* local only */

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        if (errno == EADDRINUSE)
            fprintf(stderr,
                    "Error: port %d is already in use. Stop the other "
                    "program or choose another port with --port N.\n", port);
        else
            perror("bind");
        close(listen_fd);
        free(buf);
        return 1;
    }
    if (listen(listen_fd, 16) < 0) {
        perror("listen");
        close(listen_fd);
        free(buf);
        return 1;
    }

    printf("Hospital Priority Queue & Bed Scheduler\n");
    printf("Listening on http://127.0.0.1:%d  (Ctrl+C to stop)\n", port);
    fflush(stdout);

    last_tick = monotonic_seconds();
    while (!g_stop) {
        fd_set rfds;
        struct timeval tv;
        int ready;

        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        ready = select(listen_fd + 1, &rfds, NULL, NULL, &tv);

        /* Real-time auto progression, only while status == Running. */
        if (sim->status == SIM_RUNNING && sim->tick_seconds > 0) {
            double now = monotonic_seconds();
            while (now - last_tick >= sim->tick_seconds) {
                auto_tick(sim);
                last_tick += sim->tick_seconds;
            }
        } else {
            last_tick = monotonic_seconds();
        }

        if (ready > 0 && FD_ISSET(listen_fd, &rfds)) {
            struct timeval rcv = { 5, 0 };
            int fd = accept(listen_fd, NULL, NULL);
            if (fd < 0)
                continue;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof rcv);
            handle_connection(fd, sim, buf);
            close(fd);
        }
    }

    printf("\nShutting down.\n");
    close(listen_fd);
    free(buf);
    return 0;
}
