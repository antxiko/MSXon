//=============================================================================
// network.h — Adaptador MSXgl UNAPI TCP
//
// Capa de abstraccion sobre las funciones reales de MSXgl (unapi_tcp.h).
// Si MSXgl cambia nombres, solo hay que tocar este archivo.
//
// NOTA: NO usar inline — SDCC en Z80 corrompe el stack con structs
// grandes en funciones inlineadas. Usar variables globales para los
// structs UNAPI para evitar problemas de stack.
//
// API real verificada contra:
//   MSXgl/engine/src/network/unapi_tcp.h
//=============================================================================
#ifndef NETWORK_H
#define NETWORK_H

#include "msxgl.h"
#include "network/unapi_tcp.h"

// ── Handle de conexion ──────────────────────────────────────────
// MSXgl usa int como handle de conexion TCP
#define NET_INVALID_CONN    (-1)
typedef int NetConn;

// ── Resultado de operaciones ────────────────────────────────────
#define NET_OK              1
#define NET_ERROR           0

// ── Structs globales para llamadas UNAPI ────────────────────────
// En variables globales (BSS) para evitar stack overflow en Z80.
// NO declarar structs UNAPI como variables locales.
static tcpip_unapi_tcp_conn_parms g_TcpParms;
static tcpip_unapi_tcp_conn_parms g_OpenParms;
static tcpip_unapi_ip_info        g_IpInfo;

// ── Diagnostico ─────────────────────────────────────────────────
static u8  g_NetLastError   = 0;
static u8  g_NetImplCount   = 0;

//─────────────────────────────────────────────────────────────────
// Net_Init
// Busca implementaciones UNAPI TCP/IP instaladas (InterNestor, etc.)
// Devuelve NET_OK si hay al menos una, NET_ERROR si no.
//─────────────────────────────────────────────────────────────────
static u8 Net_Init(void)
{
    g_NetImplCount = (u8)tcpip_enumerate();
    return (g_NetImplCount > 0) ? NET_OK : NET_ERROR;
}

//─────────────────────────────────────────────────────────────────
// Net_Open
// Abre conexion TCP activa al servidor.
// ip: 4 bytes de IP (ej: {217,154,107,144})
// port: puerto TCP destino
// Devuelve handle de conexion o NET_INVALID_CONN.
//─────────────────────────────────────────────────────────────────
static NetConn Net_Open(const u8* ip, u16 port)
{
    int conn;
    int err;
    u8 i;
    u8* p;

    // Inicializar struct global a cero
    p = (u8*)&g_OpenParms;
    for(i = 0; i < sizeof(g_OpenParms); i++) p[i] = 0;

    // IP destino (4 bytes)
    g_OpenParms.dest_ip[0] = ip[0];
    g_OpenParms.dest_ip[1] = ip[1];
    g_OpenParms.dest_ip[2] = ip[2];
    g_OpenParms.dest_ip[3] = ip[3];

    // Puerto remoto
    g_OpenParms.dest_port = (int)port;

    // Puerto local aleatorio
    g_OpenParms.local_port = 0xFFFF;

    // Timeout (en segundos, 0 = por defecto del stack)
    g_OpenParms.user_timeout = 0;

    // Flags: 0 = conexion activa transitoria
    g_OpenParms.flags = CONNTYPE_TRANSIENT;

    conn = 0;
    err = tcpip_tcp_open(&g_OpenParms, &conn);
    g_NetLastError = (u8)err;

    if(err == ERR_OK)
    {
        return (NetConn)conn;
    }
    return NET_INVALID_CONN;
}

//─────────────────────────────────────────────────────────────────
// Net_GetLocalIP
// Obtiene la IP local del stack TCP/IP.
// Devuelve TRUE si hay IP asignada (no 0.0.0.0).
//─────────────────────────────────────────────────────────────────
static bool Net_GetLocalIP(u8* ipOut)
{
    if(tcpip_get_ipinfo(&g_IpInfo) != ERR_OK)
        return FALSE;
    ipOut[0] = g_IpInfo.local_ip[0];
    ipOut[1] = g_IpInfo.local_ip[1];
    ipOut[2] = g_IpInfo.local_ip[2];
    ipOut[3] = g_IpInfo.local_ip[3];
    return (ipOut[0] != 0 || ipOut[1] != 0 || ipOut[2] != 0 || ipOut[3] != 0)
        ? TRUE : FALSE;
}

//─────────────────────────────────────────────────────────────────
// Net_Close
// Cierra la conexion TCP de forma ordenada (FIN).
//─────────────────────────────────────────────────────────────────
static void Net_Close(NetConn conn)
{
    tcpip_tcp_close((int)conn);
}

//─────────────────────────────────────────────────────────────────
// Net_Abort
// Aborta la conexion TCP inmediatamente (RST).
//─────────────────────────────────────────────────────────────────
static void Net_Abort(NetConn conn)
{
    tcpip_tcp_abort((int)conn);
}

//─────────────────────────────────────────────────────────────────
// Net_IsConnected
// Devuelve TRUE si la conexion TCP esta en estado ESTABLISHED.
//─────────────────────────────────────────────────────────────────
static bool Net_IsConnected(NetConn conn)
{
    if(tcpip_tcp_state((int)conn, &g_TcpParms) != ERR_OK)
        return FALSE;
    return (g_TcpParms.conn_state == TCP_STATE_ESTABLISHED) ? TRUE : FALSE;
}

//─────────────────────────────────────────────────────────────────
// Net_Send
// Envia datos por la conexion TCP con flag PUSH.
// Devuelve NET_OK si el envio fue aceptado por el stack.
//─────────────────────────────────────────────────────────────────
static u8 Net_Send(NetConn conn, const u8* data, u16 length)
{
    return (tcpip_tcp_send((int)conn, (char*)data, (int)length, 1) == ERR_OK)
        ? NET_OK : NET_ERROR;
}

//─────────────────────────────────────────────────────────────────
// Net_Available
// Devuelve el numero de bytes disponibles para leer (no bloqueante).
// Retorna 0 si no hay datos o si hay error.
//─────────────────────────────────────────────────────────────────
static u16 Net_Available(NetConn conn)
{
    if(tcpip_tcp_state((int)conn, &g_TcpParms) != ERR_OK)
        return 0;
    return (u16)g_TcpParms.incoming_bytes;
}

//─────────────────────────────────────────────────────────────────
// Net_Recv
// Lee hasta 'maxLen' bytes del buffer de recepcion.
// IMPORTANTE: Solo llamar si Net_Available() >= maxLen.
// Devuelve el numero real de bytes leidos, o 0 en caso de error.
//─────────────────────────────────────────────────────────────────
static u16 Net_Recv(NetConn conn, u8* buffer, u16 maxLen)
{
    if(tcpip_tcp_rcv((int)conn, (char*)buffer, (int)maxLen, &g_TcpParms) != ERR_OK)
        return 0;
    return maxLen;
}

//─────────────────────────────────────────────────────────────────
// Net_Flush
// Fuerza el envio de datos pendientes en el buffer de salida.
//─────────────────────────────────────────────────────────────────
static void Net_Flush(NetConn conn)
{
    tcpip_tcp_flush((int)conn);
}

#endif // NETWORK_H
