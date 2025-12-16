#pragma once
#include <stdint.h>

typedef uint32_t sv_time_ms_t;

typedef enum sv_pkt_type_t
{
    SV_PKT_INVALID = 0,
    SV_PKT_CONNECT,
    SV_PKT_DISCONNECT,
    SV_PKT_PING,
    SV_PKT_PONG
} sv_pkt_type_t;

typedef enum sv_disc_reason_t
{
    SV_DISC_NONE = 0,
    SV_DISC_CLIENT_QUIT,
    SV_DISC_SERVER_SHUTDOWN,
    SV_DISC_PROTOCOL_ERROR,
    SV_DISC_TIMEOUT
} sv_disc_reason_t;

typedef struct sv_pkt_hdr_t
{
    uint16_t type;
    uint16_t size;
} sv_pkt_hdr_t;

typedef struct sv_pkt_connect_t
{
    sv_pkt_hdr_t hdr;
    uint32_t protocol_version;
    uint64_t client_nonce;
} sv_pkt_connect_t;

typedef struct sv_pkt_disconnect_t
{
    sv_pkt_hdr_t hdr;
    uint32_t reason;
} sv_pkt_disconnect_t;

typedef struct sv_pkt_ping_t
{
    sv_pkt_hdr_t hdr;
    sv_time_ms_t client_time;
    uint32_t seq;
} sv_pkt_ping_t;

typedef struct sv_pkt_pong_t
{
    sv_pkt_hdr_t hdr;
    sv_time_ms_t client_time;
    sv_time_ms_t server_time;
    uint32_t seq;
} sv_pkt_pong_t;
