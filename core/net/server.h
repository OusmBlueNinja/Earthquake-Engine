#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sv_utils.h"

// 000.000.000.000 + null
#define MAX_IP_LEN 64
#define SV_DEAFULT_PORT 34434

typedef enum sv_status_t
{
    SV_STATUS_OK = 0,
    SV_STATUS_ERR = 1
} sv_status_t;

typedef enum sv_run_state_t
{
    SV_STOPPED = 0,
    SV_RUNNING = 1
} sv_run_state_t;

typedef struct server_cfg_t
{
    host_t host;
    uint16_t port;
} server_cfg_t;

typedef struct server_t
{
    server_cfg_t cfg;
    void *net;
    sv_run_state_t state;
} server_t;

sv_status_t sv_init(void);
void sv_shutdown(void);

sv_status_t sv_start(void);
void sv_stop(void);

sv_status_t sv_reload(void);

sv_run_state_t sv_state(void);
server_cfg_t sv_config(void);
