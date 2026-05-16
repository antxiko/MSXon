//=============================================================================
// game_runtime.c — Implementación
//=============================================================================
#include "game_runtime.h"
#include "lobby_client.h"
#include "protocol.h"
#include "bios.h"

GameContext g_Game;

static u8 g_GRT_SendBuf[6 + GAMERT_PAYLOAD_MAX];
static u8 g_GRT_RecvHdr[6];
static u8 g_GRT_RecvPl[GAMERT_PAYLOAD_MAX];

bool GameRT_Init(void)
{
    if (!LobbyClient_Load()) return FALSE;
    g_Game.conn     = (NetConn)(int)g_LobbyData.conn;
    g_Game.pid      = g_LobbyData.pid;
    g_Game.roomId   = g_LobbyData.roomId;
    g_Game.active   = g_LobbyData.active;
    g_Game.gameId   = g_LobbyData.gameId;
    g_Game.protoVer = g_LobbyData.protoVer;
    Net_Init();
    return TRUE;
}

void GameRT_Poll(GameRT_PacketCb cb, u8 maxPackets)
{
    if (g_Game.conn == NET_INVALID_CONN) return;
    while (maxPackets--) {
        u16 avail = Net_Available(g_Game.conn);
        if (avail < 6) break;
        Net_Recv(g_Game.conn, g_GRT_RecvHdr, 6);
        if (g_GRT_RecvHdr[0] != PROTO_MAGIC_0 || g_GRT_RecvHdr[1] != PROTO_MAGIC_1) break;
        u8 plLen = g_GRT_RecvHdr[5];
        if (plLen > 0) {
            avail = Net_Available(g_Game.conn);
            if (avail < plLen) break;
            Net_Recv(g_Game.conn, g_GRT_RecvPl, plLen);
        }
        cb(g_GRT_RecvHdr[2], g_GRT_RecvHdr[4], g_GRT_RecvPl, plLen);
    }
}

void GameRT_Send(u8 cmd, const u8* payload, u8 len)
{
    if (g_Game.conn == NET_INVALID_CONN) return;
    g_GRT_SendBuf[0] = PROTO_MAGIC_0;
    g_GRT_SendBuf[1] = PROTO_MAGIC_1;
    g_GRT_SendBuf[2] = cmd;
    g_GRT_SendBuf[3] = g_Game.roomId;
    g_GRT_SendBuf[4] = g_Game.pid;
    g_GRT_SendBuf[5] = len;
    if (payload && len > 0) {
        u8 i;
        for (i = 0; i < len; i++) g_GRT_SendBuf[6 + i] = payload[i];
    }
    Net_Send(g_Game.conn, g_GRT_SendBuf, 6 + len);
}

void GameRT_SendPing(void)
{
    GameRT_Send(CMD_PING, 0, 0);
}

u8 GameRT_ActiveCount(void)
{
    u8 cnt = 0;
    u8 m = g_Game.active;
    while (m) { cnt += (m & 1); m >>= 1; }
    return cnt;
}

void GameRT_ExitToLobby(void)
{
    // Cerrar conexión TCP por limpieza (libera el slot en el server)
    if (g_Game.conn != NET_INVALID_CONN) {
        Net_Close(g_Game.conn);
        g_Game.conn = NET_INVALID_CONN;
    }
    // Keyboard stuffing: escribimos "MSXON\r" al buffer del teclado MSX
    // (0xFBF0..0xFC0F) y ajustamos PUTPNT/GETPNT. Al salir con Bios_Exit,
    // el shell de MSX-DOS lo lee como si lo hubiera tipeado el usuario.
    {
        u8* buf = (u8*)0xFBF0;
        buf[0] = 'M'; buf[1] = 'S'; buf[2] = 'X';
        buf[3] = 'O'; buf[4] = 'N'; buf[5] = 0x0D;
        *((u16*)0xF3FA) = (u16)buf;          // GETPNT = inicio
        *((u16*)0xF3F8) = (u16)buf + 6;      // PUTPNT = fin
    }
    Bios_Exit(0);
}
