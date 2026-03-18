//=============================================================================
// msxonline.c — MSX Online · Cliente mínimo de demostración
//
// MSX2 · MSX-DOS · MSXgl · UNAPI TCP/IP
// Hardware: MSX2 + ObsoNET + InterNestor Lite
//
// Controles:
//   Cursores → mover la bola
//   ESC      → salir de la sala y del juego
//
// Compilar: ver Makefile
//=============================================================================

#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "print.h"
#include "bios.h"
#include "system.h"
#include "font/font_mgl_sample6.h"
#include "protocol.h"
#include "network.h"
#include "log.h"

//=============================================================================
// CONFIGURACIÓN — EDITAR ANTES DE COMPILAR
//=============================================================================

// IP del servidor como 4 bytes (cambiar a la IP real de tu VPS)
static const u8 SERVER_IP[4] = { 217, 154, 107, 144 };
#define SERVER_PORT     9876

#define MOVE_SPEED      2           // Píxeles por frame

// Límites del área de juego (Screen 5: 256×212)
// Reservamos 33px arriba para el HUD
#define HUD_HEIGHT      33
#define PLAY_X0         0
#define PLAY_Y0         HUD_HEIGHT
#define PLAY_X1         (256 - 16)  // 240 (restar ancho sprite)
#define PLAY_Y1         (212 - 16)  // 196 (restar alto sprite)

// Posiciones de inicio por jugador (en los 4 cuadrantes)
#define P1_START_X      32
#define P1_START_Y      (HUD_HEIGHT + 20)
#define P2_START_X      192
#define P2_START_Y      (HUD_HEIGHT + 20)
#define P3_START_X      32
#define P3_START_Y      (HUD_HEIGHT + 120)
#define P4_START_X      192
#define P4_START_Y      (HUD_HEIGHT + 120)

// Ping cada 250 frames (~5 s a 50 Hz)
#define PING_INTERVAL   250

//=============================================================================
// TIPOS
//=============================================================================

typedef enum
{
    STATE_INIT = 0,
    STATE_CONNECTING,       // Llamando a Net_Open
    STATE_TCP_WAIT,         // Esperando TCP handshake (SYN_SENT → ESTABLISHED)
    STATE_AUTH_WAIT,        // Esperando CMD_AUTH_OK
    STATE_LOBBY,            // Lobby: lista de salas
    STATE_LOBBY_WAIT,       // Esperando respuesta CMD_ROOM_LIST
    STATE_JOIN_INPUT,       // Introduciendo Room ID por teclado
    STATE_CREATE_ROOM,      // Enviando CMD_ROOM_CREATE
    STATE_JOIN_ROOM,        // Enviando CMD_ROOM_JOIN
    STATE_ROOM_WAIT,        // Esperando CMD_ROOM_INFO
    STATE_PLAYING,          // En partida
    STATE_LEAVING,          // Enviando CMD_ROOM_LEAVE
    STATE_DISCONNECTED,     // Conexión perdida o auth fallida
    STATE_EXIT              // Salir al MSX-DOS
} GameState;

typedef struct
{
    u16     x;              // Posición X (0..255)
    u16     y;              // Posición Y (0..211)
    u8      frame;          // Frame de animación
    u8      flags;          // STATE_FLAG_*
    bool   active;         // TRUE si está en la sala
} Player;

// Entrada en la lista de salas del lobby
#define LOBBY_MAX_ROOMS     20  // Max salas visibles en pantalla
typedef struct
{
    u8  roomId;
    u8  gameId;
    u8  players;
} RoomEntry;

//=============================================================================
// CONSTANTES DE COLOR POR JUGADOR
// Paleta default MSX Screen 5 (TMS9918 colors)
//=============================================================================

// Índice 0 sin uso (PID empieza en 1)
const u8 g_Colors[5]      = { 0, 15, 7, 9, 11 };
//                              P1  P2  P3  P4
//                           Blanco Cian Rojo Amarillo

const c8* g_ColorNames[5] = { "", "BLANCO", "CIAN", "ROJO", "AMARILLO" };

//=============================================================================
// PATRÓN SPRITE: Bola 16×16
//
// Formato MSXgl para sprites 16×16 (32 bytes):
//   Bytes  0–7:  filas 0–7,  byte izquierdo  (columnas 8–15)
//   Bytes  8–15: filas 0–7,  byte derecho     (columnas 0–7)
//   Bytes 16–23: filas 8–15, byte izquierdo
//   Bytes 24–31: filas 8–15, byte derecho
//
// Visual (aproximado):
//     ..ooooooo..
//    .ooooooooooo.
//   ooooooooooooooo
//   ooooooooooooooo
//   ooooooooooooooo
//    .ooooooooooo.
//     ..ooooooo..
//=============================================================================
const u8 g_BallPattern[32] =
{
    // Filas 0–7, byte izquierdo (pixels 15..8)
    0x07, 0x1F, 0x3F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF,
    // Filas 0–7, byte derecho  (pixels  7..0)
    0xE0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    // Filas 8–15, byte izquierdo
    0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x3F, 0x1F, 0x07,
    // Filas 8–15, byte derecho
    0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC, 0xF8, 0xE0
};

//=============================================================================
// VARIABLES GLOBALES
//=============================================================================

static GameState    g_State        = STATE_INIT;
static Player       g_Players[5];          // Índice 1..4 (0 sin uso)
static u8           g_MyPid        = 0;
static u8           g_RoomId       = 0;
static u8           g_PlayerCount  = 0;
static NetConn      g_Conn         = NET_INVALID_CONN;
static u8           g_SendBuf[270];        // 6 header + 255 payload max + margen
static u8           g_RecvBuf[270];
static u16          g_PingTimer    = 0;
static bool        g_HUDDirty     = TRUE;
static c8           g_StatusMsg[33];       // Mensaje de estado visible en HUD
static u8           g_JoinDigits[3];       // Dígitos del Room ID (max 255)
static u8           g_JoinLen      = 0;    // Cuántos dígitos introducidos
static RoomEntry    g_LobbyRooms[LOBBY_MAX_ROOMS];
static u8           g_LobbyCount   = 0;    // Salas recibidas del servidor
static u8           g_LobbyCursor  = 0;    // Sala seleccionada en la lista

// Posiciones de inicio indexadas por PID
static const u16 g_StartX[5] = { 0, P1_START_X, P2_START_X, P3_START_X, P4_START_X };
static const u16 g_StartY[5] = { 0, P1_START_Y, P2_START_Y, P3_START_Y, P4_START_Y };

//=============================================================================
// FORWARD DECLARATIONS
//=============================================================================

void Game_Init(void);
void Game_Loop(void);
void Game_Shutdown(void);
void Input_ProcessMovement(void);

void Net_Connect(void);
void Net_Poll(void);
void Net_SendAuth(void);
void Net_SendCreateRoom(void);
void Net_SendJoinRoom(u8 roomId);
void Net_SendRoomList(void);
void Net_SendLeave(void);
void Net_SendPing(void);
void Net_SendState(void);
void Lobby_Draw(void);
void Lobby_ProcessInput(void);
void Lobby_RequestList(void);
void JoinInput_Draw(void);
void JoinInput_ProcessKey(void);
void Net_HandlePacket(u8 cmd, u8 room, u8 pid, const u8* payload, u8 len);
u8   Packet_Build(u8 cmd, u8 room, u8 pid, const u8* payload, u8 payloadLen);

void HUD_Redraw(void);
void HUD_SetStatus(const c8* msg);
void Sprites_Refresh(void);

//=============================================================================
// INICIALIZACIÓN
//=============================================================================

void Game_Init(void)
{
    u8 i;

    //-- Log: crear fichero de log antes de todo
    Log_Init();
    Log_Write("[INIT] MSX Online arrancando");

    //-- VDP: Screen 5 (256×212, 16 colores, bitmap)
    VDP_SetMode(VDP_MODE_SCREEN5);
    VDP_SetColor(0);
    VDP_FillVRAM(0x00, 0x0000, 0x00, 0x8000); // Limpiar VRAM (primera mitad)
    VDP_FillVRAM(0x00, 0x8000, 0x00, 0x8000); // Limpiar VRAM (segunda mitad)

    //-- Sprites: modo 16×16, sin zoom
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_16 | VDP_SPRITE_SCALE_1);

    //-- Cargar el patrón de bola en todos los slots de sprite (0..3)
    //   Mismo patrón para todos; el color diferencia a los jugadores
    for(i = 0; i < MAX_PLAYERS; i++)
    {
        VDP_LoadSpritePattern(g_BallPattern, i * 4, 4); // 4 bloques de 8 bytes
    }

    //-- Inicializar datos de jugadores
    for(i = 1; i <= MAX_PLAYERS; i++)
    {
        g_Players[i].x      = g_StartX[i];
        g_Players[i].y      = g_StartY[i];
        g_Players[i].frame  = 0;
        g_Players[i].flags  = 0;
        g_Players[i].active = FALSE;
    }

    //-- Print: modo bitmap (Screen 5) para el HUD
    Print_SetMode(PRINT_MODE_BITMAP);
    Print_SetFont(g_Font_MGL_Sample6);
    Print_SetColor(15, 0);

    //-- HUD inicial
    HUD_SetStatus("Conectando...");
    HUD_Redraw();
}

//=============================================================================
// HUD
//=============================================================================

void HUD_SetStatus(const c8* msg)
{
    u8 i;
    for(i = 0; i < 32 && msg[i]; i++) g_StatusMsg[i] = msg[i];
    g_StatusMsg[i] = 0;
    g_HUDDirty = TRUE;
}

void HUD_Redraw(void)
{
    u8 i;

    if(!g_HUDDirty) return;

    // Fondo del HUD (negro) — rectángulo lleno via comando HMMV
    VDP_CommandHMMV(0, 0, 256, HUD_HEIGHT, 0x00);
    VDP_CommandWait();

    //-- Línea 1: "MSX ONLINE  [estado]"
    Print_SetPosition(0, 0);
    Print_SetColor(11, 0);          // Amarillo
    Print_DrawText("MSX ONLINE");
    Print_SetColor(14, 0);          // Gris
    Print_DrawText(" >> ");
    Print_SetColor(15, 0);          // Blanco
    Print_DrawText(g_StatusMsg);

    //-- Línea 2: info de sala + jugadores
    Print_SetPosition(0, 8);

    if(g_RoomId == 0)
    {
        Print_SetColor(8, 0);       // Rojo apagado
        Print_DrawText("Sin sala");
    }
    else
    {
        Print_SetColor(7, 0);       // Cian
        Print_DrawText("SALA:");
        Print_DrawHex8(g_RoomId);
        Print_DrawText("  ");

        for(i = 1; i <= MAX_PLAYERS; i++)
        {
            if(g_Players[i].active)
            {
                // Color del jugador + indicador "yo" (asterisco)
                Print_SetColor(g_Colors[i], 0);
                Print_DrawText("[P");
                Print_DrawHex8(i);
                if(i == g_MyPid) Print_DrawChar('*');
                Print_DrawChar(']');
                Print_DrawChar(' ');
            }
        }

        // Instrucciones breves (en gris, al final de la línea si cabe)
        Print_SetColor(14, 0);
        Print_DrawText(" ESC=salir");
    }

    //-- Línea separadora entre HUD y área de juego
    VDP_CommandLINE(0, HUD_HEIGHT - 1, 255, 0, g_Colors[1], 0, 0);

    g_HUDDirty = FALSE;
}

//=============================================================================
// SPRITES
//=============================================================================

void Sprites_Refresh(void)
{
    u8 i;
    for(i = 1; i <= MAX_PLAYERS; i++)
    {
        if(g_Players[i].active)
        {
            // Sprite i-1 (índices 0..3)
            VDP_SetSpriteSM1(
                i - 1,                          // índice sprite
                (u8)(g_Players[i].x & 0xFF),    // X
                (u8)(g_Players[i].y & 0xFF),    // Y
                (i - 1) * 4,                    // patrón (bloque 0/4/8/12)
                g_Colors[i]                     // color
            );
        }
        else
        {
            // Ocultar sprite fuera de pantalla (Y=209 = "invisible" en MSX)
            VDP_SetSpriteSM1(i - 1, 0, 209, 0, 0);
        }
    }
}

//=============================================================================
// PROTOCOLO — CONSTRUCCIÓN DE PAQUETES
//=============================================================================

u8 Packet_Build(u8 cmd, u8 room, u8 pid, const u8* payload, u8 payloadLen)
{
    u8 i;
    g_SendBuf[0] = PROTO_MAGIC_0;
    g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = cmd;
    g_SendBuf[3] = room;
    g_SendBuf[4] = pid;
    g_SendBuf[5] = payloadLen;
    for(i = 0; i < payloadLen; i++) g_SendBuf[6 + i] = payload[i];
    return PROTO_HEADER_SZ + payloadLen;
}

//=============================================================================
// RED — ENVÍOS
//=============================================================================

void Net_SendAuth(void)
{
    u8 token[4];
    u8 len;
    token[0] = AUTH_TOKEN_0;
    token[1] = AUTH_TOKEN_1;
    token[2] = AUTH_TOKEN_2;
    token[3] = AUTH_TOKEN_3;
    len = Packet_Build(CMD_AUTH, 0, 0, token, 4);
    Net_Send(g_Conn, g_SendBuf, len);
    Net_Flush(g_Conn);
    Log_Write("[AUTH] Enviando auth...");
    HUD_SetStatus("Auth enviada...");
}

void Net_SendCreateRoom(void)
{
    u8 payload[1];
    u8 len;
    payload[0] = GAME_ID_BALL;
    len = Packet_Build(CMD_ROOM_CREATE, 0, 0, payload, 1);
    Net_Send(g_Conn, g_SendBuf, len);
    Log_Write("[ROOM] Enviando CREATE");
    HUD_SetStatus("Creando sala...");
}

void Net_SendRoomList(void)
{
    u8 len;
    len = Packet_Build(CMD_ROOM_LIST, 0, 0, 0, 0);
    Net_Send(g_Conn, g_SendBuf, len);
}

void Net_SendJoinRoom(u8 roomId)
{
    u8 payload[1];
    u8 len;
    payload[0] = roomId;
    len = Packet_Build(CMD_ROOM_JOIN, 0, 0, payload, 1);
    Net_Send(g_Conn, g_SendBuf, len);
    Log_WriteHex("[ROOM] Enviando JOIN room=", roomId);
    HUD_SetStatus("Uniendo a sala...");
}

void Net_SendLeave(void)
{
    u8 len;
    len = Packet_Build(CMD_ROOM_LEAVE, g_RoomId, g_MyPid, 0, 0);
    Net_Send(g_Conn, g_SendBuf, len);
}

void Net_SendPing(void)
{
    u8 len;
    len = Packet_Build(CMD_PING, g_RoomId, g_MyPid, 0, 0);
    Net_Send(g_Conn, g_SendBuf, len);
}

void Net_SendState(void)
{
    Player* me = &g_Players[g_MyPid];
    u8 payload[STATE_PAYLOAD_SZ];
    u8 len;

    payload[0] = (u8)(me->x >> 8);
    payload[1] = (u8)(me->x & 0xFF);
    payload[2] = (u8)(me->y >> 8);
    payload[3] = (u8)(me->y & 0xFF);
    payload[4] = me->frame;
    payload[5] = me->flags;
    payload[6] = 0;
    payload[7] = 0;

    len = Packet_Build(CMD_STATE_UPDATE, g_RoomId, g_MyPid, payload, STATE_PAYLOAD_SZ);
    Net_Send(g_Conn, g_SendBuf, len);
}

//=============================================================================
// RED — PROCESAR PAQUETE RECIBIDO
//=============================================================================

void Net_HandlePacket(u8 cmd, u8 room, u8 pid, const u8* payload, u8 len)
{
    u8 i;
    u8 newPid;
    Player* p;

    (void)room; // Ignorado — el servidor ya filtra por sala

    switch(cmd)
    {
        //-- AUTH ─────────────────────────────────────────────────────────────
        case CMD_AUTH_OK:
            Log_Write("[AUTH] OK");
            HUD_SetStatus("Cargando salas...");
            Lobby_RequestList();
            break;

        case CMD_AUTH_FAIL:
            Log_Write("[AUTH] FALLIDA");
            g_State = STATE_DISCONNECTED;
            HUD_SetStatus("AUTH FALLIDA!");
            break;

        //-- SALA ─────────────────────────────────────────────────────────────
        case CMD_ROOM_INFO:
            // payload: [ROOM_ID, GAME_ID, N_PLAYERS, MY_PID]
            if(len >= 4)
            {
                g_RoomId        = payload[0];
                g_PlayerCount   = payload[2];
                g_MyPid         = payload[3];

                Log_WriteHex("[ROOM] Info room=", g_RoomId);
                Log_WriteHex("[ROOM] Mi PID=", g_MyPid);
                Log_WriteHex("[ROOM] Jugadores=", g_PlayerCount);

                // Activar mi jugador y colocarlo en posición de inicio
                g_Players[g_MyPid].active = TRUE;
                g_Players[g_MyPid].x = g_StartX[g_MyPid];
                g_Players[g_MyPid].y = g_StartY[g_MyPid];

                g_State = STATE_PLAYING;
                HUD_SetStatus(g_ColorNames[g_MyPid]); // Mostrar color asignado
                g_HUDDirty = TRUE;
            }
            break;

        case CMD_ROOM_LIST:
            // payload: [N_ROOMS, ROOM_ID, GAME_ID, N_PLAYERS, ...]
            Log_WriteHex("[LOBBY] Lista recibida, salas=", len >= 1 ? payload[0] : 0);
            if(len >= 1)
            {
                g_LobbyCount = payload[0];
                if(g_LobbyCount > LOBBY_MAX_ROOMS)
                    g_LobbyCount = LOBBY_MAX_ROOMS;
                for(i = 0; i < g_LobbyCount; i++)
                {
                    g_LobbyRooms[i].roomId  = payload[1 + i * 3];
                    g_LobbyRooms[i].gameId  = payload[2 + i * 3];
                    g_LobbyRooms[i].players = payload[3 + i * 3];
                }
                g_LobbyCursor = 0;
                g_State = STATE_LOBBY;
                g_HUDDirty = TRUE;
            }
            break;

        case CMD_ROOM_FULL:
            Log_Write("[ROOM] Sala llena");
            g_State = STATE_LOBBY;
            HUD_SetStatus("SALA LLENA");
            Lobby_RequestList();
            break;

        case CMD_ROOM_NOT_FOUND:
            Log_Write("[ROOM] Sala no existe");
            g_State = STATE_LOBBY;
            HUD_SetStatus("SALA NO EXISTE");
            Lobby_RequestList();
            break;

        //-- EVENTOS DE SALA ───────────────────────────────────────────────────
        case CMD_PLAYER_JOINED:
            // payload[0] = PID del nuevo jugador
            Log_WriteHex("[ROOM] Player joined PID=", len >= 1 ? payload[0] : 0);
            if(len >= 1)
            {
                newPid = payload[0];
                if(newPid >= 1 && newPid <= MAX_PLAYERS)
                {
                    g_Players[newPid].active = TRUE;
                    g_Players[newPid].x = g_StartX[newPid];
                    g_Players[newPid].y = g_StartY[newPid];
                    g_PlayerCount++;
                    g_HUDDirty = TRUE;
                }
            }
            break;

        case CMD_PLAYER_LEFT:
            // payload[0] = PID del jugador que salió
            Log_WriteHex("[ROOM] Player left PID=", len >= 1 ? payload[0] : 0);
            if(len >= 1)
            {
                newPid = payload[0];
                if(newPid >= 1 && newPid <= MAX_PLAYERS)
                {
                    g_Players[newPid].active = FALSE;
                    if(g_PlayerCount > 0) g_PlayerCount--;
                    g_HUDDirty = TRUE;
                }
            }
            break;

        case CMD_GAME_START:
            HUD_SetStatus("JUEGO INICIADO");
            g_HUDDirty = TRUE;
            break;

        //-- ESTADO DE JUEGO ───────────────────────────────────────────────────
        case CMD_STATE_UPDATE:
            // pid = quién envió · payload = posición
            // Solo actualizar jugadores que NO somos nosotros
            if(pid >= 1 && pid <= MAX_PLAYERS && pid != g_MyPid && len >= STATE_PAYLOAD_SZ)
            {
                p = &g_Players[pid];
                p->x     = ((u16)payload[0] << 8) | payload[1];
                p->y     = ((u16)payload[2] << 8) | payload[3];
                p->frame = payload[4];
                p->flags = payload[5];
                // Asegurar que está activo (puede llegar STATE_UPDATE
                // antes que PLAYER_JOINED en condiciones de red lentas)
                p->active = TRUE;
            }
            break;

        case CMD_PONG:
            // Keepalive recibido — todo OK
            break;

        default:
            break;
    }
}

//=============================================================================
// RED — POLLING (llamar cada frame desde el bucle principal)
//=============================================================================

void Net_Poll(void)
{
    u16 avail;
    u8  header[PROTO_HEADER_SZ];
    u8  payload[256];
    u8  payloadLen;
    u8  maxPkts;    // Máx paquetes a procesar por frame (evitar bloqueo)

    if(g_Conn == NET_INVALID_CONN) return;

    //-- Verificar que la conexión sigue viva
    if(!Net_IsConnected(g_Conn))
    {
        Log_Write("[DISC] Conexion perdida");
        g_State = STATE_DISCONNECTED;
        HUD_SetStatus("CONEXION PERDIDA");
        return;
    }

    //-- Leer hasta 4 paquetes por frame
    //   (InterNestor procesa a 50 Hz; no esperamos más de 4 paquetes/frame)
    maxPkts = 4;
    while(maxPkts--)
    {
        avail = Net_Available(g_Conn);

        // ¿Hay suficientes bytes para un header completo?
        if(avail < PROTO_HEADER_SZ) break;

        // Leer el header
        Net_Recv(g_Conn, header, PROTO_HEADER_SZ);

        // Validar magic
        if(header[0] != PROTO_MAGIC_0 || header[1] != PROTO_MAGIC_1)
        {
            // Byte de basura en el buffer — poco probable con TCP limpio.
            Log_WriteHex("[NET] Magic invalido byte0=", header[0]);
            Log_WriteHex("[NET] Magic invalido byte1=", header[1]);
            break;
        }

        payloadLen = header[5];

        // ¿Tenemos el payload completo?
        if(payloadLen > 0)
        {
            avail = Net_Available(g_Conn);
            if(avail < payloadLen) break;   // Paquete incompleto, esperar
            Net_Recv(g_Conn, payload, payloadLen);
        }

        // Procesar paquete
        Net_HandlePacket(header[2], header[3], header[4], payload, payloadLen);
    }

    //-- Ping periódico para mantener viva la conexión
    g_PingTimer++;
    if(g_PingTimer >= PING_INTERVAL)
    {
        g_PingTimer = 0;
        Net_SendPing();
    }
}

//=============================================================================
// RED — CONEXIÓN INICIAL
//=============================================================================

void Net_Connect(void)
{
    //-- 1. Buscar implementación UNAPI TCP/IP
    Log_Write("[CONN] Buscando UNAPI...");
    if(Net_Init() != NET_OK)
    {
        g_State = STATE_DISCONNECTED;
        HUD_SetStatus("UNAPI no hallado");
        Log_Write("[CONN] UNAPI no hallado");
        HUD_Redraw();
        return;
    }
    Log_WriteHex("[CONN] UNAPI OK, impl=", g_NetImplCount);

    //-- 1b. Cerrar conexiones TCP fantasma de ejecuciones anteriores
    //   InterNestor mantiene conexiones abiertas tras salir del programa
    {
        u8 c;
        Log_Write("[CONN] Limpiando conexiones previas...");
        for(c = 0; c < 4; c++)
        {
            tcpip_tcp_abort((int)c);
        }
    }

    //-- 1c. Verificar IP local — dump raw del struct para depurar
    {
        u8 localIP[4];
        u8* raw;
        u8 j;
        if(Net_GetLocalIP(localIP))
        {
            Log_WriteHex("[CONN] IP local=", localIP[0]);
            Log_WriteHex("[CONN]         .", localIP[1]);
            Log_WriteHex("[CONN]         .", localIP[2]);
            Log_WriteHex("[CONN]         .", localIP[3]);
        }
        else
        {
            Log_Write("[CONN] SIN IP LOCAL");
        }
        // Dump raw de los primeros 16 bytes del struct g_IpInfo
        raw = (u8*)&g_IpInfo;
        Log_Write("[CONN] IpInfo raw:");
        for(j = 0; j < 16; j++)
        {
            Log_Hex8(raw[j]);
        }
        Log_Write("");
    }

    HUD_SetStatus("Conectando...");
    HUD_Redraw();

    //-- 2. Abrir conexión TCP al servidor
    Log_WriteHex("[CONN] Destino IP=", SERVER_IP[0]);
    Log_WriteHex("[CONN]          .", SERVER_IP[1]);
    Log_WriteHex("[CONN]          .", SERVER_IP[2]);
    Log_WriteHex("[CONN]          .", SERVER_IP[3]);
    Log_Write("[CONN] Abriendo conexion TCP...");
    g_Conn = Net_Open(SERVER_IP, SERVER_PORT);

    if(g_Conn == NET_INVALID_CONN)
    {
        g_State = STATE_DISCONNECTED;
        HUD_SetStatus("Fallo conexion");
        // Error codes UNAPI:
        // 1=NOT_IMP 2=NO_NETWORK 4=INV_PARAM 9=NO_FREE_CONN
        Log_WriteHex("[CONN] Net_Open FALLO err=", g_NetLastError);
        HUD_Redraw();
        return;
    }
    Log_WriteHex("[CONN] Handle=", (u8)g_Conn);

    //-- 3. Esperar a que TCP handshake complete (SYN_SENT → ESTABLISHED)
    //   tcpip_tcp_open es no-bloqueante: devuelve handle inmediatamente
    //   pero la conexión aún está en SYN_SENT hasta que llegue SYN-ACK.
    g_State = STATE_TCP_WAIT;
    g_PingTimer = 0;    // Reutilizar como timeout de conexión
    HUD_SetStatus("TCP handshake...");
    Log_Write("[CONN] Esperando ESTABLISHED...");
}

//=============================================================================
// INPUT Y MOVIMIENTO
//=============================================================================

void Input_ProcessMovement(void)
{
    Player* me;
    u8 keys;
    bool moved;

    if(g_State != STATE_PLAYING || g_MyPid == 0) return;

    me    = &g_Players[g_MyPid];
    keys  = Joystick_Read(JOY_PORT_1);  // Lee cursores + espacio como joystick 1
    moved = FALSE;

    me->flags = 0;

    if(keys & JOY_INPUT_DIR_UP)
    {
        if(me->y > PLAY_Y0)
        {
            me->y -= MOVE_SPEED;
            moved = TRUE;
        }
    }
    if(keys & JOY_INPUT_DIR_DOWN)
    {
        if(me->y < PLAY_Y1)
        {
            me->y += MOVE_SPEED;
            moved = TRUE;
            me->flags |= STATE_FLAG_DIR_D;
        }
    }
    if(keys & JOY_INPUT_DIR_LEFT)
    {
        if(me->x > PLAY_X0)
        {
            me->x -= MOVE_SPEED;
            moved = TRUE;
        }
    }
    if(keys & JOY_INPUT_DIR_RIGHT)
    {
        if(me->x < PLAY_X1)
        {
            me->x += MOVE_SPEED;
            moved = TRUE;
            me->flags |= STATE_FLAG_DIR_R;
        }
    }

    // Solo enviar cuando hay movimiento real (ahorra ancho de banda)
    if(moved)
    {
        me->frame = (me->frame + 1) & 0x07; // Simular animación (8 frames)
        Net_SendState();
    }
}

//=============================================================================
// LOBBY — LISTA DE SALAS
//=============================================================================

void Lobby_RequestList(void)
{
    Net_SendRoomList();
    g_State = STATE_LOBBY_WAIT;
}

void Lobby_Draw(void)
{
    u8 i;
    u8 y;

    // Limpiar área de juego
    VDP_CommandHMMV(0, HUD_HEIGHT, 256, 212 - HUD_HEIGHT, 0x00);
    VDP_CommandWait();

    //-- Título
    Print_SetColor(11, 0);  // Amarillo
    Print_SetPosition(8, HUD_HEIGHT + 4);
    Print_DrawText("SALA  JUEGO  JUGADORES");

    //-- Línea separadora
    VDP_CommandHMMV(8, HUD_HEIGHT + 14, 200, 1, 0x77);
    VDP_CommandWait();

    if(g_LobbyCount == 0)
    {
        Print_SetColor(14, 0);  // Gris
        Print_SetPosition(8, HUD_HEIGHT + 20);
        Print_DrawText("No hay salas abiertas");
    }
    else
    {
        for(i = 0; i < g_LobbyCount; i++)
        {
            y = HUD_HEIGHT + 18 + i * 10;
            if(y > 190) break;  // No salirse de pantalla

            // Cursor de selección
            if(i == g_LobbyCursor)
            {
                Print_SetColor(15, 0);  // Blanco (seleccionada)
                Print_SetPosition(2, y);
                Print_DrawChar('>');
            }
            else
            {
                Print_SetColor(7, 0);   // Cian
            }

            Print_SetPosition(8, y);
            Print_DrawChar('#');
            Print_DrawHex8(g_LobbyRooms[i].roomId);

            Print_SetPosition(56, y);
            Print_DrawHex8(g_LobbyRooms[i].gameId);

            Print_SetPosition(104, y);
            Print_DrawHex8(g_LobbyRooms[i].players);
            Print_DrawChar('/');
            Print_DrawChar('4');
        }
    }

    //-- Controles
    Print_SetColor(14, 0);
    Print_SetPosition(8, 194);
    Print_DrawText("C=Crear  ENTER=Unir  R=Refr  ESC");
}

void Lobby_ProcessInput(void)
{
    // Cursor arriba
    if(Keyboard_IsKeyPushed(KEY_UP))
    {
        if(g_LobbyCursor > 0) g_LobbyCursor--;
        g_HUDDirty = TRUE;
    }

    // Cursor abajo
    if(Keyboard_IsKeyPushed(KEY_DOWN))
    {
        if(g_LobbyCount > 0 && g_LobbyCursor < g_LobbyCount - 1)
            g_LobbyCursor++;
        g_HUDDirty = TRUE;
    }

    // ENTER — unirse a sala seleccionada
    if(Keyboard_IsKeyPushed(KEY_RET))
    {
        if(g_LobbyCount > 0)
        {
            Net_SendJoinRoom(g_LobbyRooms[g_LobbyCursor].roomId);
            g_State = STATE_ROOM_WAIT;
            HUD_SetStatus("Uniendo a sala...");
        }
    }

    // C — crear sala nueva
    if(Keyboard_IsKeyPushed(KEY_C))
    {
        g_State = STATE_CREATE_ROOM;
    }

    // R — refrescar lista
    if(Keyboard_IsKeyPushed(KEY_R))
    {
        HUD_SetStatus("Actualizando...");
        Lobby_RequestList();
    }

    // J — introducir Room ID manualmente
    if(Keyboard_IsKeyPushed(KEY_J))
    {
        g_JoinLen = 0;
        g_JoinDigits[0] = 0;
        g_JoinDigits[1] = 0;
        g_JoinDigits[2] = 0;
        g_State = STATE_JOIN_INPUT;
        g_HUDDirty = TRUE;
    }
}

//=============================================================================
// INPUT DE ROOM ID POR TECLADO
//=============================================================================

void JoinInput_Draw(void)
{
    u8 i;

    // Limpiar área de juego
    VDP_CommandHMMV(0, HUD_HEIGHT, 256, 212 - HUD_HEIGHT, 0x00);
    VDP_CommandWait();

    Print_SetColor(15, 0);
    Print_SetPosition(48, 60);
    Print_DrawText("Room ID (1-255):");

    // Mostrar dígitos introducidos
    Print_SetPosition(48, 80);
    Print_SetColor(11, 0);  // Amarillo
    for(i = 0; i < g_JoinLen; i++)
    {
        Print_DrawChar('0' + g_JoinDigits[i]);
    }
    Print_DrawChar('_');  // Cursor

    Print_SetColor(14, 0);
    Print_SetPosition(48, 110);
    Print_DrawText("ENTER=OK  ESC=Volver");
}

// Tabla de teclas numéricas (KEY_0..KEY_9 no son consecutivas en MSXgl)
static const u8 g_KeyDigits[10] = {
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
    KEY_5, KEY_6, KEY_7, KEY_8, KEY_9
};

void JoinInput_ProcessKey(void)
{
    u8 digit;
    u16 val;

    // Teclas 0-9
    for(digit = 0; digit <= 9; digit++)
    {
        if(Keyboard_IsKeyPushed(g_KeyDigits[digit]) && g_JoinLen < 3)
        {
            g_JoinDigits[g_JoinLen] = digit;
            g_JoinLen++;
            g_HUDDirty = TRUE;
        }
    }

    // Backspace — borrar último dígito
    if(Keyboard_IsKeyPushed(KEY_BS) && g_JoinLen > 0)
    {
        g_JoinLen--;
        g_HUDDirty = TRUE;
    }

    // ENTER — confirmar
    if(Keyboard_IsKeyPushed(KEY_RET))
    {
        if(g_JoinLen > 0)
        {
            // Convertir dígitos a valor
            val = 0;
            if(g_JoinLen >= 1) val = g_JoinDigits[0];
            if(g_JoinLen >= 2) val = val * 10 + g_JoinDigits[1];
            if(g_JoinLen >= 3) val = val * 10 + g_JoinDigits[2];

            if(val >= 1 && val <= 255)
            {
                g_State = STATE_JOIN_ROOM;
                Net_SendJoinRoom((u8)val);
                g_State = STATE_ROOM_WAIT;
            }
            else
            {
                HUD_SetStatus("ID invalido (1-255)");
                g_JoinLen = 0;
                g_HUDDirty = TRUE;
            }
        }
    }

    // ESC — volver al lobby
    if(Keyboard_IsKeyPushed(KEY_ESC))
    {
        HUD_SetStatus("Actualizando...");
        Lobby_RequestList();
    }
}

//=============================================================================
// LIMPIEZA AL SALIR
//=============================================================================

void Game_Shutdown(void)
{
    u8 i;

    Log_Write("[EXIT] Cerrando...");

    // Enviar ROOM_LEAVE si estamos en una sala
    if(g_State == STATE_PLAYING && g_RoomId != 0)
    {
        Log_Write("[EXIT] Enviando ROOM_LEAVE");
        Net_SendLeave();
        // Dar 2 frames para que el paquete salga
        Halt();
        Halt();
    }

    // Cerrar conexión TCP
    if(g_Conn != NET_INVALID_CONN)
    {
        Log_Write("[EXIT] Cerrando TCP");
        Net_Close(g_Conn);
        g_Conn = NET_INVALID_CONN;
    }

    // Cerrar fichero de log
    Log_Write("[EXIT] Fin");
    Log_Close();

    // Ocultar todos los sprites
    for(i = 0; i < MAX_PLAYERS; i++)
    {
        VDP_SetSpriteSM1(i, 0, 209, 0, 0);
    }

    // Volver a MSX-DOS limpiamente via BIOS
    // Bios_Exit restaura modo texto (CHGMOD+TOTEXT) y llama BDOS para terminar
    Bios_Exit(0);
}

//=============================================================================
// BUCLE PRINCIPAL
//=============================================================================

void Game_Loop(void)
{
    while(g_State != STATE_EXIT)
    {
        // Sincronizar con VBlank (50/60 Hz — ritmo del juego)
        Halt();

        // Actualizar buffer de teclado (necesario para Keyboard_IsKeyPushed)
        Keyboard_Update();

        //-- ESC: salir según contexto ────────────────────────────────────
        if(Keyboard_IsKeyPressed(KEY_ESC))
        {
            if(g_State == STATE_PLAYING)
            {
                g_State = STATE_EXIT;
            }
            else if(g_State == STATE_LOBBY)
            {
                g_State = STATE_EXIT;
            }
            else if(g_State == STATE_DISCONNECTED ||
                    g_State == STATE_TCP_WAIT     ||
                    g_State == STATE_AUTH_WAIT    ||
                    g_State == STATE_ROOM_WAIT)
            {
                g_State = STATE_EXIT;
            }
            // STATE_JOIN_INPUT maneja ESC internamente (vuelve a lobby)
        }

        //-- Máquina de estados ──────────────────────────────────────────────
        switch(g_State)
        {
            case STATE_CONNECTING:
                Net_Connect();
                break;

            case STATE_TCP_WAIT:
                // Esperar a que TCP pase de SYN_SENT a ESTABLISHED
                // Timeout: ~10 segundos (500 frames a 50Hz)
                g_PingTimer++;
                {
                    u8 tcpState = Net_GetConnState(g_Conn);

                    // Log cada 50 frames (~1s) para no saturar
                    if((g_PingTimer & 0x31) == 1)
                    {
                        Log_WriteHex("[WAIT] state=", tcpState);
                        Log_WriteHex("[WAIT] close_reason=", (u8)g_TcpParms.close_reason);
                        Log_WriteHex("[WAIT] err=", g_NetLastError);
                    }

                    if(tcpState == TCP_STATE_ESTABLISHED)
                    {
                        Log_Write("[CONN] ESTABLISHED OK");
                        g_State = STATE_AUTH_WAIT;
                        Log_Write("[AUTH] Enviando auth...");
                        Net_SendAuth();
                    }
                    else if(tcpState == 0xFF || tcpState == TCP_STATE_UNKNOWN)
                    {
                        // Conexion perdida o nunca se abrio
                        Log_WriteHex("[CONN] Fallo TCP state=", tcpState);
                        Log_WriteHex("[CONN] close_reason=", (u8)g_TcpParms.close_reason);
                        g_State = STATE_DISCONNECTED;
                        HUD_SetStatus("Fallo conexion TCP");
                    }
                    else if(g_PingTimer > 500)
                    {
                        Log_WriteHex("[CONN] Timeout state=", tcpState);
                        Log_WriteHex("[CONN] close_reason=", (u8)g_TcpParms.close_reason);
                        g_State = STATE_DISCONNECTED;
                        HUD_SetStatus("Timeout conexion");
                    }
                }
                HUD_Redraw();
                break;

            case STATE_AUTH_WAIT:
                Net_Poll();
                HUD_Redraw();
                break;

            case STATE_LOBBY_WAIT:
                Net_Poll();
                HUD_Redraw();
                break;

            case STATE_LOBBY:
                if(g_HUDDirty)
                {
                    HUD_Redraw();
                    Lobby_Draw();
                    g_HUDDirty = FALSE;
                }
                Lobby_ProcessInput();
                break;

            case STATE_JOIN_INPUT:
                if(g_HUDDirty)
                {
                    HUD_Redraw();
                    JoinInput_Draw();
                    g_HUDDirty = FALSE;
                }
                JoinInput_ProcessKey();
                break;

            case STATE_CREATE_ROOM:
                Net_SendCreateRoom();
                g_State = STATE_ROOM_WAIT;
                break;

            case STATE_ROOM_WAIT:
                Net_Poll();
                HUD_Redraw();
                break;

            case STATE_PLAYING:
                Input_ProcessMovement();
                Net_Poll();
                Sprites_Refresh();
                HUD_Redraw();
                break;

            case STATE_DISCONNECTED:
                HUD_Redraw();
                break;

            default:
                break;
        }
    }
}

//=============================================================================
// DIAGNOSTICO DE RED (Screen 0 — modo texto MSX-DOS)
//=============================================================================

// Imprime un byte como 3 digitos decimales (sin ceros a la izquierda)
void Diag_PrintDec(u8 val)
{
    u8 h, t, u;
    h = val / 100;
    t = (val % 100) / 10;
    u = val % 10;
    if(h > 0) DOS_CharOutput('0' + h);
    if(h > 0 || t > 0) DOS_CharOutput('0' + t);
    DOS_CharOutput('0' + u);
}

// Imprime una IP como xxx.xxx.xxx.xxx en decimal
void Diag_PrintIP(const u8* ip)
{
    u8 i;
    for(i = 0; i < 4; i++)
    {
        Diag_PrintDec(ip[i]);
        if(i < 3) DOS_CharOutput('.');
    }
}

void Diag_ShowNetInfo(void)
{
    u8 localIP[4];
    u8 ok;

    DOS_StringOutput("================================\r\n$");
    DOS_StringOutput("  MSX ONLINE - NET DIAGNOSTICS\r\n$");
    DOS_StringOutput("================================\r\n\r\n$");

    //-- 1. Buscar UNAPI
    DOS_StringOutput("UNAPI TCP/IP: $");
    ok = (tcpip_enumerate() > 0) ? 1 : 0;
    if(ok)
        DOS_StringOutput("ENCONTRADO\r\n$");
    else
    {
        DOS_StringOutput("NO ENCONTRADO\r\n\r\n$");
        DOS_StringOutput("Pulsa ESPACIO para salir...$");
        DOS_CharInput();
        Bios_Exit(0);
        return;
    }

    //-- 2. Obtener info IP
    tcpip_get_ipinfo(&g_IpInfo);

    //-- 3. IP local
    DOS_StringOutput("IP local    : $");
    localIP[0] = (u8)g_IpInfo.local_ip[0];
    localIP[1] = (u8)g_IpInfo.local_ip[1];
    localIP[2] = (u8)g_IpInfo.local_ip[2];
    localIP[3] = (u8)g_IpInfo.local_ip[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    //-- 4. Mascara de subred
    DOS_StringOutput("Mascara     : $");
    localIP[0] = (u8)g_IpInfo.subnet_mask[0];
    localIP[1] = (u8)g_IpInfo.subnet_mask[1];
    localIP[2] = (u8)g_IpInfo.subnet_mask[2];
    localIP[3] = (u8)g_IpInfo.subnet_mask[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    //-- 5. Gateway
    DOS_StringOutput("Gateway     : $");
    localIP[0] = (u8)g_IpInfo.gateway_ip[0];
    localIP[1] = (u8)g_IpInfo.gateway_ip[1];
    localIP[2] = (u8)g_IpInfo.gateway_ip[2];
    localIP[3] = (u8)g_IpInfo.gateway_ip[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    //-- 6. Servidor destino
    DOS_StringOutput("\r\nServidor    : $");
    Diag_PrintIP(SERVER_IP);
    DOS_StringOutput("\r\n$");

    //-- 7. Puerto
    DOS_StringOutput("Puerto      : $");
    {
        u16 port = SERVER_PORT;
        u8 d[5];
        u8 i, started;
        d[0] = (u8)(port / 10000); port %= 10000;
        d[1] = (u8)(port / 1000);  port %= 1000;
        d[2] = (u8)(port / 100);   port %= 100;
        d[3] = (u8)(port / 10);    port %= 10;
        d[4] = (u8)(port);
        started = 0;
        for(i = 0; i < 5; i++)
        {
            if(d[i] > 0 || started || i == 4)
            {
                DOS_CharOutput('0' + d[i]);
                started = 1;
            }
        }
    }
    DOS_StringOutput("\r\n$");

    //-- 8. Esperar tecla
    DOS_StringOutput("\r\nPulsa ESPACIO para continuar...$");
    DOS_CharInput();
    DOS_StringOutput("\r\n$");
}

//=============================================================================
// ENTRY POINT (MSX-DOS .COM)
//=============================================================================

void main(void)
{
    Diag_ShowNetInfo();
    Game_Init();
    g_State = STATE_CONNECTING;
    Game_Loop();
    Game_Shutdown();
}
