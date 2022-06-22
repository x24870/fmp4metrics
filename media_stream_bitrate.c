/*
 * Author: Mave Rick
 * Date:   2022/06/20
 * File:   media_stream_bitrate.c
 * Desc:   FMP4 media stream bitrate metric
 */

#include <fmp4.h>
#include <inttypes.h>

#include "error.h"
#include "metric.h"

#define MAX_MEDIA_FRAME_SIZE (4 * 1024 * 1024)

/* Internal metric context */
typedef struct context_t
{
    uint64_t init_time_ms;
    uint64_t prev_time_ms;
    uint32_t nxt_mdat_track_id;
    size_t   audio_bytes;
    size_t   video_bytes;

} context_t;

static metric_context_t media_stream_bitrate_context(error_context_t *errctx);
static bool media_stream_bitrate_emit(metric_context_t ctx,
        const fmp4_box_t *box, error_context_t *errctx);

static metric_t media_stream_bitrate =
{
    .envname = "MEDIA_STREAM_BITRATE",
    .masks   = METRIC_MASK_AUDIO | METRIC_MASK_VIDEO,
    .context = media_stream_bitrate_context,
    .emit    = media_stream_bitrate_emit,
};

REGISTER_METRIC(media_stream_bitrate);

static metric_context_t
media_stream_bitrate_context(error_context_t *errctx)
{
    context_t *ctx = (context_t *)(calloc(1, sizeof(context_t)));
    error_save_retval_if(!ctx, errctx, errno, NULL);
    return (metric_context_t)(ctx);
}

static bool
media_stream_bitrate_emit(metric_context_t  ctx,
                          const fmp4_box_t  *box,
                          error_context_t  *errctx)
{
    context_t *metric_ctx = NULL;
    uint64_t   now_ms     = 0;
    uint64_t   diff_ms    = 0;
    float      audio_bps  = 0;
    float      video_bps  = 0;
    int        ret        = -1;

    /* Sanity checks */
    if (!ctx || !box || !errctx)
        error_save_retval(errctx, EINVAL, false);

    /* Cast context to internal metric context */
    metric_ctx = (context_t *)(ctx);

    /* Increment the number of bytes received */
    switch (ntohl(box->type))
    {
        case 0x6d6f6f66: { // moof
            fmp4_box_t* curBox;
            /* move to moof child - mfhd box */
            curBox = (fmp4_box_t*)box->body;
            /* move to mfhd sibling - traf box */
            curBox = (fmp4_box_t*)( (uint8_t*)curBox + ntohl(curBox->size) );
            /* move to traf child - tfhd */
            curBox = (fmp4_box_t*)curBox->body;
	    /* decide next mdat box track ID */
	    fmp4_full_box_t* tfhd = (fmp4_full_box_t*)curBox;
            uint32_t* track_id = (uint32_t*)tfhd->body;
            metric_ctx->nxt_mdat_track_id = ntohl(*track_id);
            break;
	}
        case 0x6d646174: { // mdat
	    int framesize = ntohl(box->size);
	    if (framesize <= 0 || framesize > MAX_MEDIA_FRAME_SIZE) {
	        return true;
	    }
	    if (metric_ctx->nxt_mdat_track_id == 1) {
	        metric_ctx->video_bytes += framesize;
	    } else if (metric_ctx->nxt_mdat_track_id == 2)  {
	        metric_ctx->audio_bytes += framesize;
	    }
	    break;
	}
	default: return true;
    }

    /* Initialize tracking timestamps */
    now_ms = current_time_milliseconds();
    if (metric_ctx->init_time_ms == 0)
        metric_ctx->init_time_ms = metric_ctx->prev_time_ms = now_ms;

    /* Check if we're still within warmup period */
    if (now_ms - metric_ctx->init_time_ms < media_stream_bitrate.interval_ms)
        return true;

    /* Check if we are at the end of an interval time frame */
    diff_ms = now_ms - metric_ctx->prev_time_ms;
    if (diff_ms <= 0) return true;
    if (diff_ms >= media_stream_bitrate.interval_ms)
    {
        /* Calculate audio bitrate */
        audio_bps = (float)(metric_ctx->audio_bytes) * 1000 / (float)(diff_ms);
        audio_bps *= 8; // Convert to bits per second
        ret = printf("%s.audio %.2f %" PRIu64 "\n",
                media_stream_bitrate.path, audio_bps, now_ms / 1000);
        error_save_retval_if(ret < 0, errctx, errno, false);
        metric_ctx->audio_bytes = 0;

        /* Calculate video bitrate */
        video_bps = (float)(metric_ctx->video_bytes) * 1000 / (float)(diff_ms);
        video_bps *= 8; // Convert to bits per second
        ret = printf("%s.video %.2f %" PRIu64 "\n",
                media_stream_bitrate.path, video_bps, now_ms / 1000);
        error_save_retval_if(ret < 0, errctx, errno, false);
        metric_ctx->video_bytes = 0;

        /* Reset previous time */
        metric_ctx->prev_time_ms = now_ms;
    }

    return true;
}


