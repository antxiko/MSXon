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
    STATE_AUTH_WAIT,        // Esperando CMD_AUTH_OK
    STATE_CREATE_ROOM,      // Enviando CMD_ROOM_CREATE
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
void Net_SendLeave(void);
void Net_SendPing(void);
void Net_SendState(void);
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
    HUD_SetStatus("Auth enviada...");
}

void Net_SendCreateRoom(void)
{
    u8 payload[1];
    u8 len;
    payload[0] = GAME_ID_BALL;
    len = Packet_Build(CMD_ROOM_CREATE, 0, 0, payload, 1);
    Net_Send(g_Conn, g_SendBuf, len);
    HUD_SetStatus("Creando sala...");
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
    u8 newPid;
    Player* p;

    (void)room; // Ignorado — el servidor ya filtra por sala

    switch(cmd)
    {
        //-- AUTH ─────────────────────────────────────────────────────────────
        case CMD_AUTH_OK:
            g_State = STATE_CREATE_ROOM;
            HUD_SetStatus("Autenticado  OK");
            break;

        case CMD_AUTH_FAIL:
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

                // Activar mi jugador y colocarlo en posición de inicio
                g_Players[g_MyPid].active = TRUE;
                g_Players[g_MyPid].x = g_StartX[g_MyPid];
                g_Players[g_MyPid].y = g_StartY[g_MyPid];

                g_State = STATE_PLAYING;
                HUD_SetStatus(g_ColorNames[g_MyPid]); // Mostrar color asignado
                g_HUDDirty = TRUE;
            }
            break;

        case CMD_ROOM_FULL:
            g_State = STATE_DISCONNECTED;
            HUD_SetStatus("SALA LLENA");
            break;

        case CMD_ROOM_NOT_FOUND:
            g_State = STATE_DISCONNECTED;
            HUD_SetStatus("SALA NO EXISTE");
            break;

        //-- EVENTOS DE SALA ───────────────────────────────────────────────────
        case CMD_PLAYER_JOINED:
            // payload[0] = PID del nuevo jugador
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
            // Descartamos este "header" y esperamos al siguiente frame.
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
    if(Net_Init() != NET_OK)
    {
        g_State = STATE_DISCONNECTED;
        HUD_SetStatus("UNAPI no hallado");
        HUD_Redraw();
        return;
    }

    HUD_SetStatus("Conectando...");
    HUD_Redraw();

    //-- 2. Abrir conexión TCP al servidor
    g_Conn = Net_Open(SERVER_IP, SERVER_PORT);

    if(g_Conn == NET_INVALID_CONN)
    {
        g_State = STATE_DISCONNECTED;
        HUD_SetStatus("Fallo conexion");
        HUD_Redraw();
        return;
    }

    //-- 3. Conexión establecida → enviar auth
    g_State = STATE_AUTH_WAIT;
    Net_SendAuth();
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
// LIMPIEZA AL SALIR
//=============================================================================

void Game_Shutdown(void)
{
    u8 i;

    // Enviar ROOM_LEAVE si estamos en una sala
    if(g_State == STATE_PLAYING && g_RoomId != 0)
    {
        Net_SendLeave();
        // Dar 2 frames para que el paquete salga
        Halt();
        Halt();
    }

    // Cerrar conexión TCP
    if(g_Conn != NET_INVALID_CONN)
    {
        Net_Close(g_Conn);
        g_Conn = NET_INVALID_CONN;
    }

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

        //-- ESC: salir de sala / del juego ─────────────────────────────────
        if(Keyboard_IsKeyPressed(KEY_ESC))
        {
            if(g_State == STATE_PLAYING)
            {
                // ESC en partida → salir de sala y terminar
                g_State = STATE_EXIT;
            }
            else if(g_State == STATE_DISCONNECTED ||
                    g_State == STATE_AUTH_WAIT    ||
                    g_State == STATE_ROOM_WAIT)
            {
                // ESC en pantalla de error/espera → salir
                g_State = STATE_EXIT;
            }
        }

        //-- Máquina de estados ──────────────────────────────────────────────
        switch(g_State)
        {
            case STATE_CONNECTING:
                Net_Connect();      // Bloquea hasta conectar o fallar
                break;

            case STATE_AUTH_WAIT:
                Net_Poll();         // Esperar CMD_AUTH_OK
                HUD_Redraw();
                break;

            case STATE_CREATE_ROOM:
                Net_SendCreateRoom();
                g_State = STATE_ROOM_WAIT;
                break;

            case STATE_ROOM_WAIT:
                Net_Poll();         // Esperar CMD_ROOM_INFO
                HUD_Redraw();
                break;

            case STATE_PLAYING:
                Input_ProcessMovement();
                Net_Poll();
                Sprites_Refresh();
                HUD_Redraw();       // Solo redibuja si g_HUDDirty == TRUE
                break;

            case STATE_DISCONNECTED:
                // Pantalla de error — esperar ESC (ver bloque ESC arriba)
                HUD_Redraw();
                break;

            default:
                break;
        }
    }
}

//=============================================================================
// ENTRY POINT (MSX-DOS .COM)
//=============================================================================

void main(void)
{
    Game_Init();
    g_State = STATE_CONNECTING;
    Game_Loop();
    Game_Shutdown();
}
