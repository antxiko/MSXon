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

// ── Struct global unico para llamadas UNAPI TCP ─────────────────
// En variable global (BSS) para evitar stack overflow en Z80.
// NO declarar structs UNAPI como variables locales.
// Se usa tanto para Net_Open (lectura por tcpip_tcp_open) como para
// Net_IsConnected/Net_Available/Net_Recv (escritura por tcpip_tcp_state).
// Tras tcpip_tcp_open, el struct se puede reutilizar libremente.
static tcpip_unapi_tcp_conn_parms g_TcpParms;
static tcpip_unapi_ip_info        g_IpInfo;

// ── Resultado de tcpip_tcp_open (global para evitar stack corruption) ──
static int g_ConnResult     = 0;

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
    int err;
    u8 i;
    u8* p;

    // Inicializar struct global a cero
    p = (u8*)&g_TcpParms;
    for(i = 0; i < sizeof(g_TcpParms); i++) p[i] = 0;

    // IP destino (4 bytes)
    g_TcpParms.dest_ip[0] = ip[0];
    g_TcpParms.dest_ip[1] = ip[1];
    g_TcpParms.dest_ip[2] = ip[2];
    g_TcpParms.dest_ip[3] = ip[3];

    // Puerto remoto
    g_TcpParms.dest_port = (int)port;

    // Puerto local aleatorio
    g_TcpParms.local_port = 0xFFFF;

    // Timeout (en segundos, 0 = por defecto del stack)
    g_TcpParms.user_timeout = 0;

    // Flags: 0 = conexion activa transitoria
    g_TcpParms.flags = CONNTYPE_TRANSIENT;

    // conn en variable global para evitar corrupcion de stack
    g_ConnResult = 0;
    err = tcpip_tcp_open(&g_TcpParms, &g_ConnResult);
    g_NetLastError = (u8)err;

    if(err == ERR_OK)
    {
        return (NetConn)g_ConnResult;
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
// Net_GetConnState
// Consulta el estado TCP de la conexion.
// Devuelve el conn_state (TCP_STATE_*) o 0xFF si error en la llamada.
// Tras llamar, g_TcpParms.close_reason tiene el motivo de cierre.
//─────────────────────────────────────────────────────────────────
static u8 Net_GetConnState(NetConn conn)
{
    int err = tcpip_tcp_state((int)conn, &g_TcpParms);
    g_NetLastError = (u8)err;
    if(err != ERR_OK)
        return 0xFF;
    return (u8)g_TcpParms.conn_state;
}

//─────────────────────────────────────────────────────────────────
// Net_IsConnected
// Devuelve TRUE si la conexion TCP esta en estado ESTABLISHED.
//─────────────────────────────────────────────────────────────────
static bool Net_IsConnected(NetConn conn)
{
    return (Net_GetConnState(conn) == TCP_STATE_ESTABLISHED) ? TRUE : FALSE;
}

//─────────────────────────────────────────────────────────────────
// Net_Send
// Envia datos por la conexion TCP con flag PUSH.
// Devuelve NET_OK si el envio fue aceptado por el stack.
//─────────────────────────────────────────────────────────────────
static u8 Net_Send(NetConn conn, const u8* data, u16 length)
{
    int err = tcpip_tcp_send((int)conn, (char*)data, (int)length, 1);
    if(err != ERR_OK) return NET_ERROR;
    // Flush inmediato — InterNestor/GR8NET pueden bufferizar sin esto
    tcpip_tcp_flush((int)conn);
    return NET_OK;
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
