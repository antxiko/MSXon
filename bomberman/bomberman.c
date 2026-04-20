// Bomberman — MSX2 Screen 4 — MSXon
// 4 players grid-based, place bombs, last one alive wins
// GAME_ID=0x08, RELAY + handler
#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"
#include "lobby.h"
#include "lobby_client.h"

// ── Grid ────────────────────────────────────────────────────────────
// 15x10 casillas, cada casilla = 2x2 tiles (16x16 px)
// HUD arriba: 4 tiles (32px). Tablero: 20 tiles de alto (10 casillas) x 30 tiles (15 casillas)
#define GW 15
#define GH 10
#define HUD_H 4          // tiles de alto del HUD
#define BOARD_TX 1       // X inicial del tablero en tiles
#define BOARD_TY 4       // Y inicial del tablero en tiles

#define MAX_PLAYERS 4
#define MAX_BOMBS   16

// Cell types
#define CELL_FLOOR  0
#define CELL_WALL   1    // muro fijo (indestructible)
#define CELL_BRICK  2    // ladrillo destructible
#define CELL_BOMB   3
#define CELL_POWER  4    // power-up visible

// Power-ups (type byte)
#define PW_NONE    0
#define PW_BOMB    1     // +1 bomba
#define PW_FIRE    2     // +1 rango explosion
#define PW_SPEED   3     // +velocidad

// Tiles (tile index en VRAM)
#define T_BLK    0
#define T_FL1    1    // floor normal
#define T_FL2    2    // floor alterno
#define T_WL1    3    // wall top-left
#define T_WL2    4    // wall top-right
#define T_WL3    5    // wall bot-left
#define T_WL4    6    // wall bot-right
#define T_BR1    7    // brick top-left
#define T_BR2    8    // brick top-right
#define T_BR3    9    // brick bot-left
#define T_BR4    10   // brick bot-right
#define T_EXP1   11   // explosion center
#define T_EXP2   12   // explosion arm horiz
#define T_EXP3   13   // explosion arm vert
#define T_PWB    14   // powerup bomb
#define T_PWF    15   // powerup fire
#define T_PWS    16   // powerup speed
#define T_FA     20   // font A-Z (20..45)
#define T_F0     46   // font 0-9 (46..55)

// ── Patterns (8x8) ──────────────────────────────────────────────────
static const u8 P_BLK[8] = {0,0,0,0,0,0,0,0};
static const u8 P_FL1[8] = {0x00,0x22,0x00,0x88,0x00,0x22,0x00,0x88};
static const u8 P_FL2[8] = {0x88,0x00,0x22,0x00,0x88,0x00,0x22,0x00};
// Wall (2x2 tile = bloque 16x16): cuatro cuadrantes formando un bloque
static const u8 P_WL1[8] = {0xFF,0xFF,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC};
static const u8 P_WL2[8] = {0xFF,0xFF,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F};
static const u8 P_WL3[8] = {0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0xFF,0xFF};
static const u8 P_WL4[8] = {0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0xFF,0xFF};
// Brick (2x2 tile): ladrillo con "junta" en medio
static const u8 P_BR1[8] = {0xFF,0x80,0x80,0x80,0x80,0xFF,0xAA,0xAA};
static const u8 P_BR2[8] = {0xFF,0x01,0x01,0x01,0x01,0xFF,0xAA,0xAA};
static const u8 P_BR3[8] = {0xAA,0xAA,0xFF,0x80,0x80,0x80,0x80,0xFF};
static const u8 P_BR4[8] = {0xAA,0xAA,0xFF,0x01,0x01,0x01,0x01,0xFF};
// Explosion center
static const u8 P_EXP1[8] = {0x18,0x3C,0x7E,0xFF,0xFF,0x7E,0x3C,0x18};
static const u8 P_EXP2[8] = {0x00,0x18,0x3C,0x7E,0x7E,0x3C,0x18,0x00};
static const u8 P_EXP3[8] = {0x18,0x18,0x3C,0x3C,0x7E,0x7E,0x3C,0x18};
// Power-ups
static const u8 P_PWB[8] = {0x3C,0x42,0x5A,0x7E,0x7E,0x5A,0x42,0x3C};
static const u8 P_PWF[8] = {0x18,0x3C,0x5A,0x7E,0x7E,0x3C,0x18,0x24};
static const u8 P_PWS[8] = {0x00,0x30,0x70,0xFF,0xFF,0x70,0x30,0x00};

// Font 26 letras + 10 números (copiado de otros juegos)
static const u8 FONT[][8]={
 {0,0x38,0x44,0x44,0x7C,0x44,0x44,0},{0,0x78,0x44,0x78,0x44,0x44,0x78,0},
 {0,0x3C,0x40,0x40,0x40,0x40,0x3C,0},{0,0x78,0x44,0x44,0x44,0x44,0x78,0},
 {0,0x7C,0x40,0x78,0x40,0x40,0x7C,0},{0,0x7C,0x40,0x78,0x40,0x40,0x40,0},
 {0,0x3C,0x40,0x4C,0x44,0x44,0x3C,0},{0,0x44,0x44,0x7C,0x44,0x44,0x44,0},
 {0,0x38,0x10,0x10,0x10,0x10,0x38,0},{0,0x1C,0x04,0x04,0x04,0x44,0x38,0},
 {0,0x44,0x48,0x70,0x48,0x44,0x44,0},{0,0x40,0x40,0x40,0x40,0x40,0x7C,0},
 {0,0x44,0x6C,0x54,0x44,0x44,0x44,0},{0,0x44,0x64,0x54,0x4C,0x44,0x44,0},
 {0,0x38,0x44,0x44,0x44,0x44,0x38,0},{0,0x78,0x44,0x44,0x78,0x40,0x40,0},
 {0,0x38,0x44,0x44,0x54,0x48,0x34,0},{0,0x78,0x44,0x44,0x78,0x48,0x44,0},
 {0,0x3C,0x40,0x38,0x04,0x04,0x78,0},{0,0x7C,0x10,0x10,0x10,0x10,0x10,0},
 {0,0x44,0x44,0x44,0x44,0x44,0x38,0},{0,0x44,0x44,0x44,0x44,0x28,0x10,0},
 {0,0x44,0x44,0x44,0x54,0x6C,0x44,0},{0,0x44,0x28,0x10,0x28,0x44,0x44,0},
 {0,0x44,0x44,0x28,0x10,0x10,0x10,0},{0,0x7C,0x04,0x08,0x10,0x20,0x7C,0},
 {0,0x38,0x4C,0x54,0x64,0x44,0x38,0},{0,0x10,0x30,0x10,0x10,0x10,0x38,0},
 {0,0x38,0x44,0x08,0x10,0x20,0x7C,0},{0,0x38,0x44,0x18,0x04,0x44,0x38,0},
 {0,0x08,0x18,0x28,0x48,0x7C,0x08,0},{0,0x7C,0x40,0x78,0x04,0x44,0x38,0},
 {0,0x38,0x40,0x78,0x44,0x44,0x38,0},{0,0x7C,0x04,0x08,0x10,0x10,0x10,0},
 {0,0x38,0x44,0x38,0x44,0x44,0x38,0},{0,0x38,0x44,0x44,0x3C,0x04,0x38,0},
};

// Colors (fg<<4|bg)
static const u8 CL_BLK[8]  = {0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11};
static const u8 CL_FL[8]   = {0x52,0x52,0x52,0x52,0x52,0x52,0x52,0x52}; // ltblue on dkblue
static const u8 CL_WL[8]   = {0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1}; // gray on black
static const u8 CL_BR[8]   = {0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x81}; // red on black
static const u8 CL_EXP[8]  = {0xAF,0xAF,0xAF,0xAF,0xAF,0xAF,0xAF,0xAF}; // orange on yellow
static const u8 CL_PWB[8]  = {0xB1,0xB1,0xB1,0xB1,0xB1,0xB1,0xB1,0xB1}; // ltgreen on black
static const u8 CL_PWF[8]  = {0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x81}; // red on black
static const u8 CL_PWS[8]  = {0xF1,0xF1,0xF1,0xF1,0xF1,0xF1,0xF1,0xF1}; // white on black
static const u8 CL_FNT[8]  = {0xF1,0xF1,0xF1,0xF1,0xF1,0xF1,0xF1,0xF1};

// ── Sprites (16x16) ─────────────────────────────────────────────────
// Jugador básico: cabeza redonda + cuerpo
static const u8 SPR_PLAYER[32] = {
    // Left half (top 8 rows, bot 8 rows)
    0x07,0x0F,0x1F,0x1F,0x0F,0x07,0x03,0x01,
    0x03,0x07,0x07,0x07,0x03,0x01,0x03,0x07,
    // Right half
    0xE0,0xF0,0xF8,0xF8,0xF0,0xE0,0xC0,0x80,
    0xC0,0xE0,0xE0,0xE0,0xC0,0x80,0xC0,0xE0
};

// Bomba: círculo con mecha
static const u8 SPR_BOMB[32] = {
    0x00,0x00,0x01,0x02,0x04,0x08,0x0F,0x1F,
    0x1F,0x1F,0x1F,0x0F,0x07,0x01,0x00,0x00,
    0x00,0x10,0xB0,0x40,0x80,0x00,0xF0,0xF8,
    0xF8,0xF8,0xF8,0xF0,0xE0,0x80,0x00,0x00
};

// ── Player struct ───────────────────────────────────────────────────
typedef struct {
    i16 x, y;        // pixel position (subpixel)
    u8 gx, gy;       // grid cell (0-based)
    u8 dir;          // 0=right, 1=left, 2=up, 3=down
    u8 alive;
    u8 bombs_max;    // max bombs simultáneas
    u8 bombs_active; // bombas puestas actualmente
    u8 fire;         // rango explosión
    u8 speed;        // pixels/frame
    u8 wins;
} Player;

typedef struct {
    u8 gx, gy;
    u8 owner;        // slot del dueño
    u8 power;        // rango al explotar
    u8 timer;        // frames hasta explotar
    u8 active;
} Bomb;

// ── Globals ─────────────────────────────────────────────────────────
static Player g_P[MAX_PLAYERS];
static Bomb g_B[MAX_BOMBS];
static u8 g_Grid[GH][GW];      // estado actual del tablero
static u8 g_ExpMap[GH][GW];    // timer de tiles en explosion (frames)

u8 g_NB[768];
static u8 g_MySlot = 0;
static bool g_Online = FALSE;
static u8 g_Winner = 0xFF;

// Lobby config
static const u8 SRV_IP[4] = {217,154,107,144};
static const LobbyConfig g_LobbyCfg = {
    "BOMBERMAN", 0x08, 4, SRV_IP, 9876
};

// Network packet types
#define PKT_PLAYER    1   // 5 bytes: slot x y dir
#define PKT_BOMB      2   // 3 bytes: gx gy
#define PKT_LAYOUT    3   // server: grid completo al arrancar
#define PKT_EXPLOSION 4   // server: gx gy power (origen)
#define PKT_BRICK     5   // server: brick destruido gx gy + powerup type
#define PKT_POWERUP   6   // cliente: cojo powerup en mi casilla
#define PKT_DEATH     7   // server: slot muerto
#define PKT_WINNER    8   // server: ganador
#define PKT_BOMB_ACK  9   // server: bomba confirmada gx gy owner power

// Random
static u16 g_Seed;
static u8 Rnd(void) { g_Seed = g_Seed*25173+13849; return (u8)(g_Seed>>8); }

// ── Name buffer helpers ─────────────────────────────────────────────
#define NB_SET(x,y,v) do{u16 _i=(u16)(y)*32+(x);u8 _v=(v);if(g_NB[_i]!=_v)g_NB[_i]=_v;}while(0)

static void NB_Text(u8 x, u8 y, const c8* s) {
    while(*s && x<32) {
        u8 ch=(u8)*s, t;
        if(ch>='A'&&ch<='Z') t=T_FA+ch-'A';
        else if(ch>='0'&&ch<='9') t=T_F0+ch-'0';
        else t=T_BLK;
        NB_SET(x,y,t); x++; s++;
    }
}

static void NB_Num(u8 x, u8 y, u16 v) {
    c8 b[6]; u8 n=0,i; u16 d=10000;
    for(i=0;i<5;i++){u8 g=(u8)(v/d);v%=d;d/=10;if(g||n||i==4)b[n++]='0'+g;}
    b[n]=0; NB_Text(x,y,b);
}

// ── Load tileset ────────────────────────────────────────────────────
static void LoadTiles(void) {
    u8 bank, t;
    for(bank=0; bank<3; bank++) {
        u16 pb = bank*2048;
        u16 cb = 0x2000 + pb;
        VDP_WriteVRAM(P_BLK, pb+0*8, 0, 8);  VDP_WriteVRAM(CL_BLK, cb+0*8, 0, 8);
        VDP_WriteVRAM(P_FL1, pb+1*8, 0, 8);  VDP_WriteVRAM(CL_FL, cb+1*8, 0, 8);
        VDP_WriteVRAM(P_FL2, pb+2*8, 0, 8);  VDP_WriteVRAM(CL_FL, cb+2*8, 0, 8);
        VDP_WriteVRAM(P_WL1, pb+3*8, 0, 8);  VDP_WriteVRAM(CL_WL, cb+3*8, 0, 8);
        VDP_WriteVRAM(P_WL2, pb+4*8, 0, 8);  VDP_WriteVRAM(CL_WL, cb+4*8, 0, 8);
        VDP_WriteVRAM(P_WL3, pb+5*8, 0, 8);  VDP_WriteVRAM(CL_WL, cb+5*8, 0, 8);
        VDP_WriteVRAM(P_WL4, pb+6*8, 0, 8);  VDP_WriteVRAM(CL_WL, cb+6*8, 0, 8);
        VDP_WriteVRAM(P_BR1, pb+7*8, 0, 8);  VDP_WriteVRAM(CL_BR, cb+7*8, 0, 8);
        VDP_WriteVRAM(P_BR2, pb+8*8, 0, 8);  VDP_WriteVRAM(CL_BR, cb+8*8, 0, 8);
        VDP_WriteVRAM(P_BR3, pb+9*8, 0, 8);  VDP_WriteVRAM(CL_BR, cb+9*8, 0, 8);
        VDP_WriteVRAM(P_BR4, pb+10*8, 0, 8); VDP_WriteVRAM(CL_BR, cb+10*8, 0, 8);
        VDP_WriteVRAM(P_EXP1, pb+11*8, 0, 8); VDP_WriteVRAM(CL_EXP, cb+11*8, 0, 8);
        VDP_WriteVRAM(P_EXP2, pb+12*8, 0, 8); VDP_WriteVRAM(CL_EXP, cb+12*8, 0, 8);
        VDP_WriteVRAM(P_EXP3, pb+13*8, 0, 8); VDP_WriteVRAM(CL_EXP, cb+13*8, 0, 8);
        VDP_WriteVRAM(P_PWB, pb+14*8, 0, 8);  VDP_WriteVRAM(CL_PWB, cb+14*8, 0, 8);
        VDP_WriteVRAM(P_PWF, pb+15*8, 0, 8);  VDP_WriteVRAM(CL_PWF, cb+15*8, 0, 8);
        VDP_WriteVRAM(P_PWS, pb+16*8, 0, 8);  VDP_WriteVRAM(CL_PWS, cb+16*8, 0, 8);
        for(t=0; t<26; t++) {
            VDP_WriteVRAM(FONT[t], pb+(T_FA+t)*8, 0, 8);
            VDP_WriteVRAM(CL_FNT, cb+(T_FA+t)*8, 0, 8);
        }
        for(t=0; t<10; t++) {
            VDP_WriteVRAM(FONT[26+t], pb+(T_F0+t)*8, 0, 8);
            VDP_WriteVRAM(CL_FNT, cb+(T_F0+t)*8, 0, 8);
        }
    }
}

// ── Grid helpers ────────────────────────────────────────────────────

// Draw a cell (2x2 tiles)
static void DrawCell(u8 gx, u8 gy) {
    u8 tx = BOARD_TX + gx*2;
    u8 ty = BOARD_TY + gy*2;
    u8 type = g_Grid[gy][gx];
    u8 e = g_ExpMap[gy][gx];

    if(e) {
        // Dibujar explosion
        NB_SET(tx, ty, T_EXP1);     NB_SET(tx+1, ty, T_EXP1);
        NB_SET(tx, ty+1, T_EXP1);   NB_SET(tx+1, ty+1, T_EXP1);
        return;
    }

    if(type == CELL_WALL) {
        NB_SET(tx, ty, T_WL1);     NB_SET(tx+1, ty, T_WL2);
        NB_SET(tx, ty+1, T_WL3);   NB_SET(tx+1, ty+1, T_WL4);
    } else if(type == CELL_BRICK) {
        NB_SET(tx, ty, T_BR1);     NB_SET(tx+1, ty, T_BR2);
        NB_SET(tx, ty+1, T_BR3);   NB_SET(tx+1, ty+1, T_BR4);
    } else if(type >= CELL_POWER) {
        // Power-up (CELL_POWER + tipo-1)
        u8 pt = type - CELL_POWER + 1;
        u8 pT = (pt==PW_BOMB)?T_PWB:(pt==PW_FIRE)?T_PWF:T_PWS;
        NB_SET(tx, ty, pT);        NB_SET(tx+1, ty, T_FL1);
        NB_SET(tx, ty+1, T_FL1);   NB_SET(tx+1, ty+1, T_FL1);
    } else {
        // Floor
        u8 alt = (gx+gy) & 1;
        NB_SET(tx, ty, alt?T_FL1:T_FL2);   NB_SET(tx+1, ty, alt?T_FL2:T_FL1);
        NB_SET(tx, ty+1, alt?T_FL2:T_FL1); NB_SET(tx+1, ty+1, alt?T_FL1:T_FL2);
    }
}

static void DrawBoard(void) {
    u8 gx, gy;
    for(gy=0; gy<GH; gy++)
        for(gx=0; gx<GW; gx++)
            DrawCell(gx, gy);
}

// ── HUD ─────────────────────────────────────────────────────────────
static void DrawHUD(void) {
    u8 i;
    for(i=0; i<MAX_PLAYERS; i++) {
        u8 col = i*8;
        NB_Text(col, 0, "P");
        NB_Num(col+1, 0, i+1);
        NB_Text(col+3, 0, "B");
        NB_Num(col+4, 0, g_P[i].bombs_max);
        NB_Text(col+6, 0, "F");
        NB_Num(col+7, 0, g_P[i].fire);
        NB_Num(col, 1, g_P[i].wins);
        if(!g_P[i].alive) NB_Text(col+2, 1, "OUT");
    }
}

// ── Layout generation ───────────────────────────────────────────────
static void GenerateLayout(u16 seed) {
    u8 gx, gy;
    g_Seed = seed ? seed : 1;

    // Base: walls en impares, floor en el resto
    for(gy=0; gy<GH; gy++) {
        for(gx=0; gx<GW; gx++) {
            if((gx&1) && (gy&1)) g_Grid[gy][gx] = CELL_WALL;
            else g_Grid[gy][gx] = CELL_FLOOR;
            g_ExpMap[gy][gx] = 0;
        }
    }

    // Bricks aleatorios (~40% de las casillas libres)
    for(gy=0; gy<GH; gy++) {
        for(gx=0; gx<GW; gx++) {
            if(g_Grid[gy][gx] != CELL_FLOOR) continue;
            // Dejar esquinas libres para que los jugadores arranquen
            if((gx<2 && gy<2) || (gx>GW-3 && gy<2) ||
               (gx<2 && gy>GH-3) || (gx>GW-3 && gy>GH-3)) continue;
            if((Rnd() % 100) < 40) g_Grid[gy][gx] = CELL_BRICK;
        }
    }
}

static void InitPlayers(void) {
    u8 i;
    static const u8 startX[4] = {0, GW-1, 0, GW-1};
    static const u8 startY[4] = {0, 0, GH-1, GH-1};
    for(i=0; i<MAX_PLAYERS; i++) {
        g_P[i].gx = startX[i];
        g_P[i].gy = startY[i];
        g_P[i].x = (BOARD_TX + startX[i]*2) * 8;
        g_P[i].y = (BOARD_TY + startY[i]*2) * 8;
        g_P[i].dir = 0;
        g_P[i].alive = 1;
        g_P[i].bombs_max = 1;
        g_P[i].bombs_active = 0;
        g_P[i].fire = 1;
        g_P[i].speed = 1;
    }
    // Init bombs pool
    for(i=0; i<MAX_BOMBS; i++) g_B[i].active = 0;
}

// ── Bomb logic ──────────────────────────────────────────────────────
static void PlaceBomb(u8 slot) {
    u8 i;
    Player* p = &g_P[slot];
    if(!p->alive) return;
    if(p->bombs_active >= p->bombs_max) return;
    // Check no bomb here already
    if(g_Grid[p->gy][p->gx] == CELL_BOMB) return;
    for(i=0; i<MAX_BOMBS; i++) {
        if(g_B[i].active) continue;
        g_B[i].active = 1;
        g_B[i].gx = p->gx;
        g_B[i].gy = p->gy;
        g_B[i].owner = slot;
        g_B[i].power = p->fire;
        g_B[i].timer = 120; // ~2.4s a 50Hz
        g_Grid[p->gy][p->gx] = CELL_BOMB;
        p->bombs_active++;
        DrawCell(p->gx, p->gy);
        return;
    }
}

static void Explode(u8 bombIdx) {
    Bomb* b = &g_B[bombIdx];
    i8 dx, dy, d;
    if(!b->active) return;
    b->active = 0;
    g_P[b->owner].bombs_active--;
    g_Grid[b->gy][b->gx] = CELL_FLOOR;
    g_ExpMap[b->gy][b->gx] = 30; // 30 frames de explosion

    // 4 direcciones
    static const i8 DX[4] = {1,-1,0,0};
    static const i8 DY[4] = {0,0,1,-1};
    for(d=0; d<4; d++) {
        dx = DX[d]; dy = DY[d];
        for(u8 r=1; r<=b->power; r++) {
            i8 nx = (i8)b->gx + dx*r;
            i8 ny = (i8)b->gy + dy*r;
            if(nx<0||nx>=GW||ny<0||ny>=GH) break;
            u8 cell = g_Grid[ny][nx];
            if(cell == CELL_WALL) break;
            g_ExpMap[ny][nx] = 30;
            if(cell == CELL_BRICK) {
                // Destruir ladrillo, 30% chance de power-up
                if((Rnd()%100) < 30) {
                    u8 pwType = 1 + (Rnd() % 3);
                    g_Grid[ny][nx] = CELL_POWER + pwType - 1;
                } else {
                    g_Grid[ny][nx] = CELL_FLOOR;
                }
                break;
            }
            if(cell == CELL_BOMB) {
                // Reacción en cadena
                u8 bi;
                for(bi=0; bi<MAX_BOMBS; bi++)
                    if(g_B[bi].active && g_B[bi].gx==nx && g_B[bi].gy==ny)
                        g_B[bi].timer = 1;
                break;
            }
        }
    }
}

static void UpdateBombs(void) {
    u8 i;
    for(i=0; i<MAX_BOMBS; i++) {
        if(!g_B[i].active) continue;
        if(--g_B[i].timer == 0) Explode(i);
    }
}

static void UpdateExplosions(void) {
    u8 gx, gy;
    for(gy=0; gy<GH; gy++) {
        for(gx=0; gx<GW; gx++) {
            if(g_ExpMap[gy][gx]) {
                if(--g_ExpMap[gy][gx] == 0) {
                    DrawCell(gx, gy);
                } else if(g_ExpMap[gy][gx] == 29) {
                    DrawCell(gx, gy); // redibujar como explosion
                }
            }
        }
    }
}

// ── Collision check ─────────────────────────────────────────────────
static void CheckPlayerDeaths(void) {
    u8 i;
    for(i=0; i<MAX_PLAYERS; i++) {
        if(!g_P[i].alive) continue;
        if(g_ExpMap[g_P[i].gy][g_P[i].gx]) {
            g_P[i].alive = 0;
        }
    }
    // Check winner
    {
        u8 alive=0, lastAlive=0xFF;
        for(i=0; i<MAX_PLAYERS; i++) {
            if(g_P[i].alive) { alive++; lastAlive=i; }
        }
        if(alive<=1 && g_Winner==0xFF) {
            g_Winner = lastAlive;
            g_P[lastAlive].wins++;
        }
    }
}

// ── Player movement ─────────────────────────────────────────────────
static void MovePlayer(u8 slot, u8 input) {
    Player* p = &g_P[slot];
    i16 nx, ny;
    u8 ngx, ngy;
    if(!p->alive) return;

    nx = p->x; ny = p->y;
    if(input & 1) { nx += p->speed; p->dir = 0; }       // right
    else if(input & 2) { nx -= p->speed; p->dir = 1; }  // left
    else if(input & 4) { ny -= p->speed; p->dir = 2; }  // up
    else if(input & 8) { ny += p->speed; p->dir = 3; }  // down
    else return;

    // Calcular nueva casilla
    ngx = (u8)((nx - BOARD_TX*8 + 4) / 16);
    ngy = (u8)((ny - BOARD_TY*8 + 4) / 16);
    if(ngx >= GW || ngy >= GH) return;
    // Bloquear movimiento si celda ocupada (menos power-up)
    {
        u8 c = g_Grid[ngy][ngx];
        if(c == CELL_WALL || c == CELL_BRICK) return;
        // Bomba propia: permitir salir pero no entrar una vez fuera (simplificado)
        if(c == CELL_BOMB && !(p->gx == ngx && p->gy == ngy)) return;
        // Power-up: cogerlo
        if(c >= CELL_POWER) {
            u8 pt = c - CELL_POWER + 1;
            if(pt == PW_BOMB) p->bombs_max++;
            else if(pt == PW_FIRE) p->fire++;
            else if(pt == PW_SPEED && p->speed < 3) p->speed++;
            g_Grid[ngy][ngx] = CELL_FLOOR;
            DrawCell(ngx, ngy);
        }
    }

    p->x = nx; p->y = ny;
    p->gx = ngx; p->gy = ngy;
}

// ── Draw sprites ────────────────────────────────────────────────────
static const u8 PLAYER_COL[4] = {7, 8, 11, 13}; // cyan, red, yellow, magenta

static void DrawSprites(void) {
    u8 sprIdx = 0, i;
    for(i=0; i<MAX_PLAYERS; i++) {
        if(!g_P[i].alive) continue;
        VDP_SetSpriteExUniColor(sprIdx, (u8)g_P[i].x, (u8)g_P[i].y, 0, PLAYER_COL[i]);
        sprIdx++;
    }
    // Bombas como sprites
    for(i=0; i<MAX_BOMBS; i++) {
        if(!g_B[i].active || sprIdx>=31) continue;
        u8 bx = (BOARD_TX + g_B[i].gx*2) * 8;
        u8 by = (BOARD_TY + g_B[i].gy*2) * 8;
        VDP_SetSpriteExUniColor(sprIdx, bx, by, 4, 1);
        sprIdx++;
    }
    for(; sprIdx<32; sprIdx++)
        VDP_SetSpriteExUniColor(sprIdx, 0, 209, 0, 0);
}

// ── Network ─────────────────────────────────────────────────────────
static void Net_SendPlayer(void) {
    u8 pl[5];
    Player* me = &g_P[g_MySlot];
    pl[0] = PKT_PLAYER;
    pl[1] = (u8)me->x;
    pl[2] = (u8)me->y;
    pl[3] = me->gx;
    pl[4] = me->dir;
    Lobby_SendStateUpdate(pl, 5);
}

static void Net_SendBomb(void) {
    u8 pl[3];
    pl[0] = PKT_BOMB;
    pl[1] = g_P[g_MySlot].gx;
    pl[2] = g_P[g_MySlot].gy;
    Lobby_SendStateUpdate(pl, 3);
}

static void Game_OnPacket(u8 cmd, u8 senderPid, u8* pl, u8 len) {
    u8 slot;
    if(cmd != CMD_STATE_UPDATE || len < 1) return;
    slot = senderPid - 1;

    if(pl[0] == PKT_PLAYER && len >= 5 && slot < MAX_PLAYERS && slot != g_MySlot) {
        Player* p = &g_P[slot];
        p->x = pl[1];
        p->y = pl[2];
        p->gx = pl[3];
        p->dir = pl[4];
    }
    else if(pl[0] == PKT_BOMB_ACK && len >= 5) {
        u8 gx=pl[1], gy=pl[2], owner=pl[3], power=pl[4];
        u8 i;
        for(i=0; i<MAX_BOMBS; i++) {
            if(g_B[i].active) continue;
            g_B[i].active=1; g_B[i].gx=gx; g_B[i].gy=gy;
            g_B[i].owner=owner; g_B[i].power=power; g_B[i].timer=120;
            g_Grid[gy][gx] = CELL_BOMB;
            DrawCell(gx, gy);
            break;
        }
    }
    else if(pl[0] == PKT_DEATH && len >= 2) {
        if(pl[1] < MAX_PLAYERS) g_P[pl[1]].alive = 0;
    }
    else if(pl[0] == PKT_WINNER && len >= 2) {
        g_Winner = pl[1];
    }
}

// ── Main ────────────────────────────────────────────────────────────
void main(void)
{
    u8 i, input;

    *((u8*)0xF3DB) = 0;

    g_Online = FALSE;
    g_MySlot = 0;

    if(LobbyClient_Load()) {
        Lobby_Init(&g_LobbyCfg, Game_OnPacket);
        g_LobbyConn = (NetConn)(int)g_LobbyData.conn;
        g_LobbyPid = g_LobbyData.pid;
        g_LobbyRoomId = g_LobbyData.roomId;
        g_LobbyActive = g_LobbyData.active;
        g_LobbyOnline = TRUE;
        g_LobbyState = LOBBY_ST_PLAYING;
        g_Online = TRUE;
        g_MySlot = g_LobbyPid - 1;
        Net_Init();
    } else {
        Lobby_Init(&g_LobbyCfg, Game_OnPacket);
        Lobby_SetTileOffsets(T_FA, T_F0);
        Lobby_Diag();
        if(Lobby_Connect()) g_Online = TRUE;
    }

    VDP_SetMode(VDP_MODE_SCREEN4);
    VDP_SetColor(0x11);
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_16 | VDP_SPRITE_SCALE_1);
    VDP_LoadSpritePattern(SPR_PLAYER, 0, 4);
    VDP_LoadSpritePattern(SPR_BOMB, 4, 4);
    for(i=0; i<32; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);

    LoadTiles();
    g_Seed = *((u16*)0xFC9E);
    GenerateLayout(g_Seed);
    InitPlayers();
    g_Winner = 0xFF;

    // Lobby phase
    if(g_Online && !g_FromLobby) {
        Lobby_RequestRooms();
        g_LobbyState = LOBBY_ST_LIST_WAIT;
        while(g_LobbyState < LOBBY_ST_PLAYING) {
            Halt();
            VDP_WriteVRAM(g_NB, 0x1800, 0, 768);
            Keyboard_Update();
            *((u16*)0xF3F8) = *((u16*)0xF3FA);
            if(Keyboard_IsKeyPressed(KEY_ESC)) { Bios_Exit(0); return; }
            Lobby_Update();
        }
        g_MySlot = g_LobbyPid - 1;
    }

    DrawBoard();
    DrawHUD();
    Halt();
    VDP_WriteVRAM(g_NB, 0x1800, 0, 768);

    // Game loop
    while(1) {
        Halt();
        VDP_WriteVRAM(g_NB, 0x1800, 0, 768);
        Keyboard_Update();
        *((u16*)0xF3F8) = *((u16*)0xF3FA);

        if(g_Online) Lobby_Poll();
        if(Keyboard_IsKeyPressed(KEY_ESC)) break;

        // Input
        input = 0;
        if(Keyboard_IsKeyPressed(KEY_RIGHT)) input |= 1;
        if(Keyboard_IsKeyPressed(KEY_LEFT))  input |= 2;
        if(Keyboard_IsKeyPressed(KEY_UP))    input |= 4;
        if(Keyboard_IsKeyPressed(KEY_DOWN))  input |= 8;
        if(input) MovePlayer(g_MySlot, input);

        if(Keyboard_IsKeyPressed(KEY_SPACE)) {
            if(!g_Online) PlaceBomb(g_MySlot);
            else Net_SendBomb();
        }

        // AI offline (resto de slots)
        if(!g_Online) {
            u8 ai;
            for(ai=0; ai<MAX_PLAYERS; ai++) {
                if(ai == g_MySlot) continue;
                if(!g_P[ai].alive) continue;
                // AI muy tonta: movimiento random
                if((Rnd()&3) == 0) {
                    u8 d = Rnd() & 3;
                    MovePlayer(ai, 1<<d);
                }
                if((Rnd()&0x3F) == 0) PlaceBomb(ai);
            }
        }

        // Updates
        if(!g_Online) {
            UpdateBombs();
            CheckPlayerDeaths();
        }
        UpdateExplosions();

        // Send my state
        if(g_Online && (*((u8*)0xFC9E) & 3) == 0) Net_SendPlayer();

        DrawHUD();
        DrawSprites();

        // Win screen
        if(g_Winner != 0xFF) {
            u16 ci;
            for(ci=0; ci<768; ci++) g_NB[ci] = T_BLK;
            NB_Text(10, 10, "GANA P");
            NB_Num(16, 10, g_Winner+1);
            NB_Text(6, 12, "ESPACIO PARA SALIR");
            for(i=0; i<32; i++) VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);
            Halt();
            VDP_WriteVRAM(g_NB, 0x1800, 0, 768);
            while(1) {
                Halt();
                Keyboard_Update();
                if(Keyboard_IsKeyPressed(KEY_SPACE)) break;
                if(Keyboard_IsKeyPressed(KEY_ESC)) break;
                if(g_Online) Lobby_Poll();
            }
            break;
        }
    }

    if(g_Online) Lobby_SendRoomLeave();
    for(i=0; i<32; i++) VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);
    Bios_Exit(0);
}
