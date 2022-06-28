/*
 * Author: Mave Rick
 * Date:   2022/06/23
 * File:   q2q_wallclock_latency.c
 * Desc:   FMP4 stream queue-to-queue wallclock packet latency metric
 */

#include <fmp4.h>
#include <inttypes.h>

#include "error.h"
#include "metric.h"

/* Internal metric context */
typedef struct context_t
{
    uint64_t init_time_ms;
    uint64_t prev_time_ms;
    uint64_t cumulative_latency_ms;
    size_t   samples;

} context_t;

static metric_context_t q2q_wallclock_latency_context(error_context_t *errctx);
static bool q2q_wallclock_latency_emit(metric_context_t ctx,
        const fmp4_box_t *box, error_context_t *errctx);

static metric_t q2q_wallclock_latency =
{
    .envname = "QUEUE_TO_QUEUE_WALLCLOCK_LATENCY",
    .masks   = METRIC_MASK_SCRIPT,
    .context = q2q_wallclock_latency_context,
    .emit    = q2q_wallclock_latency_emit,
};

REGISTER_METRIC(q2q_wallclock_latency);

static metric_context_t
q2q_wallclock_latency_context(error_context_t *errctx)
{
    context_t *ctx = (context_t *)(calloc(1, sizeof(context_t)));
    error_save_retval_if(!ctx, errctx, errno, NULL);
    return (metric_context_t)(ctx);
}

static bool
q2q_wallclock_latency_emit(metric_context_t  ctx,
                           const fmp4_box_t  *box,
                           error_context_t  *errctx)
{
    context_t *metric_ctx = NULL;
    uint64_t   now_ms     = 0;
    uint64_t   stream_ms  = 0;
    uint64_t   average_ms = 0;
    uint64_t   diff_ms    = 0;
    int        ret        = -1;

    /* Sanity checks */
    if (!ctx || !box || !errctx)
        error_save_retval(errctx, EINVAL, false);

    /* Cast context to internal metric context */
    metric_ctx = (context_t *)(ctx);

    /* Check box */
    switch (ntohl(box->type))
    {
	    case 0x65677763: break; // egwc box
	    default: return true;
    }

    /* Extract wallclock timestamp, omit overflow samples due to clock skew */
    now_ms = current_time_milliseconds();
    stream_ms = fmp4_parse_wallclock(box->body,
            ntohl(box->size), errctx) / 1000;
    if (now_ms < stream_ms || stream_ms == 0)
        return true;

    /* Check if we're still within warmup period */
    if (metric_ctx->init_time_ms == 0)
        metric_ctx->init_time_ms = metric_ctx->prev_time_ms = now_ms;
    if (now_ms - metric_ctx->init_time_ms < q2q_wallclock_latency.interval_ms)
        return true;

    /* Calculate queue-to-queue wallclock latency */
    metric_ctx->cumulative_latency_ms += (now_ms - stream_ms);
    ++(metric_ctx->samples);

    /* Check if we are at the end of an interval time frame */
    diff_ms = now_ms - metric_ctx->prev_time_ms;
    if (diff_ms <= 0) return true;
    if (now_ms - metric_ctx->prev_time_ms >= q2q_wallclock_latency.interval_ms)
    {
        average_ms = metric_ctx->cumulative_latency_ms / metric_ctx->samples;
        metric_ctx->cumulative_latency_ms = 0;
        metric_ctx->samples = 0;
        metric_ctx->prev_time_ms = now_ms;
        ret = printf("%s %" PRIu64 " %" PRIu64 "\n", q2q_wallclock_latency.path,
                average_ms, now_ms / 1000);
        error_save_retval_if(ret < 0, errctx, errno, false);
    }

    return true;
}


