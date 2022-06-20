/*
 * Author: Mave Rick
 * Date:   2022/06/20
 * File:   metric.c
 * Desc:   FMP4 stream metric interface implementation
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
                  const fmp4_box_t  *box,
                  error_context_t  *errctx)
{
    size_t  idx  = 0;

    /* Sanity checks */
    if (!metric_contexts || !box || !errctx)
        error_save_retval(errctx, EINVAL, false);

    /* Set mask of media box */
    switch (ntohl(box->type))
    {
	case 0x66747970: /*fprintf(stderr, "box is ftyp\n");*/ break;
	case 0x6d6f6f76: /*fprintf(stderr, "box is moov\n");*/ break;
 	case 0x6d6f6f66: /*fprintf(stderr, "box is moof\n");*/ break;
	case 0x6d646174: /*fprintf(stderr, "box is mdat\n");*/ break;
	default: /*fprintf(stderr, "unknown box\n");*/ break;
    }

    /* Cycle through and invoke emit function for each metric */
    for (idx = 0; idx < registered_count; idx++)
    {
        if (!metrics_registry[idx]->emit(metric_contexts[idx], box, errctx))
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

