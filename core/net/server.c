#include "server.h"
#include "sv_net.h"
#include "utils/logger.h"
#include <string.h>

static server_t g_server;

static server_cfg_t sv_read_cfg(void)
{
    server_cfg_t c;

    char buff[MAX_IP_LEN];
    cvar_get_string(SV_HOST, buff, sizeof(buff));
    c.host = char_to_host(buff, sizeof(buff));

    int port_i;
    cvar_get_int(SV_PORT, &port_i);
    if (port_i < 0)
        port_i = 0;
    if (port_i > SV_DEAFULT_PORT)
        port_i = SV_DEAFULT_PORT;
    c.port = (uint16_t)port_i;

    return c;
}

static bool sv_cfg_equal(server_cfg_t a, server_cfg_t b)
{
    return (a.host.ip_u32 == b.host.ip_u32) && (a.port == b.port);
}

sv_status_t sv_init(void)
{
    memset(&g_server, 0, sizeof(g_server));
    g_server.state = SV_STOPPED;
    g_server.cfg = sv_read_cfg();

    if (sv_net_init() != SV_NET_OK)
        return SV_STATUS_ERR;

    g_server.net = sv_net_server_create();
    if (!g_server.net)
        return SV_STATUS_ERR;

    return SV_STATUS_OK;
}

void sv_shutdown(void)
{
    sv_stop();

    if (g_server.net)
    {
        sv_net_server_destroy(g_server.net);
        g_server.net = NULL;
    }

    sv_net_shutdown();
    memset(&g_server, 0, sizeof(g_server));
}

sv_status_t sv_start(void)
{
    if (!g_server.net)
        return SV_STATUS_ERR;

    if (g_server.state == SV_RUNNING)
        return SV_STATUS_OK;

    if (sv_net_server_start(g_server.net, g_server.cfg.host.ip_u32, g_server.cfg.port) != SV_NET_OK)
        return SV_STATUS_ERR;

    g_server.state = SV_RUNNING;
    LOG_INFO("Server started");
    return SV_STATUS_OK;
}

void sv_stop(void)
{
    if (!g_server.net)
        return;

    if (g_server.state == SV_STOPPED)
        return;

    sv_net_server_stop(g_server.net);
    g_server.state = SV_STOPPED;
    LOG_INFO("Server stopped");
}

sv_status_t sv_reload(void)
{
    server_cfg_t new_cfg = sv_read_cfg();

    if (sv_cfg_equal(g_server.cfg, new_cfg))
        return SV_STATUS_OK;

    sv_run_state_t was_running = g_server.state;
    sv_stop();

    g_server.cfg = new_cfg;

    if (was_running == SV_RUNNING)
        return sv_start();

    return SV_STATUS_OK;
}

sv_run_state_t sv_state(void)
{
    return g_server.state;
}

server_cfg_t sv_config(void)
{
    return g_server.cfg;
}
