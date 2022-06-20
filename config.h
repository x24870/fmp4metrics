/*
 * Author: Pu-Chen Mao
 * Date:   2018/11/30
 * File:   config.h
 * Desc:   Environment variables configuration parser headers
 */

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "error.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool config_parse_path(const char *envname, char **path, uint64_t *interval_ms);

#ifdef __cplusplus
}
#endif

