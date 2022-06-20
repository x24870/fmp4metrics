/*
 * Author: Pu-Chen Mao
 * Date:   2018/11/29
 * File:   metric.c
 * Desc:   FLV stream metric interface implementation
 */

#include "metric.h"

/* Global metric names, registry, and registered metrics count */
const char *metrics_supported[MAX_METRICS_COUNT] = {};
const metric_t *metrics_registry[MAX_METRICS_COUNT] = {};
size_t supported_count = 0, registered_count = 0;

bool
metrics_init(metric_context_t **metric_contexts,
             error_context_t   *errctx)
{
    size_t idx    = 0;
    bool   result = false;

    /* Sanity checks */
    if (!metric_contexts || !errctx)
        error_save_jump(errctx, EINVAL, CLEANUP);

    /* Allocate array for list of metric contexts */
    *metric_contexts = (metric_context_t *)(calloc(registered_count,
                sizeof(metric_context_t)));
    error_save_jump_if(!*metric_contexts, errctx, errno, CLEANUP);

    /* Generate contexts for each metric */
    for (idx = 0; idx < registered_count; idx++)
    {
        (*metric_contexts)[idx] = metrics_registry[idx]->context(errctx);
        error_save_jump_if(!(*metric_contexts)[idx], errctx, errno, CLEANUP);
    }

    result = true;

CLEANUP:

    if (!result)
        metrics_fini(metric_contexts);

    return result;
}

bool metric_config(metric_t *metric)
{
    const char *config = NULL;
    const char *comma  = NULL;
    int         ret    = -1;

    /* Sanity checks */
    if (!metric || !metric->envname)
        return false;

    /* Obtain environment variable value */
    config = getenv(metric->envname);
    if (!config)
        return false;

    /* Extract configuration values */
    comma = strchr(config, ',');
    if (!comma)
        return false;
    ret = snprintf(metric->path, sizeof(metric->path), "%.*s",
            (int)(comma - config), config);
    if (ret <= 0 || ret >= sizeof(metric->path))
        return false;
    metric->interval_ms = strtoull(comma + 1, NULL, 10);
    if (metric->interval_ms == ULLONG_MAX || metric->interval_ms == 0)
        return false;

    return true;
}

bool
metrics_feed_data(metric_context_t *metric_contexts,
                  const flv_tag_t  *tag,
                  error_context_t  *errctx)
{
    uint8_t mask = 0x00;
    size_t  idx  = 0;

    /* Sanity checks */
    if (!metric_contexts || !tag || !errctx)
        error_save_retval(errctx, EINVAL, false);

    /* Set mask of media tag */
    switch (tag->type)
    {
        case 4:   mask = METRIC_MASK_CONTROL; break;
        case 8:   mask = METRIC_MASK_AUDIO;   break;
        case 9:   mask = METRIC_MASK_VIDEO;   break;
        case 18:  mask = METRIC_MASK_SCRIPT;  break;
        case 101: mask = METRIC_MASK_TIME;    break;
        default:  mask = METRIC_MASK_UNKNOWN; break;
    }

    /* Cycle through and invoke emit function for each metric */
    for (idx = 0; idx < registered_count; idx++)
    {
        if (!metric_contexts[idx] || !(metrics_registry[idx]->masks & mask))
            continue;
        if (!metrics_registry[idx]->emit(metric_contexts[idx], tag, errctx))
            return false;
    }

    return true;
}

void metrics_fini(metric_context_t **metric_contexts)
{
    size_t idx = 0;

    /* Sanity checks */
    if (!metric_contexts || !*metric_contexts)
        return;

    /* Free allocated contexts */
    for (idx = 0; idx < registered_count; idx++)
        if ((*metric_contexts)[idx])
            FREE_AND_NULLIFY((*metric_contexts)[idx]);
    FREE_AND_NULLIFY(*metric_contexts);
}

