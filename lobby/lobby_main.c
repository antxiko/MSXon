// lobby.c — MSXonLINE Universal Lobby (standalone LOBBY.COM)
// Screen 0 text mode — game selector, connect, rooms, waiting, launch game
#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"
#include "protocol.h"
#include "network.h"
#include "log.h"

// ── Server ─────────────────────────────────────────────────────────
static const u8 SERVER_IP[4] = {217, 154, 107, 144};
#define SERVER_PORT 9876

// ── Game table ─────────────────────────────────────────────────────
typedef struct {
    const c8* name;
    const c8* comFile;
    u8 gameId;
    u8 maxPlayers;
    u8 protoVersion;
} GameDef;

#define NUM_GAMES 8

static const GameDef g_Games[NUM_GAMES] = {
    {"BALL DEMO",    "MSXONLIN", 0x01, 4, 0x01},
    {"DAMAS",        "DAMAS",    0x02, 2, 0x01},
    {"BURDYN RPG",   "BURDYN",   0x03, 4, 0x02},
    {"PARCHIS",      "PARCHIS",  0x04, 4, 0x01},
    {"TEXAS HOLDEM", "TEXAS",    0x05, 6, 0x01},
    {"TETRIS 4P",    "TETRIS",   0x06, 4, 0x01},
    {"AMONG MSX",    "AMONG",    0x07, 8, 0x01},
    {"FROG FLIES",   "FROGFLIE", 0x69, 4, 0x01},
};

// ── State ──────────────────────────────────────────────────────────
#define ST_MENU      0
#define ST_DIAG      1
#define ST_CONNECTING 2
#define ST_LOBBY     3
#define ST_WAITING   4
#define ST_LAUNCHING 5

static u8 g_State;
static u8 g_SelGame;
static const GameDef* g_CurGame;

// Network
static NetConn g_Conn;
static u8 g_MyPid;
static u8 g_RoomId;
static u8 g_Active;
static u8 g_SendBuf[20];
static bool g_Online;

// Room list
#define LB_MAX 20
typedef struct { u8 rid, gid, np; } LBRoom;
static LBRoom g_LB[LB_MAX];
static u8 g_LBN, g_LBC;

// Key debounce
static u8 g_KeyDly;

// Ping
#define PING_INT 250
static u16 g_PingT;

// ── Helpers ────────────────────────────────────────────────────────

static void Print(const c8* s) {
    while(*s) { DOS_CharOutput(*s); s++; }
}

static void PrintLn(const c8* s) {
    Print(s);
    DOS_CharOutput(0x0D); DOS_CharOutput(0x0A);
}

static void PrintDec8(u8 val) {
    u8 h = val / 100, t = (val % 100) / 10, u = val % 10;
    if(h) DOS_CharOutput('0' + h);
    if(h || t) DOS_CharOutput('0' + t);
    DOS_CharOutput('0' + u);
}

static void PrintDec16(u16 val) {
    u8 d[5], i, started;
    u16 div;
    d[0] = (u8)(val / 10000); val %= 10000;
    d[1] = (u8)(val / 1000);  val %= 1000;
    d[2] = (u8)(val / 100);   val %= 100;
    d[3] = (u8)(val / 10);    val %= 10;
    d[4] = (u8)(val);
    started = 0;
    for(i = 0; i < 5; i++) {
        if(d[i] > 0 || started || i == 4) { DOS_CharOutput('0' + d[i]); started = 1; }
    }
}

static void PrintIP(const u8* ip) {
    u8 i;
    for(i = 0; i < 4; i++) { PrintDec8(ip[i]); if(i < 3) DOS_CharOutput('.'); }
}

static void ClearScreen(void) {
    DOS_CharOutput(0x1B); DOS_CharOutput('E'); // ANSI clear screen
}

static void Wait50(void) { u8 w; for(w = 0; w < 25; w++) Halt(); }

// ── Diagnostics ────────────────────────────────────────────────────

static bool RunDiag(void) {
    u8 localIP[4];
    u8 ok;

    PrintLn("================================");
    Print("   MSXonLINE - ");
    PrintLn(g_CurGame->name);
    PrintLn("================================");
    PrintLn("");

    Print("UNAPI TCP/IP: ");
    ok = (tcpip_enumerate() > 0) ? 1 : 0;
    if(ok)
        PrintLn("ENCONTRADO");
    else {
        PrintLn("NO ENCONTRADO");
        PrintLn("");
        PrintLn("Modo OFFLINE.");
        PrintLn("Pulsa ESPACIO para continuar...");
        DOS_CharInput();
        return FALSE;
    }

    tcpip_get_ipinfo(&g_IpInfo);

    Print("IP local    : ");
    localIP[0] = (u8)g_IpInfo.local_ip[0]; localIP[1] = (u8)g_IpInfo.local_ip[1];
    localIP[2] = (u8)g_IpInfo.local_ip[2]; localIP[3] = (u8)g_IpInfo.local_ip[3];
    PrintIP(localIP);
    PrintLn("");

    Print("Mascara     : ");
    localIP[0] = (u8)g_IpInfo.subnet_mask[0]; localIP[1] = (u8)g_IpInfo.subnet_mask[1];
    localIP[2] = (u8)g_IpInfo.subnet_mask[2]; localIP[3] = (u8)g_IpInfo.subnet_mask[3];
    PrintIP(localIP);
    PrintLn("");

    Print("Gateway     : ");
    localIP[0] = (u8)g_IpInfo.gateway_ip[0]; localIP[1] = (u8)g_IpInfo.gateway_ip[1];
    localIP[2] = (u8)g_IpInfo.gateway_ip[2]; localIP[3] = (u8)g_IpInfo.gateway_ip[3];
    PrintIP(localIP);
    PrintLn("");

    Print("Servidor    : ");
    PrintIP(SERVER_IP);
    PrintLn("");

    Print("Puerto      : ");
    PrintDec16(SERVER_PORT);
    PrintLn("");

    PrintLn("");
    PrintLn("Pulsa ESPACIO para continuar...");
    DOS_CharInput();
    return TRUE;
}

// ── Connection ─────────────────────────────────────────────────────

static bool DoConnect(void) {
    u8 tcpState;
    u16 timeout;

    Log_Init();
    Log_Write("[LOBBY] Init");

    if(Net_Init() != NET_OK) { Log_Write("[LOBBY] No UNAPI"); return FALSE; }

    Wait50();
    tcpip_get_ipinfo(&g_IpInfo);
    Wait50();

    PrintLn("Conectando al servidor...");
    g_Conn = Net_Open(SERVER_IP, SERVER_PORT);
    if(g_Conn == NET_INVALID_CONN) {
        PrintLn("ERROR: No se pudo abrir conexion.");
        Log_Write("[LOBBY] Open failed");
        DOS_CharInput();
        return FALSE;
    }

    timeout = 0;
    while(timeout < 500) {
        Halt();
        tcpState = Net_GetConnState(g_Conn);
        if(tcpState == TCP_STATE_ESTABLISHED) break;
        if(tcpState == 0xFF) { PrintLn("ERROR: Conexion rechazada."); DOS_CharInput(); return FALSE; }
        timeout++;
    }
    if(timeout >= 500) { PrintLn("ERROR: Timeout de conexion."); DOS_CharInput(); return FALSE; }
    Log_Write("[LOBBY] ESTABLISHED");

    Wait50();

    // Auth
    {
        u8 token[4];
        token[0] = AUTH_TOKEN_0; token[1] = AUTH_TOKEN_1;
        token[2] = AUTH_TOKEN_2; token[3] = AUTH_TOKEN_3;
        g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
        g_SendBuf[2] = CMD_AUTH; g_SendBuf[3] = 0;
        g_SendBuf[4] = 0; g_SendBuf[5] = 4;
        g_SendBuf[6] = token[0]; g_SendBuf[7] = token[1];
        g_SendBuf[8] = token[2]; g_SendBuf[9] = token[3];
        Net_Send(g_Conn, g_SendBuf, 10);
    }

    timeout = 0;
    while(timeout < 250) {
        u16 avail;
        Halt();
        avail = Net_Available(g_Conn);
        if(avail >= 6) {
            u8 hdr[6];
            Net_Recv(g_Conn, hdr, 6);
            if(hdr[2] == CMD_AUTH_OK) { Log_Write("[LOBBY] AUTH OK"); break; }
            if(hdr[2] == CMD_AUTH_FAIL) { PrintLn("ERROR: Auth fallida."); DOS_CharInput(); return FALSE; }
        }
        timeout++;
    }
    if(timeout >= 250) { PrintLn("ERROR: Timeout auth."); DOS_CharInput(); return FALSE; }

    PrintLn("Conectado!");
    g_Online = TRUE;
    return TRUE;
}

// ── Room operations ────────────────────────────────────────────────

static void SendRoomList(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_ROOM_LIST; g_SendBuf[3]=0;
    g_SendBuf[4]=0; g_SendBuf[5]=0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

static void SendCreateRoom(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_ROOM_CREATE; g_SendBuf[3]=0;
    g_SendBuf[4]=0; g_SendBuf[5]=3;
    g_SendBuf[6]=g_CurGame->gameId;
    g_SendBuf[7]=g_CurGame->maxPlayers;
    g_SendBuf[8]=g_CurGame->protoVersion;
    Net_Send(g_Conn, g_SendBuf, 9);
}

static void SendJoinRoom(u8 roomId) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_ROOM_JOIN; g_SendBuf[3]=0;
    g_SendBuf[4]=0; g_SendBuf[5]=1; g_SendBuf[6]=roomId;
    Net_Send(g_Conn, g_SendBuf, 7);
}

static void SendPing(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_PING; g_SendBuf[3]=g_RoomId;
    g_SendBuf[4]=g_MyPid; g_SendBuf[5]=0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

static void SendGameStart(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_GAME_START; g_SendBuf[3]=g_RoomId;
    g_SendBuf[4]=g_MyPid; g_SendBuf[5]=0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

// ── Packet processing ──────────────────────────────────────────────

static void ProcessPacket(u8 cmd, u8* pl, u8 len) {
    if(cmd == CMD_ROOM_LIST && len >= 1) {
        u8 cnt = pl[0], i;
        g_LBN = 0;
        for(i = 0; i < cnt && i < LB_MAX; i++) {
            u8 off = 1 + i * 3;
            if(pl[off + 1] == g_CurGame->gameId) {
                g_LB[g_LBN].rid = pl[off];
                g_LB[g_LBN].gid = pl[off + 1];
                g_LB[g_LBN].np = pl[off + 2];
                g_LBN++;
            }
        }
        g_LBC = 0;
        g_State = ST_LOBBY;
    }
    else if(cmd == CMD_ROOM_INFO && len >= 4) {
        g_RoomId = pl[0];
        g_MyPid = pl[3];
        g_Active = 0;
        { u8 n = pl[2], j; for(j = 0; j < n; j++) g_Active |= (1 << j); }
        g_State = ST_WAITING;
    }
    else if(cmd == CMD_PLAYER_JOINED && len >= 1) {
        u8 jp = pl[0];
        if(jp >= 1 && jp <= 16) g_Active |= (1 << (jp - 1));
    }
    else if(cmd == CMD_PLAYER_LEFT && len >= 1) {
        u8 lp = pl[0];
        if(lp >= 1 && lp <= 16) g_Active &= ~(1 << (lp - 1));
    }
    else if(cmd == CMD_GAME_START) {
        g_State = ST_LAUNCHING;
    }
}

static void Poll(void) {
    u16 avail;
    u8 hdr[6];
    u8 payload[200];
    u8 maxPkts;

    if(g_Conn == NET_INVALID_CONN) return;

    maxPkts = 4;
    while(maxPkts--) {
        avail = Net_Available(g_Conn);
        if(avail < 6) break;
        Net_Recv(g_Conn, hdr, 6);
        if(hdr[0] != PROTO_MAGIC_0 || hdr[1] != PROTO_MAGIC_1) break;
        if(hdr[5] > 0) {
            avail = Net_Available(g_Conn);
            if(avail < hdr[5]) break;
            Net_Recv(g_Conn, payload, hdr[5]);
        }
        ProcessPacket(hdr[2], payload, hdr[5]);
    }

    g_PingT++;
    if(g_PingT >= PING_INT) { g_PingT = 0; SendPing(); }
}

// ── Write LOBBY.DAT ────────────────────────────────────────────────

static void WriteLobbyDat(void) {
    u8 data[8];
    u8 fh;

    data[0] = 0xAA; // magic
    data[1] = (u8)g_Conn;
    data[2] = g_MyPid;
    data[3] = g_RoomId;
    data[4] = g_Active;
    data[5] = g_CurGame->gameId;
    data[6] = g_CurGame->protoVersion;
    data[7] = 0x00;

    fh = DOS_CreateHandle("LOBBY.DAT", O_WRONLY, 0x00);
    if(fh < 0xFE) {
        DOS_WriteHandle(fh, data, 8);
        DOS_CloseHandle(fh);
    }
}

// ── Write _LAUNCH.BAT ──────────────────────────────────────────────

static void WriteLaunchBat(void) {
    u8 fh;
    const c8* name = g_CurGame->comFile;
    u16 len = 0;
    static const c8 crlf[2] = { 0x0D, 0x0A };

    while(name[len]) len++;

    fh = DOS_CreateHandle("_LAUNCH.BAT", O_WRONLY, 0x00);
    if(fh < 0xFE) {
        DOS_WriteHandle(fh, name, len);
        DOS_WriteHandle(fh, crlf, 2);
        DOS_CloseHandle(fh);
    }
}

// ── Draw screens ───────────────────────────────────────────────────

static void DrawMenu(void) {
    u8 i;
    ClearScreen();
    PrintLn("================================");
    PrintLn("     MSXonLINE v2.0");
    PrintLn("     Selector de Juegos");
    PrintLn("================================");
    PrintLn("");
    for(i = 0; i < NUM_GAMES; i++) {
        if(i == g_SelGame) Print("> ");
        else Print("  ");
        DOS_CharOutput('1' + i);
        Print(". ");
        PrintLn(g_Games[i].name);
    }
    PrintLn("");
    PrintLn("Cursores + ENTER para elegir");
    PrintLn("ESC para salir a DOS");
}

static void DrawLobby(void) {
    u8 i;
    ClearScreen();
    Print("Salas de ");
    PrintLn(g_CurGame->name);
    PrintLn("================================");
    PrintLn("");
    if(g_LBN == 0) {
        PrintLn("  No hay salas disponibles.");
        PrintLn("");
        PrintLn("  C = Crear sala");
        PrintLn("  R = Refrescar");
        PrintLn("  ESC = Volver al menu");
    } else {
        PrintLn("  SALA   JUGADORES");
        PrintLn("  ----   ----------");
        for(i = 0; i < g_LBN; i++) {
            if(i == g_LBC) Print("> ");
            else Print("  ");
            PrintDec8(g_LB[i].rid);
            Print("      ");
            PrintDec8(g_LB[i].np);
            DOS_CharOutput('/');
            PrintDec8(g_CurGame->maxPlayers);
            PrintLn("");
        }
        PrintLn("");
        PrintLn("ENTER=Unir  C=Crear  R=Refrescar");
        PrintLn("ESC=Volver al menu");
    }
}

static void DrawWaiting(void) {
    u8 i;
    ClearScreen();
    Print("Sala: ");
    PrintDec8(g_RoomId);
    Print("  -  ");
    PrintLn(g_CurGame->name);
    PrintLn("================================");
    PrintLn("");
    Print("Tu eres P");
    PrintDec8(g_MyPid);
    PrintLn("");
    PrintLn("");
    PrintLn("Jugadores conectados:");
    for(i = 0; i < g_CurGame->maxPlayers; i++) {
        Print("  P");
        PrintDec8(i + 1);
        if(g_Active & (1 << i))
            PrintLn("  OK");
        else
            PrintLn("  --");
    }
    PrintLn("");
    if(g_MyPid == 1)
        PrintLn("S = Empezar    ESC = Salir");
    else
        PrintLn("Esperando al host...  ESC = Salir");
}

// ── Launch game ────────────────────────────────────────────────────

static void LaunchGame(bool online) {
    if(online) {
        WriteLobbyDat();
    }
    WriteLaunchBat();
    Log_Close();
    Bios_Exit(0);
}

// ── Main ───────────────────────────────────────────────────────────

void main(void)
{
    u8 prevActive;

    *((u8*)0xF3DB) = 0; // disable key click

    g_State = ST_MENU;
    g_SelGame = 0;
    g_Conn = NET_INVALID_CONN;
    g_Online = FALSE;
    g_KeyDly = 0;
    g_PingT = 0;
    g_LBN = 0;
    g_LBC = 0;

    DrawMenu();

    while(1) {
        Halt();
        *((u16*)0xF3F8) = *((u16*)0xF3FA); // flush keyboard buffer

        // Key debounce
        if(g_KeyDly > 0) { g_KeyDly--; }

        // ── MENU ───────────────────────────────────────────────
        if(g_State == ST_MENU) {
            if(g_KeyDly == 0) {
                if(Keyboard_IsKeyPressed(KEY_UP) && g_SelGame > 0) {
                    g_SelGame--; DrawMenu(); g_KeyDly = 8;
                }
                if(Keyboard_IsKeyPressed(KEY_DOWN) && g_SelGame < NUM_GAMES - 1) {
                    g_SelGame++; DrawMenu(); g_KeyDly = 8;
                }
                if(Keyboard_IsKeyPressed(KEY_RET)) {
                    g_CurGame = &g_Games[g_SelGame];
                    g_KeyDly = 15;

                    ClearScreen();
                    // Diag + connect
                    if(RunDiag()) {
                        ClearScreen();
                        if(DoConnect()) {
                            SendRoomList();
                            g_State = ST_CONNECTING; // wait for room list
                        } else {
                            g_State = ST_MENU;
                            DrawMenu();
                        }
                    } else {
                        // Offline: launch game directly
                        LaunchGame(FALSE);
                    }
                }
                if(Keyboard_IsKeyPressed(KEY_ESC)) {
                    Bios_Exit(0);
                }
                // Number keys 1-8
                {
                    u8 k;
                    for(k = 0; k < NUM_GAMES; k++) {
                        if(Keyboard_IsKeyPressed(KEY_1 + k)) {
                            g_SelGame = k;
                            DrawMenu();
                            g_KeyDly = 8;
                            break;
                        }
                    }
                }
            }
        }

        // ── CONNECTING (waiting for room list) ─────────────────
        else if(g_State == ST_CONNECTING) {
            Poll();
            // State changes to ST_LOBBY when room list arrives
        }

        // ── LOBBY (room list) ──────────────────────────────────
        else if(g_State == ST_LOBBY) {
            static u8 lobbyDrawn = 0;
            if(!lobbyDrawn) { DrawLobby(); lobbyDrawn = 1; }

            if(g_KeyDly == 0) {
                if(Keyboard_IsKeyPressed(KEY_UP) && g_LBC > 0) {
                    g_LBC--; lobbyDrawn = 0; g_KeyDly = 8;
                }
                if(Keyboard_IsKeyPressed(KEY_DOWN) && g_LBN > 0 && g_LBC < g_LBN - 1) {
                    g_LBC++; lobbyDrawn = 0; g_KeyDly = 8;
                }
                if(Keyboard_IsKeyPressed(KEY_RET) && g_LBN > 0) {
                    SendJoinRoom(g_LB[g_LBC].rid);
                    g_State = ST_CONNECTING;
                    lobbyDrawn = 0;
                    g_KeyDly = 15;
                }
                if(Keyboard_IsKeyPressed(KEY_C)) {
                    SendCreateRoom();
                    g_State = ST_CONNECTING;
                    lobbyDrawn = 0;
                    g_KeyDly = 15;
                }
                if(Keyboard_IsKeyPressed(KEY_R)) {
                    SendRoomList();
                    g_State = ST_CONNECTING;
                    lobbyDrawn = 0;
                    g_KeyDly = 15;
                }
                if(Keyboard_IsKeyPressed(KEY_ESC)) {
                    // Back to menu — disconnect
                    Net_Close(g_Conn);
                    g_Conn = NET_INVALID_CONN;
                    g_Online = FALSE;
                    g_State = ST_MENU;
                    lobbyDrawn = 0;
                    DrawMenu();
                    g_KeyDly = 15;
                }
            }

            // Light poll
            {
                u16 av = Net_Available(g_Conn);
                if(av >= 6) {
                    u8 hdr2[6], pl2[200];
                    Net_Recv(g_Conn, hdr2, 6);
                    if(hdr2[0] == PROTO_MAGIC_0 && hdr2[1] == PROTO_MAGIC_1) {
                        if(hdr2[5] > 0) { while(Net_Available(g_Conn) < hdr2[5]) Halt(); Net_Recv(g_Conn, pl2, hdr2[5]); }
                        ProcessPacket(hdr2[2], pl2, hdr2[5]);
                    }
                }
            }
        }

        // ── WAITING ────────────────────────────────────────────
        else if(g_State == ST_WAITING) {
            static u8 waitDrawn = 0;
            static u8 prevAct = 0;

            Poll();
            if(g_State == ST_LAUNCHING) continue; // game started!

            if(!waitDrawn || g_Active != prevAct) {
                DrawWaiting();
                waitDrawn = 1;
                prevAct = g_Active;
            }

            if(g_KeyDly == 0) {
                if(Keyboard_IsKeyPressed(KEY_S) && g_MyPid == 1) {
                    SendGameStart();
                    g_State = ST_LAUNCHING;
                    g_KeyDly = 15;
                }
                if(Keyboard_IsKeyPressed(KEY_ESC)) {
                    // Leave room
                    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
                    g_SendBuf[2]=CMD_ROOM_LEAVE; g_SendBuf[3]=g_RoomId;
                    g_SendBuf[4]=g_MyPid; g_SendBuf[5]=0;
                    Net_Send(g_Conn, g_SendBuf, 6);
                    // Back to lobby
                    SendRoomList();
                    g_State = ST_CONNECTING;
                    waitDrawn = 0;
                    g_KeyDly = 15;
                }
            }
        }

        // ── LAUNCHING ──────────────────────────────────────────
        else if(g_State == ST_LAUNCHING) {
            LaunchGame(TRUE);
            // Never returns — Bios_Exit in LaunchGame
        }
    }
}
