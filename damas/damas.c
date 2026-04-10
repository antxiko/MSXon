//=============================================================================
// damas.c — Damas Online · 2 jugadores
//
// MSX2 · MSX-DOS 2 · Screen 4 (Graphic 3)
// Tablero 8x8, tiles 8x8, vista cenital
// GAME_ID = 0x02
//=============================================================================

#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"
#include "protocol.h"
#include "network.h"
#include "log.h"
#include "lobby_client.h"

//=============================================================================
// CONSTANTES
//=============================================================================

#define GAME_ID_DAMAS   0x02

// Tablero: 8x8 casillas, cada casilla 2x2 tiles = 16x16 tiles para el tablero
// Tablero empieza en tile (8,4) para centrarlo: (8+16=24, 4+16=20)
#define BOARD_TX        8       // Columna tile inicio tablero
#define BOARD_TY        4       // Fila tile inicio tablero
#define BOARD_SIZE      8       // 8x8 casillas

// Tiles (solo tablero)
#define TILE_EMPTY      0       // Fondo negro
#define TILE_LIGHT_TL   1       // Casilla clara 2x2
#define TILE_LIGHT_TR   2
#define TILE_LIGHT_BL   3
#define TILE_LIGHT_BR   4
#define TILE_DARK_TL    5       // Casilla oscura 2x2
#define TILE_DARK_TR    6
#define TILE_DARK_BL    7
#define TILE_DARK_BR    8
// Fuente HUD (tiles 9-51)
#define TILE_FONT_BASE  9
#define TILE_SPC        9
// A=10 ... Z=35
// 0=36 ... 9=45
#define TILE_COLON      46
#define TILE_SLASH      47
#define TILE_CURSOR_T   48   // > cursor
#define TILE_COUNT      49

// Sprites: pattern 0 = ficha (circulo 16x16), pattern 1 = cursor (marco 16x16)
#define SPR_PAT_PIECE   0       // Pattern index ficha (4 bloques)
#define SPR_PAT_CURSOR  4       // Pattern index cursor (4 bloques)
#define SPR_CURSOR      0       // Sprite 0 = cursor (mayor prioridad, encima de todo)
#define SPR_PIECES_START 1      // Sprites 1-24 = fichas

// Red
static const u8 SERVER_IP[4] = { 217, 154, 107, 144 };
#define SERVER_PORT     9876
#define PING_INTERVAL   250
#define CMD_WORLD_STATE 0x41
#define PROTO_VERSION_AGGREGATE 0x02

//=============================================================================
// TILES: patterns y colores
//=============================================================================

const u8 g_TilePatterns[] = {
    // 0: vacio (negro)
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 1-4: casilla clara — solido
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    // 5-8: casilla oscura — solido
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    // --- FUENTE (tiles 9-48) ---
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 9: espacio
    0x1C,0x22,0x22,0x3E,0x22,0x22,0x22,0x00, // 10: A
    0x3C,0x22,0x3C,0x22,0x22,0x3C,0x00,0x00, // 11: B
    0x1C,0x22,0x20,0x20,0x22,0x1C,0x00,0x00, // 12: C
    0x3C,0x22,0x22,0x22,0x22,0x3C,0x00,0x00, // 13: D
    0x3E,0x20,0x3C,0x20,0x20,0x3E,0x00,0x00, // 14: E
    0x3E,0x20,0x3C,0x20,0x20,0x20,0x00,0x00, // 15: F
    0x1C,0x22,0x20,0x2E,0x22,0x1C,0x00,0x00, // 16: G
    0x22,0x22,0x3E,0x22,0x22,0x22,0x00,0x00, // 17: H
    0x1C,0x08,0x08,0x08,0x08,0x1C,0x00,0x00, // 18: I
    0x0E,0x04,0x04,0x04,0x24,0x18,0x00,0x00, // 19: J
    0x22,0x24,0x38,0x24,0x22,0x22,0x00,0x00, // 20: K
    0x20,0x20,0x20,0x20,0x20,0x3E,0x00,0x00, // 21: L
    0x22,0x36,0x2A,0x22,0x22,0x22,0x00,0x00, // 22: M
    0x22,0x32,0x2A,0x26,0x22,0x22,0x00,0x00, // 23: N
    0x1C,0x22,0x22,0x22,0x22,0x1C,0x00,0x00, // 24: O
    0x3C,0x22,0x3C,0x20,0x20,0x20,0x00,0x00, // 25: P
    0x1C,0x22,0x22,0x2A,0x24,0x1A,0x00,0x00, // 26: Q
    0x3C,0x22,0x3C,0x24,0x22,0x22,0x00,0x00, // 27: R
    0x1C,0x20,0x1C,0x02,0x22,0x1C,0x00,0x00, // 28: S
    0x3E,0x08,0x08,0x08,0x08,0x08,0x00,0x00, // 29: T
    0x22,0x22,0x22,0x22,0x22,0x1C,0x00,0x00, // 30: U
    0x22,0x22,0x22,0x14,0x14,0x08,0x00,0x00, // 31: V
    0x22,0x22,0x22,0x2A,0x36,0x22,0x00,0x00, // 32: W
    0x22,0x14,0x08,0x14,0x22,0x22,0x00,0x00, // 33: X
    0x22,0x14,0x08,0x08,0x08,0x08,0x00,0x00, // 34: Y
    0x3E,0x04,0x08,0x10,0x20,0x3E,0x00,0x00, // 35: Z
    0x1C,0x22,0x26,0x2A,0x32,0x1C,0x00,0x00, // 36: 0
    0x08,0x18,0x08,0x08,0x08,0x1C,0x00,0x00, // 37: 1
    0x1C,0x22,0x04,0x08,0x10,0x3E,0x00,0x00, // 38: 2
    0x1C,0x02,0x0C,0x02,0x22,0x1C,0x00,0x00, // 39: 3
    0x04,0x0C,0x14,0x3E,0x04,0x04,0x00,0x00, // 40: 4
    0x3E,0x20,0x3C,0x02,0x22,0x1C,0x00,0x00, // 41: 5
    0x1C,0x20,0x3C,0x22,0x22,0x1C,0x00,0x00, // 42: 6
    0x3E,0x02,0x04,0x08,0x10,0x10,0x00,0x00, // 43: 7
    0x1C,0x22,0x1C,0x22,0x22,0x1C,0x00,0x00, // 44: 8
    0x1C,0x22,0x1E,0x02,0x04,0x18,0x00,0x00, // 45: 9
    0x00,0x08,0x00,0x00,0x08,0x00,0x00,0x00, // 46: :
    0x02,0x04,0x08,0x10,0x20,0x00,0x00,0x00, // 47: /
    0x10,0x18,0x1C,0x1E,0x1C,0x18,0x10,0x00, // 48: >
};

const u8 g_TileColors[] = {
    // 0: vacio
    0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    // 1-4: casilla clara — beige (B)
    0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,
    0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,
    0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,
    0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,0xB0,
    // 5-8: casilla oscura — marron (9)
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    // --- FUENTE colores (40 tiles, blanco sobre negro) ---
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // SPC
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // A
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // B
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // C
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // D
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // E
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // F
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // G
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // H
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // I
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // J
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // K
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // L
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // M
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // N
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // O
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // P
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // Q
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // R
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // S
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // T
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // U
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // V
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // W
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // X
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // Y
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // Z
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 0
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 1
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 2
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 3
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 4
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 5
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 6
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 7
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 8
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 9
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // :
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // /
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // >
};

//=============================================================================
// SPRITE PATTERNS: ficha redonda 16x16 + cursor marco 16x16
//=============================================================================

// Ficha: circulo solido 14x14 (con 1px margen)
const u8 g_PieceSprite[32] = {
    // Left top (filas 0-7)
    0x00,0x07,0x0F,0x1F,0x1F,0x3F,0x3F,0x3F,
    // Left bottom (filas 8-15)
    0x3F,0x3F,0x1F,0x1F,0x0F,0x07,0x00,0x00,
    // Right top (filas 0-7)
    0x00,0xE0,0xF0,0xF8,0xF8,0xFC,0xFC,0xFC,
    // Right bottom (filas 8-15)
    0xFC,0xFC,0xF8,0xF8,0xF0,0xE0,0x00,0x00,
};

//=============================================================================
// SPRITE: cursor de seleccion 16x16
//=============================================================================

const u8 g_CursorSprite[32] = {
    // Left top
    0xF0,0x80,0x80,0x80,0x00,0x00,0x00,0x00,
    // Left bottom
    0x00,0x00,0x00,0x00,0x80,0x80,0x80,0xF0,
    // Right top
    0x0F,0x01,0x01,0x01,0x00,0x00,0x00,0x00,
    // Right bottom
    0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x0F,
};

//=============================================================================
// ESTADO DEL JUEGO
//=============================================================================

// Tablero: 0=vacio, 1=blanca, 2=negra, 3=dama blanca, 4=dama negra
#define PIECE_NONE      0
#define PIECE_WHITE     1
#define PIECE_BLACK     2
#define PIECE_WHITE_KING 3
#define PIECE_BLACK_KING 4

static u8 g_Board[BOARD_SIZE][BOARD_SIZE];

// Cursor
static u8 g_CursorX = 0;   // 0-7 posicion en tablero
static u8 g_CursorY = 0;
static u8 g_Selected = 0;  // 0=nada seleccionado, 1=ficha seleccionada
static u8 g_SelX = 0;      // Posicion de ficha seleccionada
static u8 g_SelY = 0;

// Turno: 1=blancas, 2=negras
static u8 g_Turn = PIECE_WHITE;
static u8 g_MyColor = 0;   // Que color soy yo (0=offline/no asignado)
static u8 g_MustCapture = 0; // 1=ficha actual debe seguir capturando
static u8 g_CapX = 0;       // Posicion de ficha que esta capturando
static u8 g_CapY = 0;

// Red
static NetConn g_Conn = NET_INVALID_CONN;
static u8 g_MyPid = 0;
static u8 g_RoomId = 0;
static u16 g_PingTimer = 0;
static u8 g_SendBuf[20];
static u8 g_MoveDelay = 0;
static bool g_BoardDirty = TRUE;

// Name table buffer
static u8 g_NameBuf[768];
#define NB_DIRTY_MAX 128
static u16 g_NbDirtyIdx[NB_DIRTY_MAX];
static u8 g_NbDirtyCount = 0;
static bool g_FullFlush = TRUE;

//=============================================================================
// BUFFER HELPERS
//=============================================================================

void Buf_PutText(u8 col, u8 row, const c8* text)
{
    while(*text && col < 32)
    {
        u8 ch = (u8)*text;
        u8 tile;
        if(ch >= 'A' && ch <= 'Z')
            tile = 10 + (ch - 'A');
        else if(ch >= 'a' && ch <= 'z')
            tile = 10 + (ch - 'a');
        else if(ch >= '0' && ch <= '9')
            tile = 36 + (ch - '0');
        else if(ch == ':')
            tile = TILE_COLON;
        else if(ch == '/')
            tile = TILE_SLASH;
        else
            tile = TILE_SPC;
        {
            u16 idx = (u16)row * 32 + col;
            if(g_NameBuf[idx] != tile)
            {
                g_NameBuf[idx] = tile;
                if(g_NbDirtyCount < NB_DIRTY_MAX)
                    g_NbDirtyIdx[g_NbDirtyCount++] = idx;
                else
                    g_FullFlush = TRUE;
            }
        }
        col++;
        text++;
    }
}

void Buf_PutNum(u8 col, u8 row, u8 val)
{
    u8 h, t, u;
    h = val / 100;
    t = (val % 100) / 10;
    u = val % 10;
    if(h > 0) { Buf_PutText(col++, row, ""); } // placeholder
    // Simplificado: escribir directo
    {
        c8 str[4];
        u8 idx = 0;
        if(h > 0) str[idx++] = '0' + h;
        if(h > 0 || t > 0) str[idx++] = '0' + t;
        str[idx++] = '0' + u;
        str[idx] = 0;
        Buf_PutText(col, row, str);
    }
}

void Buf_PutTile(u8 col, u8 row, u8 tile)
{
    u16 idx = (u16)row * 32 + col;
    if(g_NameBuf[idx] != tile)
    {
        g_NameBuf[idx] = tile;
        if(g_NbDirtyCount < NB_DIRTY_MAX)
            g_NbDirtyIdx[g_NbDirtyCount++] = idx;
        else
            g_FullFlush = TRUE;
    }
}

//=============================================================================
// TABLERO: INICIALIZAR
//=============================================================================

// Estado fin de partida
#define GAME_RESULT_NONE    0
#define GAME_RESULT_WIN     1
#define GAME_RESULT_LOSE    2
#define GAME_RESULT_DISC    3  // Rival desconectado
static u8 g_GameResult = GAME_RESULT_NONE;

// Helpers de piezas
bool IsWhite(u8 p) { return (p == PIECE_WHITE || p == PIECE_WHITE_KING); }
bool IsBlack(u8 p) { return (p == PIECE_BLACK || p == PIECE_BLACK_KING); }
bool IsKing(u8 p) { return (p == PIECE_WHITE_KING || p == PIECE_BLACK_KING); }
bool IsEnemy(u8 a, u8 b) { return (IsWhite(a) && IsBlack(b)) || (IsBlack(a) && IsWhite(b)); }

bool HasValidMoves(u8 color)
{
    u8 x, y;
    for(y = 0; y < BOARD_SIZE; y++)
    {
        for(x = 0; x < BOARD_SIZE; x++)
        {
            u8 p = g_Board[y][x];
            if(p == PIECE_NONE) continue;
            if(color == PIECE_WHITE && !IsWhite(p)) continue;
            if(color == PIECE_BLACK && !IsBlack(p)) continue;

            // Comprobar si esta pieza puede mover a alguna casilla
            {
                i8 dx, dy;
                if(IsKing(p))
                {
                    // Dama: 4 diagonales
                    for(dy = -1; dy <= 1; dy += 2)
                        for(dx = -1; dx <= 1; dx += 2)
                        {
                            u8 tx = x + dx;
                            u8 ty = y + dy;
                            if(tx < BOARD_SIZE && ty < BOARD_SIZE && g_Board[ty][tx] == PIECE_NONE)
                                return TRUE;
                            // Captura
                            tx = x + dx * 2;
                            ty = y + dy * 2;
                            if(tx < BOARD_SIZE && ty < BOARD_SIZE &&
                               g_Board[ty][tx] == PIECE_NONE &&
                               IsEnemy(p, g_Board[y+dy][x+dx]))
                                return TRUE;
                        }
                }
                else
                {
                    i8 fwd = (color == PIECE_WHITE) ? -1 : 1;
                    for(dx = -1; dx <= 1; dx += 2)
                    {
                        u8 tx = x + dx;
                        u8 ty = y + fwd;
                        if(tx < BOARD_SIZE && ty < BOARD_SIZE && g_Board[ty][tx] == PIECE_NONE)
                            return TRUE;
                    }
                    // Capturas en 4 direcciones
                    for(dy = -1; dy <= 1; dy += 2)
                        for(dx = -1; dx <= 1; dx += 2)
                        {
                            u8 tx = x + dx * 2;
                            u8 ty = y + dy * 2;
                            if(tx < BOARD_SIZE && ty < BOARD_SIZE &&
                               g_Board[ty][tx] == PIECE_NONE &&
                               IsEnemy(p, g_Board[y+dy][x+dx]))
                                return TRUE;
                        }
                }
            }
        }
    }
    return FALSE;
}

void CheckGameOver(void)
{
    u8 x, y, whites, blacks;
    whites = 0;
    blacks = 0;
    for(y = 0; y < BOARD_SIZE; y++)
        for(x = 0; x < BOARD_SIZE; x++)
        {
            if(IsWhite(g_Board[y][x])) whites++;
            if(IsBlack(g_Board[y][x])) blacks++;
        }

    if(g_MyColor == 0) return;

    // Sin fichas
    if(whites == 0)
        g_GameResult = (g_MyColor == PIECE_BLACK) ? GAME_RESULT_WIN : GAME_RESULT_LOSE;
    else if(blacks == 0)
        g_GameResult = (g_MyColor == PIECE_WHITE) ? GAME_RESULT_WIN : GAME_RESULT_LOSE;
    // Sin movimientos (solo comprobar en mi turno)
    else if(g_Turn == g_MyColor && !HasValidMoves(g_MyColor))
        g_GameResult = GAME_RESULT_LOSE;
    else if(g_Turn != g_MyColor && !HasValidMoves(g_Turn))
        g_GameResult = GAME_RESULT_WIN;
}

// Forward declarations
void Net_SendMove(u8 fromX, u8 fromY, u8 toX, u8 toY, u8 endTurn);
void Lobby_Draw(void);

void Board_Init(void)
{
    u8 x, y;
    for(y = 0; y < BOARD_SIZE; y++)
    {
        for(x = 0; x < BOARD_SIZE; x++)
        {
            g_Board[y][x] = PIECE_NONE;

            // Fichas solo en casillas oscuras (x+y impar)
            if((x + y) & 1)
            {
                if(y < 3)
                    g_Board[y][x] = PIECE_BLACK;
                else if(y > 4)
                    g_Board[y][x] = PIECE_WHITE;
            }
        }
    }
}

//=============================================================================
// TABLERO: DIBUJAR AL BUFFER
//=============================================================================

void Board_Draw(void)
{
    u8 bx, by, tx, ty;
    u16 idx;

    // Limpiar todo
    for(idx = 0; idx < 768; idx++) g_NameBuf[idx] = TILE_EMPTY;
    g_FullFlush = TRUE;

    // Solo dibujar casillas (las fichas son sprites)
    for(by = 0; by < BOARD_SIZE; by++)
    {
        for(bx = 0; bx < BOARD_SIZE; bx++)
        {
            tx = BOARD_TX + bx * 2;
            ty = BOARD_TY + by * 2;

            if((bx + by) & 1)
            {
                Buf_PutTile(tx,     ty,     TILE_DARK_TL);
                Buf_PutTile(tx + 1, ty,     TILE_DARK_TR);
                Buf_PutTile(tx,     ty + 1, TILE_DARK_BL);
                Buf_PutTile(tx + 1, ty + 1, TILE_DARK_BR);
            }
            else
            {
                Buf_PutTile(tx,     ty,     TILE_LIGHT_TL);
                Buf_PutTile(tx + 1, ty,     TILE_LIGHT_TR);
                Buf_PutTile(tx,     ty + 1, TILE_LIGHT_BL);
                Buf_PutTile(tx + 1, ty + 1, TILE_LIGHT_BR);
            }
        }
    }

    g_BoardDirty = FALSE;
}

//=============================================================================
// FICHAS: DIBUJAR COMO SPRITES
//=============================================================================

void Pieces_Draw(void)
{
    u8 bx, by, sprIdx;

    sprIdx = SPR_PIECES_START;

    for(by = 0; by < BOARD_SIZE; by++)
    {
        for(bx = 0; bx < BOARD_SIZE; bx++)
        {
            u8 piece = g_Board[by][bx];
            if(piece != PIECE_NONE && sprIdx < 31)
            {
                u8 px = (BOARD_TX + bx * 2) * 8;
                u8 py = (BOARD_TY + by * 2) * 8;
                u8 color;
                // Normal: blanco=15, negro=1. Dama: blanco=11(amarillo), negro=8(rojo)
                if(piece == PIECE_WHITE) color = 15;
                else if(piece == PIECE_BLACK) color = 1;
                else if(piece == PIECE_WHITE_KING) color = 11;
                else color = 8; // PIECE_BLACK_KING
                VDP_SetSpriteExUniColor(sprIdx, px, py, 0, color);
                sprIdx++;
            }
        }
    }

    // Ocultar sprites no usados
    for(; sprIdx < 31; sprIdx++)
        VDP_SetSpriteExUniColor(sprIdx, 0, 209, 0, 0);
}

//=============================================================================
// CURSOR SPRITE
//=============================================================================

void Cursor_Draw(void)
{
    u8 px = (BOARD_TX + g_CursorX * 2) * 8;
    u8 py = (BOARD_TY + g_CursorY * 2) * 8;
    u8 color;

    // Verde si nada seleccionado, rojo si seleccionado
    color = g_Selected ? 8 : 3;

    VDP_SetSpriteExUniColor(SPR_CURSOR, px, py, 4, color);
}

//=============================================================================
// VDP
//=============================================================================

void VDP_LoadTileset(void)
{
    u8 tile, bank;
    for(bank = 0; bank < 3; bank++)
    {
        u16 patBase = bank * 2048;
        u16 colBase = 0x2000 + bank * 2048;
        for(tile = 0; tile < TILE_COUNT; tile++)
        {
            VDP_WriteVRAM(g_TilePatterns + tile * 8, patBase + tile * 8, 0, 8);
            VDP_WriteVRAM(g_TileColors + tile * 8, colBase + tile * 8, 0, 8);
        }
    }
}

//=============================================================================
// LOGICA DE DAMAS
//=============================================================================

bool Move_IsValid(u8 fromX, u8 fromY, u8 toX, u8 toY)
{
    u8 piece;
    i8 dx, dy, sx, sy, dist, i;

    piece = g_Board[fromY][fromX];
    if(piece == PIECE_NONE) return FALSE;
    if(g_Board[toY][toX] != PIECE_NONE) return FALSE;
    if(toX >= BOARD_SIZE || toY >= BOARD_SIZE) return FALSE;
    if(!((toX + toY) & 1)) return FALSE;

    dx = (i8)toX - (i8)fromX;
    dy = (i8)toY - (i8)fromY;

    // Debe ser diagonal
    if(dx == 0 || dy == 0) return FALSE;
    if(dx < 0) { sx = -1; dist = -dx; } else { sx = 1; dist = dx; }
    if(dy < 0) sy = -1; else sy = 1;
    if(dist != (dy < 0 ? -dy : dy)) return FALSE; // No es diagonal perfecta

    if(IsKing(piece))
    {
        // Dama: puede moverse cualquier distancia en diagonal
        // Contar piezas enemigas en el camino (max 1 para captura)
        u8 enemies = 0;
        u8 enemyX = 0, enemyY = 0;

        for(i = 1; i < dist; i++)
        {
            u8 cx = fromX + sx * i;
            u8 cy = fromY + sy * i;
            u8 cp = g_Board[cy][cx];
            if(cp != PIECE_NONE)
            {
                if(IsEnemy(piece, cp))
                {
                    enemies++;
                    enemyX = cx;
                    enemyY = cy;
                    if(enemies > 1) return FALSE; // Mas de 1 pieza en el camino
                }
                else
                {
                    return FALSE; // Pieza propia en el camino
                }
            }
        }
        // Movimiento libre o captura de 1 enemiga
        return TRUE;
    }
    else
    {
        // Ficha normal: solo 1 casilla adelante
        if(dist == 1)
        {
            // Blancas avanzan hacia arriba (dy=-1), negras hacia abajo (dy=1)
            if(piece == PIECE_WHITE && dy == -1) return TRUE;
            if(piece == PIECE_BLACK && dy == 1) return TRUE;
        }
        // Captura: 2 casillas en diagonal, pieza enemiga en medio
        if(dist == 2)
        {
            u8 midX = fromX + sx;
            u8 midY = fromY + sy;
            u8 midPiece = g_Board[midY][midX];
            if(IsEnemy(piece, midPiece)) return TRUE;
        }
    }

    return FALSE;
}

bool CanCapture(u8 x, u8 y)
{
    u8 piece = g_Board[y][x];
    i8 sx, sy, i;

    if(IsKing(piece))
    {
        // Dama: buscar capturas en las 4 diagonales a cualquier distancia
        for(sy = -1; sy <= 1; sy += 2)
        {
            for(sx = -1; sx <= 1; sx += 2)
            {
                u8 foundEnemy = 0;
                for(i = 1; i < BOARD_SIZE; i++)
                {
                    u8 cx = x + sx * i;
                    u8 cy = y + sy * i;
                    u8 cp;
                    if(cx >= BOARD_SIZE || cy >= BOARD_SIZE) break;
                    cp = g_Board[cy][cx];
                    if(cp == PIECE_NONE)
                    {
                        if(foundEnemy) return TRUE; // Casilla libre tras enemiga
                        continue;
                    }
                    if(IsEnemy(piece, cp) && !foundEnemy)
                    {
                        foundEnemy = 1;
                        continue;
                    }
                    break; // Pieza propia o segunda enemiga
                }
            }
        }
    }
    else
    {
        // Ficha normal: solo captura a 2 casillas
        i8 dx, dy;
        for(dy = -2; dy <= 2; dy += 4)
        {
            for(dx = -2; dx <= 2; dx += 4)
            {
                u8 toX = x + dx;
                u8 toY = y + dy;
                u8 midX = x + dx / 2;
                u8 midY = y + dy / 2;
                if(toX >= BOARD_SIZE || toY >= BOARD_SIZE) continue;
                if(g_Board[toY][toX] != PIECE_NONE) continue;
                if(!IsEnemy(piece, g_Board[midY][midX])) continue;
                return TRUE;
            }
        }
    }
    return FALSE;
}

void Move_Execute(u8 fromX, u8 fromY, u8 toX, u8 toY)
{
    i8 dx, dy, sx, sy, dist, i;
    u8 wasCapture = 0;
    u8 piece;

    piece = g_Board[fromY][fromX];
    g_Board[toY][toX] = piece;
    g_Board[fromY][fromX] = PIECE_NONE;

    dx = (i8)toX - (i8)fromX;
    dy = (i8)toY - (i8)fromY;
    sx = (dx > 0) ? 1 : -1;
    sy = (dy > 0) ? 1 : -1;
    dist = (dx > 0) ? dx : -dx;

    // Buscar y quitar pieza capturada en el camino
    for(i = 1; i < dist; i++)
    {
        u8 cx = fromX + sx * i;
        u8 cy = fromY + sy * i;
        if(g_Board[cy][cx] != PIECE_NONE)
        {
            g_Board[cy][cx] = PIECE_NONE;
            wasCapture = 1;
            break;
        }
    }

    // Promocion: ficha llega al extremo opuesto
    if(piece == PIECE_WHITE && toY == 0)
        g_Board[toY][toX] = PIECE_WHITE_KING;
    if(piece == PIECE_BLACK && toY == BOARD_SIZE - 1)
        g_Board[toY][toX] = PIECE_BLACK_KING;

    // Si fue captura, comprobar si puede seguir capturando
    if(wasCapture && CanCapture(toX, toY))
    {
        // Debe seguir capturando con la misma ficha
        g_MustCapture = 1;
        g_CapX = toX;
        g_CapY = toY;
        g_Selected = 1;
        g_SelX = toX;
        g_SelY = toY;
        g_CursorX = toX;
        g_CursorY = toY;
    }
    else
    {
        // Cambiar turno
        g_MustCapture = 0;
        g_Turn = (g_Turn == PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
    }

    g_BoardDirty = TRUE;
}

//=============================================================================
// INPUT
//=============================================================================

void Game_ProcessInput(void)
{
    u8 fX, fY; // Para guardar from antes de Move_Execute

    if(g_MoveDelay > 0) { g_MoveDelay--; return; }

    if(Keyboard_IsKeyPressed(KEY_UP))
    {
        if(g_CursorY > 0) g_CursorY--;
        g_MoveDelay = 8;
    }
    if(Keyboard_IsKeyPressed(KEY_DOWN))
    {
        if(g_CursorY < BOARD_SIZE - 1) g_CursorY++;
        g_MoveDelay = 8;
    }
    if(Keyboard_IsKeyPressed(KEY_LEFT))
    {
        if(g_CursorX > 0) g_CursorX--;
        g_MoveDelay = 8;
    }
    if(Keyboard_IsKeyPressed(KEY_RIGHT))
    {
        if(g_CursorX < BOARD_SIZE - 1) g_CursorX++;
        g_MoveDelay = 8;
    }

    if(Keyboard_IsKeyPressed(KEY_RET) || Keyboard_IsKeyPressed(KEY_SPACE))
    {
        g_MoveDelay = 15;

        if(!g_Selected)
        {
            // Si hay captura obligatoria, solo la ficha que captura
            if(g_MustCapture)
            {
                g_Selected = 1;
                g_SelX = g_CapX;
                g_SelY = g_CapY;
                g_CursorX = g_CapX;
                g_CursorY = g_CapY;
                return;
            }

            // Seleccionar ficha
            {
                u8 piece = g_Board[g_CursorY][g_CursorX];

                // En modo online, solo puedo mover mis fichas y en mi turno
                if(g_MyColor != 0)
                {
                    // Online: solo mis fichas en mi turno
                    if(g_MyColor == PIECE_WHITE && !IsWhite(piece)) return;
                    if(g_MyColor == PIECE_BLACK && !IsBlack(piece)) return;
                    if(g_Turn != g_MyColor) return;
                }
                else
                {
                    // Offline: solo fichas del turno actual
                    if(g_Turn == PIECE_WHITE && !IsWhite(piece)) return;
                    if(g_Turn == PIECE_BLACK && !IsBlack(piece)) return;
                }

                g_Selected = 1;
                g_SelX = g_CursorX;
                g_SelY = g_CursorY;
            }
        }
        else
        {
            // Intentar mover
            if(g_CursorX == g_SelX && g_CursorY == g_SelY)
            {
                // Deseleccionar
                g_Selected = 0;
            }
            else if(Move_IsValid(g_SelX, g_SelY, g_CursorX, g_CursorY))
            {
                fX = g_SelX;
                fY = g_SelY;
                Move_Execute(fX, fY, g_CursorX, g_CursorY);
                // endTurn=1 si no hay multi-captura, 0 si sigue
                Net_SendMove(fX, fY, g_CursorX, g_CursorY, g_MustCapture ? 0 : 1);
                Log_WriteHex("[TX] fX=", fX);
                Log_WriteHex("[TX] tX=", g_CursorX);
                Log_WriteHex("[TX] end=", g_MustCapture ? 0 : 1);
                CheckGameOver();
                // Solo deseleccionar si NO hay multi-captura
                if(!g_MustCapture)
                    g_Selected = 0;
            }
            else
            {
                // Movimiento invalido — deseleccionar
                g_Selected = 0;
            }
        }
    }
}

//=============================================================================
// DIAGNOSTICO DE RED
//=============================================================================

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
    DOS_StringOutput("   DAMAS ONLINE - DIAGNOSTICS\r\n$");
    DOS_StringOutput("================================\r\n\r\n$");

    DOS_StringOutput("UNAPI TCP/IP: $");
    ok = (tcpip_enumerate() > 0) ? 1 : 0;
    if(ok)
        DOS_StringOutput("ENCONTRADO\r\n$");
    else
    {
        DOS_StringOutput("NO ENCONTRADO\r\n\r\n$");
        DOS_StringOutput("Juego arrancara OFFLINE.\r\n$");
        DOS_StringOutput("Pulsa ESPACIO para continuar...$");
        DOS_CharInput();
        return;
    }

    tcpip_get_ipinfo(&g_IpInfo);

    DOS_StringOutput("IP local    : $");
    localIP[0] = (u8)g_IpInfo.local_ip[0];
    localIP[1] = (u8)g_IpInfo.local_ip[1];
    localIP[2] = (u8)g_IpInfo.local_ip[2];
    localIP[3] = (u8)g_IpInfo.local_ip[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("Mascara     : $");
    localIP[0] = (u8)g_IpInfo.subnet_mask[0];
    localIP[1] = (u8)g_IpInfo.subnet_mask[1];
    localIP[2] = (u8)g_IpInfo.subnet_mask[2];
    localIP[3] = (u8)g_IpInfo.subnet_mask[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("Gateway     : $");
    localIP[0] = (u8)g_IpInfo.gateway_ip[0];
    localIP[1] = (u8)g_IpInfo.gateway_ip[1];
    localIP[2] = (u8)g_IpInfo.gateway_ip[2];
    localIP[3] = (u8)g_IpInfo.gateway_ip[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("\r\nServidor    : $");
    Diag_PrintIP(SERVER_IP);
    DOS_StringOutput("\r\n$");

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

    DOS_StringOutput("\r\nPulsa ESPACIO para continuar...$");
    DOS_CharInput();
    DOS_StringOutput("\r\n$");
}

//=============================================================================
// RED: CONEXION
//=============================================================================

void Net_Wait50(void)
{
    u8 w;
    for(w = 0; w < 25; w++) Halt();
}

bool Net_ConnectToServer(void)
{
    u8 tcpState;
    u16 timeout;

    Log_Init();
    Log_Write("[INIT] Damas arrancando");

    // Buscar UNAPI
    if(Net_Init() != NET_OK)
    {
        Log_Write("[CONN] UNAPI no hallado");
        return FALSE;
    }
    Log_WriteHex("[CONN] UNAPI OK impl=", g_NetImplCount);

    Net_Wait50();

    // IP local
    tcpip_get_ipinfo(&g_IpInfo);
    Log_WriteHex("[CONN] IP=", (u8)g_IpInfo.local_ip[0]);

    Net_Wait50();

    // Abrir TCP
    Log_Write("[CONN] Abriendo TCP...");
    g_Conn = Net_Open(SERVER_IP, SERVER_PORT);
    if(g_Conn == NET_INVALID_CONN)
    {
        Log_WriteHex("[CONN] Fallo err=", g_NetLastError);
        return FALSE;
    }
    Log_WriteHex("[CONN] Handle=", (u8)g_Conn);

    // Esperar ESTABLISHED
    timeout = 0;
    while(timeout < 500)
    {
        Halt();
        tcpState = Net_GetConnState(g_Conn);
        if(tcpState == TCP_STATE_ESTABLISHED) break;
        if(tcpState == 0xFF) { Log_Write("[CONN] Fallo TCP"); return FALSE; }
        timeout++;
    }
    if(timeout >= 500) { Log_Write("[CONN] Timeout"); return FALSE; }
    Log_Write("[CONN] ESTABLISHED");

    Net_Wait50();

    // Auth
    {
        u8 token[4];
        u8 len;
        token[0] = AUTH_TOKEN_0;
        token[1] = AUTH_TOKEN_1;
        token[2] = AUTH_TOKEN_2;
        token[3] = AUTH_TOKEN_3;
        g_SendBuf[0] = PROTO_MAGIC_0;
        g_SendBuf[1] = PROTO_MAGIC_1;
        g_SendBuf[2] = CMD_AUTH;
        g_SendBuf[3] = 0;
        g_SendBuf[4] = 0;
        g_SendBuf[5] = 4;
        g_SendBuf[6] = token[0];
        g_SendBuf[7] = token[1];
        g_SendBuf[8] = token[2];
        g_SendBuf[9] = token[3];
        Net_Send(g_Conn, g_SendBuf, 10);
        Log_Write("[AUTH] Enviado");
    }

    // Esperar AUTH_OK
    timeout = 0;
    while(timeout < 250)
    {
        u16 avail;
        Halt();
        avail = Net_Available(g_Conn);
        if(avail >= 6)
        {
            u8 hdr[6];
            Net_Recv(g_Conn, hdr, 6);
            if(hdr[2] == CMD_AUTH_OK) { Log_Write("[AUTH] OK"); break; }
            if(hdr[2] == CMD_AUTH_FAIL) { Log_Write("[AUTH] FAIL"); return FALSE; }
        }
        timeout++;
    }
    if(timeout >= 250) { Log_Write("[AUTH] Timeout"); return FALSE; }

    return TRUE;
}

//=============================================================================
// RED: LOBBY + SALA
//=============================================================================

#define STATE_LOBBY_WAIT 0
#define STATE_LOBBY      1
#define STATE_WAITING    2  // Esperando segundo jugador
#define STATE_PLAYING    3
static u8 g_GameState = STATE_LOBBY_WAIT;

#define LOBBY_MAX_ROOMS 10
typedef struct { u8 roomId; u8 gameId; u8 players; } LobbyRoom;
static LobbyRoom g_LobbyRooms[LOBBY_MAX_ROOMS];
static u8 g_LobbyCount = 0;
static u8 g_LobbyCursor = 0;
static u8 g_KeyUp = 0, g_KeyDown = 0, g_KeyRet = 0, g_KeyC = 0, g_KeyR = 0, g_KeyEsc = 0;

void Net_RequestRoomList(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_ROOM_LIST; g_SendBuf[3] = 0; g_SendBuf[4] = 0; g_SendBuf[5] = 0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

void Net_CreateRoom(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_ROOM_CREATE; g_SendBuf[3] = 0; g_SendBuf[4] = 0;
    g_SendBuf[5] = 3;
    g_SendBuf[6] = GAME_ID_DAMAS;
    g_SendBuf[7] = 2;  // max 2 jugadores
    g_SendBuf[8] = 0x01; // RELAY mode (turnos gestionados por cliente)
    Net_Send(g_Conn, g_SendBuf, 9);
}

void Net_JoinRoom(u8 roomId)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_ROOM_JOIN; g_SendBuf[3] = 0; g_SendBuf[4] = 0;
    g_SendBuf[5] = 1; g_SendBuf[6] = roomId;
    Net_Send(g_Conn, g_SendBuf, 7);
}

// endTurn: 0 = multi-captura pendiente (no cambiar turno), 1 = fin de turno
void Net_SendMove(u8 fromX, u8 fromY, u8 toX, u8 toY, u8 endTurn)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_STATE_UPDATE; g_SendBuf[3] = g_RoomId; g_SendBuf[4] = g_MyPid;
    g_SendBuf[5] = 8;
    g_SendBuf[6] = fromX; g_SendBuf[7] = fromY;
    g_SendBuf[8] = toX; g_SendBuf[9] = toY;
    g_SendBuf[10] = endTurn; g_SendBuf[11] = 0; g_SendBuf[12] = 0; g_SendBuf[13] = 0;
    Net_Send(g_Conn, g_SendBuf, 14);
}

void Net_SendPing(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_PING; g_SendBuf[3] = g_RoomId; g_SendBuf[4] = g_MyPid;
    g_SendBuf[5] = 0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

void Net_ProcessPacket(u8 cmd, u8* payload, u8 len)
{
    if(cmd == CMD_ROOM_LIST && len >= 1)
    {
        u8 count = payload[0];
        u8 i;
        g_LobbyCount = 0;
        for(i = 0; i < count && i < LOBBY_MAX_ROOMS; i++)
        {
            u8 off = 1 + i * 3;
            if(payload[off + 1] == GAME_ID_DAMAS)
            {
                g_LobbyRooms[g_LobbyCount].roomId = payload[off];
                g_LobbyRooms[g_LobbyCount].gameId = payload[off + 1];
                g_LobbyRooms[g_LobbyCount].players = payload[off + 2];
                g_LobbyCount++;
            }
        }
        g_LobbyCursor = 0;
        g_GameState = STATE_LOBBY;
        Lobby_Draw();
    }
    else if(cmd == CMD_ROOM_INFO && len >= 4)
    {
        g_RoomId = payload[0];
        g_MyPid = payload[3];
        Log_WriteHex("[ROOM] Room=", g_RoomId);
        Log_WriteHex("[ROOM] PID=", g_MyPid);
        Log_WriteHex("[ROOM] Players=", payload[2]);
        // P1 = blancas, P2 = negras
        g_MyColor = (g_MyPid == 1) ? PIECE_WHITE : PIECE_BLACK;

        if(payload[2] >= 2)
        {
            // Dos jugadores: empieza partida
            g_GameState = STATE_PLAYING;
            g_BoardDirty = TRUE;
        }
        else
        {
            // Solo yo: dibujar tablero y esperar al segundo
            Board_Draw();
            Buf_PutText(2, 0, "ESPERANDO RIVAL");
            Buf_PutText(2, 1, (g_MyColor == PIECE_WHITE) ? "BLANCAS" : "NEGRAS");
            Buf_PutText(20, 0, "SALA:");
            Buf_PutNum(26, 0, g_RoomId);
            g_FullFlush = TRUE;
            g_GameState = STATE_WAITING;
        }
    }
    else if(cmd == CMD_PLAYER_JOINED)
    {
        if(g_GameState == STATE_WAITING)
        {
            g_GameState = STATE_PLAYING;
            g_BoardDirty = TRUE;
        }
    }
    else if(cmd == CMD_PLAYER_LEFT)
    {
        if(g_GameState == STATE_PLAYING || g_GameState == STATE_WAITING)
        {
            g_GameResult = GAME_RESULT_DISC;
        }
    }
    else if(cmd == CMD_STATE_UPDATE && len >= 4)
    {
        // Movimiento del oponente — ejecutar sin validar
        // (el servidor ya lo validó al reenviarlo)
        u8 fromX = payload[0];
        u8 fromY = payload[1];
        u8 toX = payload[2];
        u8 toY = payload[3];
        if(fromX != toX || fromY != toY)
        {
            u8 endTurn = payload[4];
            // Ejecutar movimiento del oponente sin cambiar turno
            // (Move_Execute cambia turno internamente, pero debemos respetar endTurn)
            g_Board[toY][toX] = g_Board[fromY][fromX];
            g_Board[fromY][fromX] = PIECE_NONE;
            // Quitar pieza capturada si hay
            {
                i8 dx = (i8)toX - (i8)fromX;
                i8 dy = (i8)toY - (i8)fromY;
                i8 sx = (dx > 0) ? 1 : -1;
                i8 sy = (dy > 0) ? 1 : -1;
                i8 dist = (dx > 0) ? dx : -dx;
                i8 ii;
                for(ii = 1; ii < dist; ii++)
                {
                    u8 cx = fromX + sx * ii;
                    u8 cy = fromY + sy * ii;
                    if(g_Board[cy][cx] != PIECE_NONE)
                    {
                        g_Board[cy][cx] = PIECE_NONE;
                        break;
                    }
                }
            }
            // Promocion
            if(g_Board[toY][toX] == PIECE_WHITE && toY == 0)
                g_Board[toY][toX] = PIECE_WHITE_KING;
            if(g_Board[toY][toX] == PIECE_BLACK && toY == BOARD_SIZE - 1)
                g_Board[toY][toX] = PIECE_BLACK_KING;
            // Solo cambiar turno si endTurn == 1
            if(endTurn)
                g_Turn = (g_Turn == PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
            g_BoardDirty = TRUE;
            CheckGameOver();
        }
    }
}

void Net_Poll(void)
{
    u16 avail;
    u8 hdr[6];
    u8 payload[200];
    u8 maxPkts;

    if(g_Conn == NET_INVALID_CONN) return;

    maxPkts = 4;
    while(maxPkts--)
    {
        avail = Net_Available(g_Conn);
        if(avail < 6) break;
        Net_Recv(g_Conn, hdr, 6);
        if(hdr[0] != PROTO_MAGIC_0 || hdr[1] != PROTO_MAGIC_1) break;
        if(hdr[5] > 0)
        {
            avail = Net_Available(g_Conn);
            if(avail < hdr[5]) break;
            Net_Recv(g_Conn, payload, hdr[5]);
        }
        Net_ProcessPacket(hdr[2], payload, hdr[5]);
    }

    g_PingTimer++;
    if(g_PingTimer >= PING_INTERVAL) { g_PingTimer = 0; Net_SendPing(); }
}

//=============================================================================
// LOBBY
//=============================================================================

void Lobby_Draw(void)
{
    u8 i;
    u16 idx;

    for(idx = 0; idx < 768; idx++) g_NameBuf[idx] = TILE_SPC;
    g_FullFlush = TRUE;

    Buf_PutText(10, 1, "DAMAS ONLINE");
    Buf_PutText(6, 3, "SALAS DISPONIBLES");

    if(g_LobbyCount == 0)
    {
        Buf_PutText(6, 8, "NO HAY SALAS");
        Buf_PutText(6, 10, "PULSA C PARA CREAR");
    }
    else
    {
        Buf_PutText(8, 5, "SALA  JUGADORES");
        for(i = 0; i < g_LobbyCount; i++)
        {
            u8 row = 7 + i * 2;
            if(row > 20) break;

            if(i == g_LobbyCursor)
            {
                u16 cidx = (u16)row * 32 + 6;
                g_NameBuf[cidx] = TILE_CURSOR_T;
            }

            Buf_PutNum(8, row, g_LobbyRooms[i].roomId);
            Buf_PutNum(14, row, g_LobbyRooms[i].players);
            Buf_PutText(16, row, "/2");
        }
    }

    Buf_PutText(4, 22, "C CREAR  ENTER UNIR");
    Buf_PutText(4, 23, "R REFRESCAR  ESC SALIR");
}

void Lobby_ProcessInput(void)
{
    if(g_KeyUp) { g_KeyUp = 0; if(g_LobbyCursor > 0) { g_LobbyCursor--; Lobby_Draw(); } }
    if(g_KeyDown) { g_KeyDown = 0; if(g_LobbyCount > 0 && g_LobbyCursor < g_LobbyCount-1) { g_LobbyCursor++; Lobby_Draw(); } }
    if(g_KeyRet) {
        g_KeyRet = 0;
        if(g_LobbyCount > 0) {
            Net_JoinRoom(g_LobbyRooms[g_LobbyCursor].roomId);
            g_GameState = STATE_LOBBY_WAIT;
        }
    }
    if(g_KeyC) {
        g_KeyC = 0;
        Net_CreateRoom();
        g_GameState = STATE_LOBBY_WAIT;
    }
    if(g_KeyR) { g_KeyR = 0; g_GameState = STATE_LOBBY_WAIT; Net_RequestRoomList(); }
}

//=============================================================================
// MAIN
//=============================================================================

void main(void)
{
    u8 i;
    bool online;

    // Check if launched from LOBBY.COM
    if(LobbyClient_Load()) {
        g_Conn = (NetConn)(int)g_LobbyData.conn;
        g_MyPid = g_LobbyData.pid;
        g_RoomId = g_LobbyData.roomId;
        g_MyColor = (g_MyPid == 1) ? PIECE_WHITE : PIECE_BLACK;
        online = TRUE;
        Net_Init();
    } else {
        // Direct launch: full diag + connect
        Diag_ShowNetInfo();
        online = Net_ConnectToServer();
    }

    // Screen 4
    VDP_SetMode(VDP_MODE_SCREEN4);
    VDP_SetColor(1);

    // Sprites
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_16 | VDP_SPRITE_SCALE_1);
    VDP_LoadSpritePattern(g_PieceSprite, 0, 4);
    VDP_LoadSpritePattern(g_CursorSprite, 4, 4);
    for(i = 0; i < 32; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);

    VDP_LoadTileset();
    Board_Init();

    if(g_FromLobby)
    {
        // From LOBBY.COM: go straight to playing
        g_GameState = STATE_PLAYING;
        g_BoardDirty = TRUE;
        Board_Draw();
    }
    else if(online)
    {
        g_GameState = STATE_LOBBY_WAIT;
        Net_RequestRoomList();
    }
    else
    {
        g_GameState = STATE_PLAYING;
        Board_Draw();
    }

    // Game loop
    while(1)
    {
        Halt();

        // VRAM flush
        if(g_FullFlush)
        {
            VDP_WriteVRAM(g_NameBuf, 0x1800, 0, 768);
            g_FullFlush = FALSE;
            g_NbDirtyCount = 0;
        }
        else if(g_NbDirtyCount > 0)
        {
            u8 d;
            for(d = 0; d < g_NbDirtyCount; d++)
            {
                u16 idx = g_NbDirtyIdx[d];
                VDP_WriteVRAM(g_NameBuf + idx, 0x1800 + idx, 0, 1);
            }
            g_NbDirtyCount = 0;
        }

        Keyboard_Update();
        *((u16*)0xF3F8) = *((u16*)0xF3FA);

        // Capturar teclas con anti-rebote (lobby)
        if(g_GameState == STATE_LOBBY || g_GameState == STATE_LOBBY_WAIT || g_GameState == STATE_WAITING)
        {
            if(g_MoveDelay > 0) { g_MoveDelay--; }
            else
            {
                if(Keyboard_IsKeyPressed(KEY_UP))   { g_KeyUp = 1; g_MoveDelay = 8; }
                if(Keyboard_IsKeyPressed(KEY_DOWN)) { g_KeyDown = 1; g_MoveDelay = 8; }
                if(Keyboard_IsKeyPressed(KEY_RET))  { g_KeyRet = 1; g_MoveDelay = 15; }
                if(Keyboard_IsKeyPressed(KEY_C))    { g_KeyC = 1; g_MoveDelay = 15; }
                if(Keyboard_IsKeyPressed(KEY_R))    { g_KeyR = 1; g_MoveDelay = 15; }
                if(Keyboard_IsKeyPressed(KEY_ESC))  { g_KeyEsc = 1; }
            }
        }
        else
        {
            if(Keyboard_IsKeyPressed(KEY_ESC)) g_KeyEsc = 1;
        }

        if(g_KeyEsc) break;

        if(g_GameState == STATE_LOBBY_WAIT)
        {
            Net_Poll();
        }
        else if(g_GameState == STATE_LOBBY)
        {
            Lobby_ProcessInput();
            // Poll ligero
            if(online)
            {
                u16 avail = Net_Available(g_Conn);
                if(avail >= 6)
                {
                    u8 hdr[6];
                    u8 pl[255];
                    Net_Recv(g_Conn, hdr, 6);
                    if(hdr[0] == PROTO_MAGIC_0 && hdr[1] == PROTO_MAGIC_1 && hdr[5] > 0)
                    {
                        while(Net_Available(g_Conn) < hdr[5]) Halt();
                        Net_Recv(g_Conn, pl, hdr[5]);
                    }
                    if(hdr[0] == PROTO_MAGIC_0 && hdr[1] == PROTO_MAGIC_1)
                        Net_ProcessPacket(hdr[2], pl, hdr[5]);
                }
            }
        }
        else if(g_GameState == STATE_WAITING)
        {
            // Esperando segundo jugador — mostrar fichas
            Pieces_Draw();
            Net_Poll();
        }
        else if(g_GameState == STATE_PLAYING)
        {
            Game_ProcessInput();

            if(online)
                Net_Poll();

            if(g_BoardDirty)
                Board_Draw();

            Pieces_Draw();
            Cursor_Draw();

            // Comprobar fin de partida
            if(g_GameResult != GAME_RESULT_NONE)
            {
                u16 idx;
                u8 w;

                // Mostrar resultado sobre el tablero
                for(idx = 0; idx < 768; idx++) g_NameBuf[idx] = TILE_SPC;
                if(g_GameResult == GAME_RESULT_WIN)
                    Buf_PutText(8, 10, "HAS GANADO");
                else if(g_GameResult == GAME_RESULT_LOSE)
                    Buf_PutText(8, 10, "HAS PERDIDO");
                else
                    Buf_PutText(5, 10, "RIVAL DESCONECTADO");

                Buf_PutText(5, 14, "PULSA ESPACIO");
                g_FullFlush = TRUE;

                // Esperar tecla
                for(w = 0; w < 32; w++) VDP_SetSpriteExUniColor(w, 0, 209, 0, 0);
                while(!Keyboard_IsKeyPressed(KEY_SPACE) && !Keyboard_IsKeyPressed(KEY_RET))
                {
                    Halt();
                    Keyboard_Update();
                    *((u16*)0xF3F8) = *((u16*)0xF3FA);
                    if(g_FullFlush) { VDP_WriteVRAM(g_NameBuf, 0x1800, 0, 768); g_FullFlush = FALSE; }
                }

                // Salir de la sala
                if(g_Conn != NET_INVALID_CONN)
                {
                    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
                    g_SendBuf[2] = CMD_ROOM_LEAVE; g_SendBuf[3] = g_RoomId;
                    g_SendBuf[4] = g_MyPid; g_SendBuf[5] = 0;
                    Net_Send(g_Conn, g_SendBuf, 6);
                }
                g_RoomId = 0;
                g_MyPid = 0;
                g_MyColor = 0;

                // Reiniciar para volver al lobby
                g_GameResult = GAME_RESULT_NONE;
                g_MustCapture = 0;
                g_Selected = 0;
                g_Turn = PIECE_WHITE;
                Board_Init();
                g_GameState = STATE_LOBBY_WAIT;
                Net_RequestRoomList();
            }
        }
    }

    if(g_Conn != NET_INVALID_CONN)
    {
        Net_Close(g_Conn);
        g_Conn = NET_INVALID_CONN;
    }
    Log_Write("[EXIT] Fin");
    Log_Close();

    for(i = 0; i < 32; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);
    Bios_Exit(0);
}
