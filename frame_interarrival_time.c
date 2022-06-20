/*
 * Author: Mave Rick
 * Date:   2022/06/20
 * File:   frame_interarrival_time.c
 * Desc:   FMP4 media frame inter-arrival time metric
 */

#include <fmp4.h>
#include <inttypes.h>
#include <math.h>

#include "error.h"
#include "metric.h"

/* Internal metric context */
typedef struct context_t
{
    uint64_t prev_time_ms;
    uint64_t prev_audio_ms;
    uint64_t prev_video_ms;
    uint64_t audio_max_interarrival_ms;
    uint64_t video_max_interarrival_ms;

} context_t;

static metric_context_t frame_interarrival_time_context(error_context_t *errctx);
static bool frame_interarrival_time_emit(metric_context_t ctx,
        const fmp4_box_t *box, error_context_t *errctx);

static metric_t frame_interarrival_time =
{
    .envname = "FRAME_INTERARRIVAL_TIME",
    .masks   = METRIC_MASK_AUDIO | METRIC_MASK_VIDEO,
    .context = frame_interarrival_time_context,
    .emit    = frame_interarrival_time_emit,
};

REGISTER_METRIC(frame_interarrival_time);

static metric_context_t
frame_interarrival_time_context(error_context_t *errctx)
{
    context_t *ctx = (context_t *)(calloc(1, sizeof(context_t)));
    error_save_retval_if(!ctx, errctx, errno, NULL);
    return (metric_context_t)(ctx);
}

static bool
frame_interarrival_time_emit(metric_context_t  ctx,
                             const fmp4_box_t  *box,
                             error_context_t  *errctx)
{
    context_t *metric_ctx    = NULL;
    uint64_t   now_ms        = 0;
    uint64_t   diff_ms       = 0;
    uint64_t  *prev_media_ms = 0;
    uint64_t  *media_max_ms  = 0;
    int        ret           = -1;

    /* Sanity checks */
    if (!ctx || !box || !errctx)
        error_save_retval(errctx, EINVAL, false);

    /* Cast context to internal metric context */
    metric_ctx = (context_t *)(ctx);

    /* Setup timing variables according to media type */
    switch (ntohl(box->type))
    {
	case 0x6d6f6f66: break; // moof box
	default: return true;
    }

    /* move to moof child - mfhd box */
    fmp4_box_t* curBox = (fmp4_box_t*)box->body;
    /* move to mfhd sibling - traf box */
    curBox = (fmp4_box_t*)( (uint8_t*)curBox + ntohl(curBox->size) );
    /* move to traf child - tfhd */
    curBox = (fmp4_box_t*)curBox->body;

    /* check this track is video or audio */
    fmp4_full_box_t* tfhd = (fmp4_full_box_t*)curBox;
    uint32_t* track_id = (uint32_t*)tfhd->body;
    if (ntohl(*track_id) == 1) { 
        prev_media_ms = &(metric_ctx->prev_video_ms);
	media_max_ms = &(metric_ctx->video_max_interarrival_ms);
    } else if (ntohl(*track_id) == 2) { 
        prev_media_ms = &(metric_ctx->prev_audio_ms);
	media_max_ms = &(metric_ctx->audio_max_interarrival_ms);
    }

    /* Initialize tracking timestamps */
    now_ms = current_time_milliseconds();
    if (metric_ctx->prev_time_ms == 0)
        metric_ctx->prev_time_ms = now_ms;

    /* Update minimum & maximum interarrival times */
    if (*prev_media_ms == 0) *prev_media_ms = now_ms;
    if (*prev_media_ms > now_ms) return true; // redundant
    diff_ms = now_ms - *prev_media_ms;
    *media_max_ms = MAX(*media_max_ms, diff_ms);
    *prev_media_ms = now_ms;

    /* Check if we are at the end of an interval time frame */
    diff_ms = now_ms - metric_ctx->prev_time_ms;
    if (diff_ms <= 0) return true;
    if (diff_ms >= frame_interarrival_time.interval_ms)
    {
        /* Output audio minimum & maximum interarrival times */
        ret = printf("%s.audio.max %" PRIu64 " %" PRIu64 "\n",
                frame_interarrival_time.path,
                metric_ctx->audio_max_interarrival_ms,
                now_ms / 1000);
        error_save_retval_if(ret < 0, errctx, errno, false);
        metric_ctx->audio_max_interarrival_ms = 0;

        /* Output video minimum & maximum interarrival times */
        ret = printf("%s.video.max %" PRIu64 " %" PRIu64 "\n",
                frame_interarrival_time.path,
                metric_ctx->video_max_interarrival_ms,
                now_ms / 1000);
        error_save_retval_if(ret < 0, errctx, errno, false);
        metric_ctx->video_max_interarrival_ms = 0;

        /* Reset previous time */
        metric_ctx->prev_time_ms = now_ms;
    }

    return true;
}


