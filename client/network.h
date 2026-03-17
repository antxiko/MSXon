//=============================================================================
// network.h — Adaptador MSXgl UNAPI TCP
//
// Capa de abstracción sobre las funciones reales de MSXgl (unapi_tcp.h).
// Si MSXgl cambia nombres, solo hay que tocar este archivo.
//
// API real verificada contra:
//   MSXgl/engine/src/network/unapi_tcp.h
//=============================================================================
#ifndef NETWORK_H
#define NETWORK_H

#include "msxgl.h"
#include "network/unapi_tcp.h"

// ── Handle de conexión ──────────────────────────────────────────
// MSXgl usa int como handle de conexión TCP
#define NET_INVALID_CONN    (-1)
typedef int NetConn;

// ── Resultado de operaciones ────────────────────────────────────
#define NET_OK              1
#define NET_ERROR           0

// ── Struct reutilizable para consultas de estado TCP ────────────
// Se usa en Net_IsConnected, Net_Available y Net_Recv.
// Variable global para evitar reservar en stack (Z80 limitado).
static tcpip_unapi_tcp_conn_parms g_TcpParms;

//─────────────────────────────────────────────────────────────────
// Net_Init
// Busca implementaciones UNAPI TCP/IP instaladas (InterNestor, etc.)
// Devuelve NET_OK si hay al menos una, NET_ERROR si no.
//─────────────────────────────────────────────────────────────────
inline u8 Net_Init(void)
{
    return (tcpip_enumerate() > 0) ? NET_OK : NET_ERROR;
}

//─────────────────────────────────────────────────────────────────
// Net_Open
// Abre conexión TCP activa al servidor.
// ip: 4 bytes de IP (ej: {217,154,107,144})
// port: puerto TCP destino
// Devuelve handle de conexión o NET_INVALID_CONN.
//─────────────────────────────────────────────────────────────────
inline NetConn Net_Open(const u8* ip, u16 port)
{
    tcpip_unapi_tcp_conn_parms parms;
    int conn;

    // IP destino (4 bytes)
    parms.dest_ip[0] = ip[0];
    parms.dest_ip[1] = ip[1];
    parms.dest_ip[2] = ip[2];
    parms.dest_ip[3] = ip[3];

    // Puerto remoto
    parms.dest_port = (int)port;

    // Puerto local aleatorio
    parms.local_port = 0xFFFF;

    // Timeout generoso para Z80 + ObsoNET (en segundos, 0 = por defecto)
    parms.user_timeout = 0;

    // Flags: 0 = conexión activa transitoria
    parms.flags = CONNTYPE_TRANSIENT;

    if(tcpip_tcp_open(&parms, &conn) == ERR_OK)
    {
        return (NetConn)conn;
    }
    return NET_INVALID_CONN;
}

//─────────────────────────────────────────────────────────────────
// Net_Close
// Cierra la conexión TCP de forma ordenada (FIN).
//─────────────────────────────────────────────────────────────────
inline void Net_Close(NetConn conn)
{
    tcpip_tcp_close((int)conn);
}

//─────────────────────────────────────────────────────────────────
// Net_Abort
// Aborta la conexión TCP inmediatamente (RST).
// Usar solo en caso de error irrecuperable.
//─────────────────────────────────────────────────────────────────
inline void Net_Abort(NetConn conn)
{
    tcpip_tcp_abort((int)conn);
}

//─────────────────────────────────────────────────────────────────
// Net_IsConnected
// Devuelve TRUE si la conexión TCP está en estado ESTABLISHED.
//─────────────────────────────────────────────────────────────────
inline bool Net_IsConnected(NetConn conn)
{
    if(tcpip_tcp_state((int)conn, &g_TcpParms) != ERR_OK)
        return FALSE;
    return (g_TcpParms.conn_state == TCP_STATE_ESTABLISHED) ? TRUE : FALSE;
}

//─────────────────────────────────────────────────────────────────
// Net_Send
// Envía datos por la conexión TCP con flag PUSH.
// Devuelve NET_OK si el envío fue aceptado por el stack.
//─────────────────────────────────────────────────────────────────
inline u8 Net_Send(NetConn conn, const u8* data, u16 length)
{
    // flags bit 0 = PUSH (enviar inmediatamente, no acumular)
    return (tcpip_tcp_send((int)conn, (char*)data, (int)length, 1) == ERR_OK)
        ? NET_OK : NET_ERROR;
}

//─────────────────────────────────────────────────────────────────
// Net_Available
// Devuelve el número de bytes disponibles para leer (no bloqueante).
// Retorna 0 si no hay datos o si hay error.
//─────────────────────────────────────────────────────────────────
inline u16 Net_Available(NetConn conn)
{
    if(tcpip_tcp_state((int)conn, &g_TcpParms) != ERR_OK)
        return 0;
    return (u16)g_TcpParms.incoming_bytes;
}

//─────────────────────────────────────────────────────────────────
// Net_Recv
// Lee hasta 'maxLen' bytes del buffer de recepción.
// IMPORTANTE: Solo llamar si Net_Available() >= maxLen.
// Devuelve el número real de bytes leídos, o 0 en caso de error.
//─────────────────────────────────────────────────────────────────
inline u16 Net_Recv(NetConn conn, u8* buffer, u16 maxLen)
{
    if(tcpip_tcp_rcv((int)conn, (char*)buffer, (int)maxLen, &g_TcpParms) != ERR_OK)
        return 0;
    // Tras tcpip_tcp_rcv, incoming_bytes se actualiza con los bytes restantes.
    // Los bytes leídos = maxLen - bytes_restantes_despues... pero la API
    // no devuelve bytes leídos directamente. Asumimos que lee lo pedido
    // si hay datos suficientes (verificado con Net_Available antes).
    return maxLen;
}

//─────────────────────────────────────────────────────────────────
// Net_Flush
// Fuerza el envío de datos pendientes en el buffer de salida.
//─────────────────────────────────────────────────────────────────
inline void Net_Flush(NetConn conn)
{
    tcpip_tcp_flush((int)conn);
}

#endif // NETWORK_H
