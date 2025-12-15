#pragma once
#include <stdint.h>

typedef enum sv_net_status_t
{
    SV_NET_OK = 0,
    SV_NET_ERR = 1
} sv_net_status_t;

typedef struct sv_net_server_t sv_net_server_t;

sv_net_status_t sv_net_init(void);
void sv_net_shutdown(void);

sv_net_server_t *sv_net_server_create(void);
void sv_net_server_destroy(sv_net_server_t *s);

sv_net_status_t sv_net_server_start(sv_net_server_t *s, uint32_t host_be, uint16_t port);
void sv_net_server_stop(sv_net_server_t *s);
