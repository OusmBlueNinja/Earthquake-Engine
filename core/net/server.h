#pragma once
#include <stdint.h>
#include <stddef.h>
#include "managers/cvar.h"
#include "sv_utils.h"

// 000.000.000.000 + null
#define MAX_IP_LEN 16

typedef struct server_s
{

    int port;
    host_t host;

} server_t;

typedef enum
{
    SV_STATUS_OK,
    SV_STATUS_FAILED_TO_INIT,
    SV_STATUS_FAILED_TO_BIND,

} sv_status_t;

sv_status_t sv_init();
void sv_shutdown();