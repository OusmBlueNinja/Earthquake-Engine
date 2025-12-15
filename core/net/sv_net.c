#include "sv_net.h"
#include "utils/logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

struct sv_net_server_t
{
    HANDLE thread;
    LONG running;
    SOCKET listen_sock;
    uint32_t host_be;
    uint16_t port;
};

static DWORD WINAPI sv_net_thread_main(LPVOID user)
{
    sv_net_server_t *s = (sv_net_server_t *)user;

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET)
    {
        LOG_ERROR("socket() failed");
        InterlockedExchange(&s->running, 0);
        return 1;
    }

    u_long nonblock = 1;
    ioctlsocket(ls, FIONBIO, &nonblock);

    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)s->port);
    addr.sin_addr.s_addr = s->host_be;

    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        LOG_ERROR("bind() failed");
        closesocket(ls);
        InterlockedExchange(&s->running, 0);
        return 2;
    }

    if (listen(ls, SOMAXCONN) == SOCKET_ERROR)
    {
        LOG_ERROR("listen() failed");
        closesocket(ls);
        InterlockedExchange(&s->running, 0);
        return 3;
    }

    s->listen_sock = ls;
    LOG_INFO("sv_net: server thread started");

    while (InterlockedCompareExchange(&s->running, 0, 0) == 1)
    {
        struct sockaddr_in client_addr;
        int client_len = (int)sizeof(client_addr);

        SOCKET c = accept(ls, (struct sockaddr *)&client_addr, &client_len);
        if (c == INVALID_SOCKET)
        {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK)
            {
                Sleep(1);
                continue;
            }
            Sleep(1);
            continue;
        }

        closesocket(c);
    }

    if (s->listen_sock != INVALID_SOCKET)
    {
        closesocket(s->listen_sock);
        s->listen_sock = INVALID_SOCKET;
    }

    LOG_INFO("sv_net: server thread stopped");
    return 0;
}

sv_net_status_t sv_net_init(void)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return SV_NET_ERR;
    return SV_NET_OK;
}

void sv_net_shutdown(void)
{
    WSACleanup();
}

sv_net_server_t *sv_net_server_create(void)
{
    sv_net_server_t *s = (sv_net_server_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return NULL;

    s->thread = NULL;
    s->listen_sock = INVALID_SOCKET;
    InterlockedExchange(&s->running, 0);
    return s;
}

void sv_net_server_destroy(sv_net_server_t *s)
{
    if (!s)
        return;

    sv_net_server_stop(s);
    HeapFree(GetProcessHeap(), 0, s);
}

sv_net_status_t sv_net_server_start(sv_net_server_t *s, uint32_t host_be, uint16_t port)
{
    if (!s)
        return SV_NET_ERR;

    s->host_be = host_be;
    s->port = port;

    InterlockedExchange(&s->running, 1);

    s->thread = CreateThread(NULL, 0, sv_net_thread_main, s, 0, NULL);
    if (!s->thread)
    {
        InterlockedExchange(&s->running, 0);
        return SV_NET_ERR;
    }

    return SV_NET_OK;
}

void sv_net_server_stop(sv_net_server_t *s)
{
    if (!s)
        return;

    if (InterlockedCompareExchange(&s->running, 0, 0) == 0)
        return;

    InterlockedExchange(&s->running, 0);

    if (s->listen_sock != INVALID_SOCKET)
        closesocket(s->listen_sock);

    if (s->thread)
    {
        WaitForSingleObject(s->thread, INFINITE);
        CloseHandle(s->thread);
        s->thread = NULL;
    }
}
