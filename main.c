/*
 * Author: Mave Rick
 * Date:   2022/06/20
 * File:   main.c
 * Desc:   FMP4 stream metrics daemon
 */

#include <arpa/inet.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fmp4.h>

#include "error.h"
#include "metric.h"
#include "transport.h"

#define STREAM_TIMEOUT_MS     (60 * 1000)
#define RECONNECT_INTERVAL_MS (3000)
#define GRAFANA_TIMEOUT_SECS  (15)

/* Statistics Context */
typedef struct context_t {
    /* List of metrics contexts */
    metric_context_t *metric_contexts;

    /* Timestamps to track stream timeout */
    uint64_t last_callback_ms;

} context_t;

static void usage(const char *command);
static void signal_handler(int signum);
static bool grafana_connect(const char *sink, error_context_t *errctx);
static bool on_fmp4_box(const fmp4_box_t *box, void *userdata,
        error_context_t *errctx);
static bool timeout(const context_t *ctx);

/* Global variables & flags */
bool run = true;

int main(int argc, char *argv[])
{
    const char      *url    = NULL;
    const char      *sink   = NULL;
    fmp4_t          fmp4    = NULL;
    context_t       _ctx    = {};
    context_t       *ctx    = &_ctx;
    error_context_t _errctx = {};
    error_context_t *errctx = &_errctx;
    bool             result = false;

    if (argc != 3)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Setup signal handlers */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);

    /* Setup arguments */
    url = argv[1];
    sink = argv[2];

    /* Initialize metrics */
    if (!metrics_init(&(ctx->metric_contexts), errctx))
        error_save_jump(errctx, errno, CLEANUP);

    /* Main loop entry here */
    while (run)
    {
        do
        {
            /* Connect to Grafana daemon */
            if (!grafana_connect(sink, errctx))
                error_save_break(errctx, errno);

            /* Setup FMP4 stream context */
            fmp4 = fmp4_create(url, errctx);
            error_save_break_if(!fmp4, errctx, errno);

            /* Connect to FMP4 stream source */
            if (!fmp4_connect(fmp4, errctx))
                error_save_break(errctx, errno);

            /* Receive media frames for analysis & debug */
            ctx->last_callback_ms = current_time_milliseconds();
            while (run && fmp4_recv(fmp4, on_fmp4_box, ctx, errctx))
                error_save_break_if(timeout(ctx), errctx, ENODATA);
        }
        while (false);

        /* Output log if error occurred */
        error_log_saved(errctx);

        /* Release acquired resources */
        fmp4_destroy(&fmp4);

        /* Wait a little before reconnecting */
        if (run) usleep(RECONNECT_INTERVAL_MS * 1000);
    }

    result = !errctx->saved;

CLEANUP:

    /* Release resources acquired by metrics */
    metrics_fini(&(ctx->metric_contexts));

    /* Output log if error occurred */
    error_log_saved(errctx);

    return result ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void usage(const char *command)
{
    size_t idx = 0;

    /* Output build info, built-in settings, and usage */
    fprintf(stderr, "Build:\n\t%s @ %s\n"
        "\nBuilt-in Settings:\n"
        "\tSTREAM_TIMEOUT_MS:     %u\n"
        "\tRECONNECT_INTERVAL_MS: %u\n"
        "\tGRAFANA_TIMEOUT_SECS:  %u\n\n"
        "Usage:\n\t%s <URL> <sink address>\n\n",
        STRINGIFY(COMMIT_HASH),
        STRINGIFY(BUILD_TIME),
        STREAM_TIMEOUT_MS,
        RECONNECT_INTERVAL_MS,
        GRAFANA_TIMEOUT_SECS,
        command);

    /* Output supported metrics */
    fprintf(stderr, "Supported Metrics:\n");
    for (idx = 0; idx < supported_count; idx++)
        fprintf(stderr, "\t%s\n", metrics_supported[idx]);

    /* Output supported transports */
    fprintf(stderr, "\nSupported Transports:\n");
    for (idx = 0; idx < transport_count; idx++)
        fprintf(stderr, "\t%s: %s\n",
            transport_registry[idx]->name,
            transport_registry[idx]->desc);
}

static void signal_handler(int signum)
{
    run = false;
    fprintf(stderr, "\rReceived signal, stopping main loop...\n");
}

static bool
grafana_connect(const char      *sink,
                error_context_t *errctx)
{
    struct addrinfo  hints      = {};
    struct addrinfo *results    = NULL;
    struct addrinfo *idx        = NULL;
    struct timeval   timeout    = {};
    int              grafana_fd = -1;
    char             host[256]  = {0};
    char            *port       = NULL;
    char            *delim      = NULL;
    int              ret        = -1;
    bool             result     = false;

    /* Sanity checks */
    if (!sink || !errctx)
        error_save_jump(errctx, EINVAL, CLEANUP);

    /* If stdout is specified as sink, do nothing */
    if (strncmp(sink, "-", sizeof("-") - 1) == 0)
        return true;

    /* Extract host and port from sink string */
    (void)strncpy(host, sink, sizeof(host) - 1);
    delim = strchr(host, ':');
    error_save_jump_if(!delim, errctx, EINVAL, CLEANUP);
    *delim = '\0';
    port = delim + 1;

    /* Prepare name lookup hints */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    /* Lookup host address */
    ret = getaddrinfo(host, port, &hints, &results);
    error_save_jump_if(ret < 0, errctx, errno, CLEANUP);

    /* Create socket descriptor with lookup results */
    for (idx = results; idx; idx = idx->ai_next)
    {
        grafana_fd = socket(idx->ai_family, idx->ai_socktype, idx->ai_protocol);
        if (grafana_fd >= 0)
            break;
    }
    error_save_jump_if(grafana_fd < 0, errctx, errno, CLEANUP);

    /* Configure Grafana connection timeout */
    timeout.tv_sec = GRAFANA_TIMEOUT_SECS;
    timeout.tv_usec = 0;
    ret = setsockopt(grafana_fd, SOL_SOCKET, SO_RCVTIMEO,
            (char *)(&timeout), sizeof(timeout));
    error_save_jump_if(ret < 0, errctx, errno, CLEANUP);
    ret = setsockopt(grafana_fd, SOL_SOCKET, SO_SNDTIMEO,
            (char *)(&timeout), sizeof(timeout));
    error_save_jump_if(ret < 0, errctx, errno, CLEANUP);

    /* Connect to host */
    error_save_jump_if(grafana_fd <= 0, errctx, errno, CLEANUP);
    ret = connect(grafana_fd, idx->ai_addr, idx->ai_addrlen);
    error_save_jump_if(ret < 0, errctx, errno, CLEANUP);

    /* Replace current stdout with connection */
    ret = dup2(grafana_fd, STDOUT_FILENO);
    error_save_jump_if(ret < 0, errctx, errno, CLEANUP);
    setlinebuf(stdout);

    result = true;

CLEANUP:

    if (results)
        freeaddrinfo(results);
    results = NULL;
    close(grafana_fd);
    grafana_fd = -1;

    return result;
}

static bool
on_fmp4_box(const fmp4_box_t *box,
           void            *userdata,
           error_context_t *errctx)
{
    context_t *ctx = NULL;

    /* Sanity checks */
    if (!box || !userdata || !errctx)
        error_save_retval(errctx, EINVAL, false);

    /* Cast userdata to context pointer */
    ctx = (context_t *)(userdata);

    /* Feed FMP4 box data to metrics */
    if (!metrics_feed_data(ctx->metric_contexts, box, errctx))
        return false;

    /* Update stream callback timestamp */
    ctx->last_callback_ms = current_time_milliseconds();

    return true;
}

static bool timeout(const context_t *ctx)
{
    uint64_t now_ms  = 0;
    uint64_t diff_ms = 0;

    /* Sanity checks */
    if (!ctx)
        return true;

    /* Calculate & check elapsed time */
    now_ms = current_time_milliseconds();
    diff_ms = now_ms - ctx->last_callback_ms;
    if (diff_ms > STREAM_TIMEOUT_MS)
        return true;

    return false;
}

