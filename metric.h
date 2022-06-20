/*
 * Author: Mave Rick
 * Date:   2022/06/20
 * File:   metric.h
 * Desc:   FMP4 stream metric interface header
 */

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <fmp4.h>

#include "common.h"
#include "error.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Metric-specific tag payload types */
    #define METRIC_MASK_CONTROL 0x01
    #define METRIC_MASK_AUDIO   0x02
    #define METRIC_MASK_VIDEO   0x04
    #define METRIC_MASK_SCRIPT  0x08
    #define METRIC_MASK_TIME    0x10
    #define METRIC_MASK_UNKNOWN 0x20

    /* Maximum length of Grafana path of metric */
    #define MAX_PATH_LEN 256

    /* Per-metric module registration function */
    #define REGISTER_METRIC(metric) \
        __attribute__((constructor)) static void register_##metric() \
        { \
            assert(supported_count < MAX_METRICS_COUNT); \
            assert(registered_count < MAX_METRICS_COUNT); \
            assert(metrics_registry[registered_count] == NULL); \
            assert(metric.envname!= NULL); \
            assert(metric.context != NULL); \
            assert(metric.emit != NULL); \
            if (metric_config(&metric)) \
                metrics_registry[registered_count++] = &metric; \
            metrics_supported[supported_count++] = metric.envname; \
        }

    /* Per-metric implementation function pointers types */
    typedef void * metric_context_t;
    typedef metric_context_t (*metric_context_functor_t)(
            error_context_t *errctx); // allocated context needs be free()'d
    typedef bool (*metric_emit_functor_t)(metric_context_t ctx,
            const fmp4_box_t *box, error_context_t *errctx);

    /* Transport context definition */
    typedef struct metric_t
    {
        const char                     *envname;
        char                            path[MAX_PATH_LEN + 1];
        uint64_t                        interval_ms;
        const uint8_t                   masks;
        const metric_context_functor_t  context;
        const metric_emit_functor_t     emit;

    } metric_t;

    /* Global metric names, registry, and registered metrics count */
    #define MAX_METRICS_COUNT 256
    extern const char *metrics_supported[MAX_METRICS_COUNT];
    extern const metric_t *metrics_registry[MAX_METRICS_COUNT];
    extern size_t supported_count, registered_count;

    /* Exported public functions */
    bool metrics_init(metric_context_t **metric_contexts,
            error_context_t *errctx);
    bool metric_config(metric_t *metric);
    bool metrics_feed_data(metric_context_t *metric_contexts,
            const fmp4_box_t *box, error_context_t *errctx);
    void metrics_fini(metric_context_t **metric_contexts);

#ifdef __cplusplus
}
#endif

