#pragma once
#include <stdint.h>

typedef enum sv_pkt_type_t
{
    SV_PKT_INVALID = 0,
    SV_PKT_CONNECT = 1,
    SV_PKT_DISCONNECT = 2,
    SV_PKT_PING = 3,
    SV_PKT_PONG = 4
} sv_pkt_type_t;

typedef enum sv_disconnect_reason_t
{
    SV_DISC_NONE = 0,
    SV_DISC_CLIENT_QUIT = 1,
    SV_DISC_SERVER_SHUTDOWN = 2,
    SV_DISC_PROTOCOL_ERROR = 3,
    SV_DISC_TIMEOUT = 4
} sv_disconnect_reason_t;

typedef struct sv_pkt_connect_t
{
    uint32_t protocol_version;
    uint64_t client_nonce;
} sv_pkt_connect_t;

typedef struct sv_pkt_disconnect_t
{
    uint32_t reason;
} sv_pkt_disconnect_t;

typedef struct sv_pkt_ping_t
{
    uint32_t client_time_ms;
    uint32_t seq;
} sv_pkt_ping_t;

typedef struct sv_pkt_pong_t
{
    uint32_t client_time_ms;
    uint32_t server_time_ms;
    uint32_t seq;
} sv_pkt_pong_t;

typedef union sv_pkt_payload_u
{
    sv_pkt_connect_t connect;
    sv_pkt_disconnect_t disconnect;
    sv_pkt_ping_t ping;
    sv_pkt_pong_t pong;
} sv_pkt_payload_u;

typedef struct sv_packet_t
{
    sv_pkt_type_t type;
    sv_pkt_payload_u as;
} sv_packet_t;
