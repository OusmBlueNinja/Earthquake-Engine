#include "server.h"
#include "utils/logger.h"
#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

static server_t g_server;

static void sv_load_cvars()
{

    // Read SV config from cvars
    char buff[MAX_IP_LEN]; // MAX_IP_LEN defined in server.h
    cvar_get_string(SV_HOST, buff, sizeof(buff));
    g_server.host = char_to_host(buff, sizeof(buff));

    int port;
    cvar_get_int(SV_PORT, &port);
    g_server.port = port;

    return;
}

sv_status_t sv_init(void)
{
    LOG_INFO("Initalizing Server");
    sv_load_cvars();

    return SV_STATUS_OK;
}

void sv_shutdown()
{
    LOG_INFO("Shutting down Server");
    return;
}