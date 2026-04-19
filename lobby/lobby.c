// lobby.c — MSXon Universal Lobby Module
// Extracted from texas.c (most updated pattern)
#include "lobby.h"
#include "dos.h"
#include "input.h"

// ── Internal state ──────────────────────────────────────────────────

static const LobbyConfig* g_Cfg;
static LobbyPacketCB g_GameCB;

// Exports
NetConn g_LobbyConn = NET_INVALID_CONN;
u8 g_LobbyPid = 0;
u8 g_LobbyRoomId = 0;
u8 g_LobbyActive = 0;
u8 g_LobbyState = LOBBY_ST_LIST_WAIT;
bool g_LobbyOnline = FALSE;
u8 g_LobbySendBuf[20];

// Lobby rooms
#define LB_MAX 20
typedef struct { u8 rid, gid, np; } LBRoom;
static LBRoom g_LB[LB_MAX];
static u8 g_LBN = 0, g_LBC = 0;

// Key flags
static u8 g_KUp=0,g_KDown=0,g_KRet=0,g_KC=0,g_KR=0,g_KS=0;
static u8 g_MvDly = 0;

// Ping
#define PING_INT 250
static u16 g_PingT = 0;

// ── Helpers ─────────────────────────────────────────────────────────

#define NB_SET(x,y,v) do{u16 _i=(u16)(y)*32+(x);u8 _v=(v);if(g_NB[_i]!=_v)g_NB[_i]=_v;}while(0)

// Tile indices (font assumed at same positions across all games)
// Games must have: T_FA=font A offset, T_F0=font 0 offset
// We use a simple approach: each game defines these in their tileset
// For lobby screens we use a minimal tile set
// The game must have tiles for A-Z starting at some offset and 0-9 at another
// We'll parameterize later if needed — for now assume same as texas:
// A-Z at offset 52..77 is too game-specific
// SOLUTION: lobby uses raw tile 0 for black and writes text to name buffer
// The game's NB_Text function handles tile mapping
// We provide our own minimal NB_Text that the game can override

// Generic font tile offsets (lobby uses these, game can have different)
// These match the damas/parchis pattern: A=97 in some, A=52 in others
// We'll use a simple A=8 like tetris... but that varies too
// PRAGMATIC: lobby.c has its own NB_Text with configurable offsets

static u8 g_TileFA = 52;  // default: A at 52 (like texas)
static u8 g_TileF0 = 78;  // default: 0 at 78 (like texas)

void Lobby_SetTileOffsets(u8 fontA, u8 font0)
{
    g_TileFA = fontA;
    g_TileF0 = font0;
}

void Lobby_NB_Text(u8 x, u8 y, const c8* s)
{
    while(*s && x < 32) {
        u8 ch = (u8)*s, t;
        if(ch >= 'A' && ch <= 'Z') t = g_TileFA + ch - 'A';
        else if(ch >= '0' && ch <= '9') t = g_TileF0 + ch - '0';
        else if(ch == '>') t = g_TileFA + 18; // use 'S' as cursor placeholder
        else t = 0; // black tile for space and unknown
        NB_SET(x, y, t);
        x++; s++;
    }
}

void Lobby_NB_Num(u8 x, u8 y, u16 v)
{
    c8 b[6]; u8 n=0, i; u16 d=10000;
    for(i=0; i<5; i++) {
        u8 g = (u8)(v / d); v %= d; d /= 10;
        if(g || n || i == 4) b[n++] = '0' + g;
    }
    b[n] = 0;
    Lobby_NB_Text(x, y, b);
}

void Lobby_NB_ClearRow(u8 y)
{
    u8 x;
    for(x = 0; x < 32; x++) NB_SET(x, y, 0);
}

// ── Init ────────────────────────────────────────────────────────────

void Lobby_Init(const LobbyConfig* cfg, LobbyPacketCB gameCB)
{
    g_Cfg = cfg;
    g_GameCB = gameCB;
    g_LobbyState = LOBBY_ST_LIST_WAIT;
}

// ── Diagnostics (Screen 0) ──────────────────────────────────────────

static void Diag_PrintDec(u8 val)
{
    u8 h = val / 100, t = (val % 100) / 10, u = val % 10;
    if(h) DOS_CharOutput('0' + h);
    if(h || t) DOS_CharOutput('0' + t);
    DOS_CharOutput('0' + u);
}

static void Diag_PrintIP(const u8* ip)
{
    u8 i;
    for(i = 0; i < 4; i++) { Diag_PrintDec(ip[i]); if(i < 3) DOS_CharOutput('.'); }
}

void Lobby_Diag(void)
{
    u8 localIP[4];
    u8 ok;

    DOS_StringOutput("================================\r\n$");
    DOS_StringOutput("   $");
    // Print game name char by char (it's null-terminated, not $-terminated)
    {
        const c8* p = g_Cfg->gameName;
        while(*p) { DOS_CharOutput(*p); p++; }
    }
    DOS_StringOutput("\r\n$");
    DOS_StringOutput("================================\r\n\r\n$");

    DOS_StringOutput("UNAPI TCP/IP: $");
    ok = (tcpip_enumerate() > 0) ? 1 : 0;
    if(ok)
        DOS_StringOutput("ENCONTRADO\r\n$");
    else
    {
        DOS_StringOutput("NO ENCONTRADO\r\n\r\n$");
        DOS_StringOutput("Modo OFFLINE.\r\n$");
        DOS_StringOutput("Pulsa ESPACIO para continuar...$");
        DOS_CharInput();
        return;
    }

    tcpip_get_ipinfo(&g_IpInfo);

    DOS_StringOutput("IP local    : $");
    localIP[0] = (u8)g_IpInfo.local_ip[0]; localIP[1] = (u8)g_IpInfo.local_ip[1];
    localIP[2] = (u8)g_IpInfo.local_ip[2]; localIP[3] = (u8)g_IpInfo.local_ip[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("Mascara     : $");
    localIP[0] = (u8)g_IpInfo.subnet_mask[0]; localIP[1] = (u8)g_IpInfo.subnet_mask[1];
    localIP[2] = (u8)g_IpInfo.subnet_mask[2]; localIP[3] = (u8)g_IpInfo.subnet_mask[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("Gateway     : $");
    localIP[0] = (u8)g_IpInfo.gateway_ip[0]; localIP[1] = (u8)g_IpInfo.gateway_ip[1];
    localIP[2] = (u8)g_IpInfo.gateway_ip[2]; localIP[3] = (u8)g_IpInfo.gateway_ip[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("\r\nServidor    : $");
    Diag_PrintIP(g_Cfg->serverIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("Puerto      : $");
    {
        u16 port = g_Cfg->serverPort;
        u8 d[5], i, started;
        d[0] = (u8)(port / 10000); port %= 10000;
        d[1] = (u8)(port / 1000);  port %= 1000;
        d[2] = (u8)(port / 100);   port %= 100;
        d[3] = (u8)(port / 10);    port %= 10;
        d[4] = (u8)(port);
        started = 0;
        for(i = 0; i < 5; i++) {
            if(d[i] > 0 || started || i == 4) { DOS_CharOutput('0' + d[i]); started = 1; }
        }
    }
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("\r\nPulsa ESPACIO para continuar...$");
    DOS_CharInput();
    DOS_StringOutput("\r\n$");
}

// ── Connection ──────────────────────────────────────────────────────

static void Net_Wait50(void) { u8 w; for(w = 0; w < 25; w++) Halt(); }

bool Lobby_Connect(void)
{
    u8 tcpState;
    u16 timeout;

    Log_Init();
    Log_Write("[INIT] Lobby connect");

    if(Net_Init() != NET_OK) { Log_Write("[CONN] No UNAPI"); return FALSE; }
    Log_WriteHex("[CONN] UNAPI OK impl=", g_NetImplCount);

    Net_Wait50();
    tcpip_get_ipinfo(&g_IpInfo);
    Net_Wait50();

    Log_Write("[CONN] Abriendo TCP...");
    g_LobbyConn = Net_Open(g_Cfg->serverIP, g_Cfg->serverPort);
    if(g_LobbyConn == NET_INVALID_CONN) {
        Log_WriteHex("[CONN] Fallo err=", g_NetLastError);
        return FALSE;
    }

    timeout = 0;
    while(timeout < 500) {
        Halt();
        tcpState = Net_GetConnState(g_LobbyConn);
        if(tcpState == TCP_STATE_ESTABLISHED) break;
        if(tcpState == 0xFF) return FALSE;
        timeout++;
    }
    if(timeout >= 500) return FALSE;
    Log_Write("[CONN] ESTABLISHED");

    Net_Wait50();

    {
        u8 token[4];
        token[0] = AUTH_TOKEN_0; token[1] = AUTH_TOKEN_1;
        token[2] = AUTH_TOKEN_2; token[3] = AUTH_TOKEN_3;
        g_LobbySendBuf[0] = PROTO_MAGIC_0; g_LobbySendBuf[1] = PROTO_MAGIC_1;
        g_LobbySendBuf[2] = CMD_AUTH; g_LobbySendBuf[3] = 0;
        g_LobbySendBuf[4] = 0; g_LobbySendBuf[5] = 4;
        g_LobbySendBuf[6] = token[0]; g_LobbySendBuf[7] = token[1];
        g_LobbySendBuf[8] = token[2]; g_LobbySendBuf[9] = token[3];
        Net_Send(g_LobbyConn, g_LobbySendBuf, 10);
        Log_Write("[AUTH] Enviado");
    }

    timeout = 0;
    while(timeout < 250) {
        u16 avail;
        Halt();
        avail = Net_Available(g_LobbyConn);
        if(avail >= 6) {
            u8 hdr[6];
            Net_Recv(g_LobbyConn, hdr, 6);
            if(hdr[2] == CMD_AUTH_OK) { Log_Write("[AUTH] OK"); break; }
            if(hdr[2] == CMD_AUTH_FAIL) return FALSE;
        }
        timeout++;
    }
    if(timeout >= 250) return FALSE;

    g_LobbyOnline = TRUE;
    return TRUE;
}

// ── Room operations ─────────────────────────────────────────────────

void Lobby_RequestRooms(void)
{
    if(g_LobbyConn == NET_INVALID_CONN) return;
    g_LobbySendBuf[0]=PROTO_MAGIC_0; g_LobbySendBuf[1]=PROTO_MAGIC_1;
    g_LobbySendBuf[2]=CMD_ROOM_LIST; g_LobbySendBuf[3]=0;
    g_LobbySendBuf[4]=0; g_LobbySendBuf[5]=0;
    Net_Send(g_LobbyConn, g_LobbySendBuf, 6);
}

void Lobby_CreateRoom(void)
{
    if(g_LobbyConn == NET_INVALID_CONN) return;
    g_LobbySendBuf[0]=PROTO_MAGIC_0; g_LobbySendBuf[1]=PROTO_MAGIC_1;
    g_LobbySendBuf[2]=CMD_ROOM_CREATE; g_LobbySendBuf[3]=0;
    g_LobbySendBuf[4]=0; g_LobbySendBuf[5]=3;
    g_LobbySendBuf[6]=g_Cfg->gameId;
    g_LobbySendBuf[7]=g_Cfg->maxPlayers;
    g_LobbySendBuf[8]=0x01; // RELAY
    Net_Send(g_LobbyConn, g_LobbySendBuf, 9);
}

void Lobby_JoinRoom(u8 roomId)
{
    if(g_LobbyConn == NET_INVALID_CONN) return;
    g_LobbySendBuf[0]=PROTO_MAGIC_0; g_LobbySendBuf[1]=PROTO_MAGIC_1;
    g_LobbySendBuf[2]=CMD_ROOM_JOIN; g_LobbySendBuf[3]=0;
    g_LobbySendBuf[4]=0; g_LobbySendBuf[5]=1; g_LobbySendBuf[6]=roomId;
    Net_Send(g_LobbyConn, g_LobbySendBuf, 7);
}

void Lobby_SendPing(void)
{
    if(g_LobbyConn == NET_INVALID_CONN) return;
    g_LobbySendBuf[0]=PROTO_MAGIC_0; g_LobbySendBuf[1]=PROTO_MAGIC_1;
    g_LobbySendBuf[2]=CMD_PING; g_LobbySendBuf[3]=g_LobbyRoomId;
    g_LobbySendBuf[4]=g_LobbyPid; g_LobbySendBuf[5]=0;
    Net_Send(g_LobbyConn, g_LobbySendBuf, 6);
}

void Lobby_SendGameStart(void)
{
    if(g_LobbyConn == NET_INVALID_CONN) return;
    g_LobbySendBuf[0]=PROTO_MAGIC_0; g_LobbySendBuf[1]=PROTO_MAGIC_1;
    g_LobbySendBuf[2]=CMD_GAME_START; g_LobbySendBuf[3]=g_LobbyRoomId;
    g_LobbySendBuf[4]=g_LobbyPid; g_LobbySendBuf[5]=0;
    Net_Send(g_LobbyConn, g_LobbySendBuf, 6);
}

void Lobby_SendRoomLeave(void)
{
    if(!g_LobbyOnline || g_LobbyConn == NET_INVALID_CONN) return;
    g_LobbySendBuf[0]=PROTO_MAGIC_0; g_LobbySendBuf[1]=PROTO_MAGIC_1;
    g_LobbySendBuf[2]=CMD_ROOM_LEAVE; g_LobbySendBuf[3]=g_LobbyRoomId;
    g_LobbySendBuf[4]=g_LobbyPid; g_LobbySendBuf[5]=0;
    Net_Send(g_LobbyConn, g_LobbySendBuf, 6);
}

void Lobby_SendStateUpdate(u8* payload, u8 len)
{
    if(g_LobbyConn == NET_INVALID_CONN) return;
    g_LobbySendBuf[0]=PROTO_MAGIC_0; g_LobbySendBuf[1]=PROTO_MAGIC_1;
    g_LobbySendBuf[2]=CMD_STATE_UPDATE; g_LobbySendBuf[3]=g_LobbyRoomId;
    g_LobbySendBuf[4]=g_LobbyPid; g_LobbySendBuf[5]=len;
    {
        u8 i;
        for(i = 0; i < len && i < 14; i++) g_LobbySendBuf[6 + i] = payload[i];
    }
    Net_Send(g_LobbyConn, g_LobbySendBuf, 6 + len);
}

// ── Packet processing ───────────────────────────────────────────────

static void Lobby_Draw(void);

static void Lobby_ProcessPacket(u8 cmd, u8 senderPid, u8* pl, u8 len)
{
    if(cmd == CMD_ROOM_LIST && len >= 1)
    {
        u8 cnt = pl[0], i;
        g_LBN = 0;
        for(i = 0; i < cnt && i < LB_MAX; i++) {
            u8 off = 1 + i * 3;
            if(pl[off + 1] == g_Cfg->gameId) {
                g_LB[g_LBN].rid = pl[off];
                g_LB[g_LBN].gid = pl[off + 1];
                g_LB[g_LBN].np = pl[off + 2];
                g_LBN++;
            }
        }
        g_LBC = 0;
        g_LobbyState = LOBBY_ST_LIST;
        Lobby_Draw();
    }
    else if(cmd == CMD_ROOM_INFO && len >= 4)
    {
        u16 si;
        g_LobbyRoomId = pl[0];
        g_LobbyPid = pl[3];
        g_LobbyActive = 0;
        { u8 n = pl[2], j; for(j = 0; j < n; j++) g_LobbyActive |= (1 << j); }
        for(si = 0; si < 768; si++) g_NB[si] = 0;
        g_LobbyState = LOBBY_ST_WAITING;
    }
    else if(cmd == CMD_PLAYER_JOINED && len >= 1)
    {
        u8 jp = pl[0];
        if(jp >= 1 && jp <= 16) g_LobbyActive |= (1 << (jp - 1));
    }
    else if(cmd == CMD_PLAYER_LEFT && len >= 1)
    {
        u8 lp = pl[0];
        if(lp >= 1 && lp <= 16) g_LobbyActive &= ~(1 << (lp - 1));
    }
    else if(cmd == CMD_GAME_START)
    {
        u16 si;
        for(si = 0; si < 768; si++) g_NB[si] = 0;
        g_LobbyState = LOBBY_ST_PLAYING;
    }
    else
    {
        // Game-specific: pass to callback
        if(g_GameCB) g_GameCB(cmd, senderPid, pl, len);
    }
}

void Lobby_Poll(void)
{
    u16 avail;
    u8 hdr[6];
    u8 payload[200];
    u8 maxPkts;

    if(g_LobbyConn == NET_INVALID_CONN) return;

    maxPkts = 4;
    while(maxPkts--)
    {
        avail = Net_Available(g_LobbyConn);
        if(avail < 6) break;
        Net_Recv(g_LobbyConn, hdr, 6);
        if(hdr[0] != PROTO_MAGIC_0 || hdr[1] != PROTO_MAGIC_1) break;
        if(hdr[5] > 0) {
            avail = Net_Available(g_LobbyConn);
            if(avail < hdr[5]) break;
            Net_Recv(g_LobbyConn, payload, hdr[5]);
        }
        Lobby_ProcessPacket(hdr[2], hdr[4], payload, hdr[5]);
    }

    g_PingT++;
    if(g_PingT >= PING_INT) { g_PingT = 0; Lobby_SendPing(); }
}

// ── Lobby UI ────────────────────────────────────────────────────────

static void Lobby_Draw(void)
{
    u8 i;
    u16 idx;
    c8 maxStr[4];

    for(idx = 0; idx < 768; idx++) g_NB[idx] = 0;

    Lobby_NB_Text(8, 1, g_Cfg->gameName);
    Lobby_NB_Text(6, 3, "SALAS DISPONIBLES");

    if(g_LBN == 0) {
        Lobby_NB_Text(6, 8, "NO HAY SALAS");
        Lobby_NB_Text(6, 10, "PULSA C PARA CREAR");
    } else {
        Lobby_NB_Text(8, 5, "SALA  JUGADORES");
        for(i = 0; i < g_LBN; i++) {
            u8 row = 7 + i * 2;
            if(row > 20) break;
            if(i == g_LBC) Lobby_NB_Text(6, row, ">");
            Lobby_NB_Num(8, row, g_LB[i].rid);
            Lobby_NB_Num(14, row, g_LB[i].np);
            // "/N"
            Lobby_NB_Text(16, row, "/");
            Lobby_NB_Num(17, row, g_Cfg->maxPlayers);
        }
    }

    Lobby_NB_Text(4, 22, "C CREAR  ENTER UNIR");
    Lobby_NB_Text(4, 23, "R REFRESCAR  ESC SALIR");
}

static void Lobby_ProcessInput(void)
{
    if(g_KUp) { g_KUp=0; if(g_LBC > 0) { g_LBC--; Lobby_Draw(); } }
    if(g_KDown) { g_KDown=0; if(g_LBN > 0 && g_LBC < g_LBN-1) { g_LBC++; Lobby_Draw(); } }
    if(g_KRet) { g_KRet=0; if(g_LBN > 0) { Lobby_JoinRoom(g_LB[g_LBC].rid); g_LobbyState = LOBBY_ST_LIST_WAIT; } }
    if(g_KC) { g_KC=0; Lobby_CreateRoom(); g_LobbyState = LOBBY_ST_LIST_WAIT; }
    if(g_KR) { g_KR=0; Lobby_RequestRooms(); g_LobbyState = LOBBY_ST_LIST_WAIT; }
}

// ── Update (call each frame) ────────────────────────────────────────

u8 Lobby_Update(void)
{
    // Key capture with anti-bounce
    if(g_MvDly > 0) { g_MvDly--; }
    else {
        if(Keyboard_IsKeyPressed(KEY_UP))    { g_KUp=1; g_MvDly=8; }
        if(Keyboard_IsKeyPressed(KEY_DOWN))  { g_KDown=1; g_MvDly=8; }
        if(Keyboard_IsKeyPressed(KEY_RET))   { g_KRet=1; g_MvDly=15; }
        if(Keyboard_IsKeyPressed(KEY_C))     { g_KC=1; g_MvDly=15; }
        if(Keyboard_IsKeyPressed(KEY_R))     { g_KR=1; g_MvDly=15; }
        if(Keyboard_IsKeyPressed(KEY_S))     { g_KS=1; g_MvDly=15; }
    }

    // States
    if(g_LobbyState == LOBBY_ST_LIST_WAIT)
    {
        Lobby_Poll();
    }
    else if(g_LobbyState == LOBBY_ST_LIST)
    {
        Lobby_ProcessInput();
        // Light poll
        {
            u16 av = Net_Available(g_LobbyConn);
            if(av >= 6) {
                u8 hdr[6], pl2[200];
                Net_Recv(g_LobbyConn, hdr, 6);
                if(hdr[0] == PROTO_MAGIC_0 && hdr[1] == PROTO_MAGIC_1) {
                    if(hdr[5] > 0) { while(Net_Available(g_LobbyConn) < hdr[5]) Halt(); Net_Recv(g_LobbyConn, pl2, hdr[5]); }
                    Lobby_ProcessPacket(hdr[2], hdr[4], pl2, hdr[5]);
                }
            }
        }
    }
    else if(g_LobbyState == LOBBY_ST_WAITING)
    {
        Lobby_Poll();
        if(g_LobbyState != LOBBY_ST_WAITING) return g_LobbyState; // state changed

        Lobby_NB_Text(6, 4, "SALA:");
        Lobby_NB_Num(12, 4, g_LobbyRoomId);
        Lobby_NB_Text(6, 6, "TU ERES P");
        NB_SET(15, 6, g_TileF0 + g_LobbyPid);

        Lobby_NB_Text(6, 8, "JUGADORES CONECTADOS");
        {
            u8 pi2, row2 = 10;
            for(pi2 = 0; pi2 < g_Cfg->maxPlayers; pi2++) {
                Lobby_NB_Text(8, row2, "P");
                NB_SET(9, row2, g_TileF0 + pi2 + 1);
                if(g_LobbyActive & (1 << pi2))
                    Lobby_NB_Text(11, row2, "OK    ");
                else
                    Lobby_NB_Text(11, row2, "      ");
                row2++;
            }
        }

        if(g_LobbyPid == 1)
            Lobby_NB_Text(4, 20, "S EMPEZAR  ESC SALIR");
        else
            Lobby_NB_Text(4, 20, "ESPERANDO HOST  ESC");

        if(g_KS && g_LobbyPid == 1) {
            g_KS = 0;
            Lobby_SendGameStart();
            {u16 si; for(si = 0; si < 768; si++) g_NB[si] = 0;}
            g_LobbyState = LOBBY_ST_PLAYING;
        }
    }

    return g_LobbyState;
}
