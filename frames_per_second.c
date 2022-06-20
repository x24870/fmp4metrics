/*
 * Author: Mave Rick
 * Date:   2022/06/20
 * File:   frames_per_second.c
 * Desc:   FMP4 stream frames-per-second metric
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
    size_t   audio_frames;
    size_t   video_frames;

} context_t;

static metric_context_t frames_per_second_context(error_context_t *errctx);
static bool frames_per_second_emit(metric_context_t ctx, const fmp4_box_t *box,
        error_context_t *errctx);

static metric_t frames_per_second =
{
    .envname = "FRAMES_PER_SECOND",
    .masks   = METRIC_MASK_AUDIO | METRIC_MASK_VIDEO,
    .context = frames_per_second_context,
    .emit    = frames_per_second_emit,
};

REGISTER_METRIC(frames_per_second);

static metric_context_t
frames_per_second_context(error_context_t *errctx)
{
    context_t *ctx = (context_t *)(calloc(1, sizeof(context_t)));
    error_save_retval_if(!ctx, errctx, errno, NULL);
    return (metric_context_t)(ctx);
}

static bool
frames_per_second_emit(metric_context_t  ctx,
                       const fmp4_box_t  *box,
                       error_context_t  *errctx)
{
    context_t *metric_ctx = NULL;
    uint64_t   now_ms     = 0;
    uint64_t   diff_ms    = 0;
    float      audio_fps  = 0;
    float      video_fps  = 0;
    int        ret        = -1;

    /* Sanity checks */
    if (!ctx || !box || !errctx)
        error_save_retval(errctx, EINVAL, false);

    /* Cast context to internal metric context */
    metric_ctx = (context_t *)(ctx);

    /* Increment the number of frames received */
    switch (box->type)
    {
        case 8: ++(metric_ctx->audio_frames); break;
        case 9: ++(metric_ctx->video_frames); break;
        default: return true;
    }

    /* Initialize tracking timestamps */
    now_ms = current_time_milliseconds();
    if (metric_ctx->init_time_ms == 0)
        metric_ctx->init_time_ms = metric_ctx->prev_time_ms = now_ms;

    /* Check if we're still within warmup period */
    if (now_ms - metric_ctx->init_time_ms < frames_per_second.interval_ms)
        return true;

    /* Check if we are at the end of an interval time frame */
    diff_ms = now_ms - metric_ctx->prev_time_ms;
    if (diff_ms <= 0) return true;
    if (diff_ms >= frames_per_second.interval_ms)
    {
        /* Calculate audio FPS */
        audio_fps = (float)(metric_ctx->audio_frames) * 1000 / (float)(diff_ms);
        ret = printf("%s.audio %.2f %" PRIu64 "\n",
                frames_per_second.path, audio_fps, now_ms / 1000);
        error_save_retval_if(ret < 0, errctx, errno, false);
        metric_ctx->audio_frames = 0;

        /* Calculate video FPS */
        video_fps = (float)(metric_ctx->video_frames) * 1000 / (float)(diff_ms);
        ret = printf("%s.video %.2f %" PRIu64 "\n",
                frames_per_second.path, video_fps, now_ms / 1000);
        error_save_retval_if(ret < 0, errctx, errno, false);
        metric_ctx->video_frames = 0;

        /* Reset previous time */
        metric_ctx->prev_time_ms = now_ms;
    }

    return true;
}

