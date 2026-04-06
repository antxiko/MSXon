// Among MSX — Impostor Online · MSX2 Screen 4
// 4-8 jugadores, 1 impostor, habitaciones separadas
// GAME_ID=0x07, RELAY mode
#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"

// ── Layout ──────────────────────────────────────────────────────────
#define MAX_PLAYERS 8
#define NUM_ROOMS   7

// ── Tiles ───────────────────────────────────────────────────────────
#define T_FLOOR     0
#define T_WALL_H    1   // horizontal wall
#define T_WALL_V    2   // vertical wall
#define T_CORNER_TL 3
#define T_CORNER_TR 4
#define T_CORNER_BL 5
#define T_CORNER_BR 6
#define T_DOOR_H    7   // door (horizontal passage)
#define T_DOOR_V    8   // door (vertical passage)
#define T_SYS_OK    9   // system OK (green)
#define T_SYS_BAD   10  // system sabotaged (red)
#define T_CORPSE    11  // dead body
#define T_TABLE     12  // furniture
#define T_CHAIR     13
#define T_CONSOLE   14  // computer console
#define T_PIPE      15  // pipe/vent
#define T_FA        16  // font A-Z: 16..41
#define T_F0        42  // font 0-9: 42..51
#define T_SPC       52
#define T_COLON     53
#define T_DASH      54

// ── Patterns ────────────────────────────────────────────────────────
static const u8 PAT_FLOOR[8]  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const u8 PAT_WALL_H[8] = {0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF};
static const u8 PAT_WALL_V[8] = {0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3};
static const u8 PAT_CORNER[8] = {0xFF,0xFF,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0};
static const u8 PAT_DOOR[8]   = {0x00,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x00};
static const u8 PAT_SYS[8]    = {0x3C,0x42,0x99,0xA5,0xA5,0x99,0x42,0x3C};
static const u8 PAT_CORPSE[8] = {0x18,0x18,0x7E,0x18,0x18,0x24,0x42,0x00};
static const u8 PAT_TABLE[8]  = {0x7E,0xFF,0xFF,0x7E,0x18,0x18,0x18,0x3C};
static const u8 PAT_CHAIR[8]  = {0x18,0x3C,0x18,0x18,0x18,0x18,0x3C,0x3C};
static const u8 PAT_CONS[8]   = {0x7E,0x7E,0x7E,0x7E,0x42,0x42,0x7E,0x00};
static const u8 PAT_PIPE[8]   = {0x18,0x18,0x18,0xFF,0xFF,0x18,0x18,0x18};
static const u8 PAT_EMP[8]    = {0,0,0,0,0,0,0,0};

// Font 3x5 in 8x8 (same as tetris)
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
 {0,0,0,0,0,0,0,0},
 {0,0x00,0x10,0x00,0x00,0x10,0x00,0}, // colon
 {0,0x00,0x00,0x3C,0x00,0x00,0x00,0}, // dash
};

// Colors
static const u8 CL_FLOOR[8] = {0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11}; // black
static const u8 CL_WALL[8]  = {0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1}; // gray on black
static const u8 CL_DOOR[8]  = {0xB1,0xB1,0xB1,0xB1,0xB1,0xB1,0xB1,0xB1}; // yellow on black
static const u8 CL_SYSOK[8] = {0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31}; // green on black
static const u8 CL_SYSBAD[8]= {0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x81}; // red on black
static const u8 CL_CORPSE[8]= {0x61,0x61,0x61,0x61,0x61,0x61,0x61,0x61}; // dkred on black
static const u8 CL_FURN[8]  = {0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1}; // gray
static const u8 CL_FNT[8]   = {0xF1,0xF1,0xF1,0xF1,0xF1,0xF1,0xF1,0xF1}; // white
static const u8 CL_EMP[8]   = {0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11};

// Sprite pattern (astronaut 8x8)
static const u8 SPR_PLAYER[8] = {
    0x3C, 0x7E, 0xFF, 0xDB, 0xFF, 0x7E, 0x66, 0x66
};
// Player colors by PID (1-8)
static const u8 g_PlayerCol[8] = { 7, 8, 3, 11, 13, 5, 9, 15 };
// cyan, red, ltgreen, yellow, magenta, ltblue, ltred, white

// ── Room data ───────────────────────────────────────────────────────
// Each room: name, connections (N/S/E/W = room index, 0xFF=no door),
// system position (0xFF=none), and a simple procedural layout

typedef struct {
    const c8* name;
    u8 doorN, doorS, doorE, doorW; // connected room index (0xFF=none)
    u8 sysX, sysY;                 // system position (0xFF=none)
} RoomDef;

// Map:
// 0:REACTOR -- 1:PASILLO -- 2:CAFETERIA
//                 |
//            3:ELECTRICA -- 4:MOTOR
//                 |
//            5:MEDBAY -- 6:ESCOTILLA

static const RoomDef g_Rooms[NUM_ROOMS] = {
    // 0: REACTOR
    { "REACTOR",    0xFF, 0xFF, 1,    0xFF, 15, 11 },
    // 1: PASILLO
    { "PASILLO",    0xFF, 3,    2,    0,    0xFF, 0xFF },
    // 2: CAFETERIA
    { "CAFETERIA",  0xFF, 0xFF, 0xFF, 1,    15, 11 },
    // 3: ELECTRICA
    { "ELECTRICA",  1,    5,    4,    0xFF, 15, 11 },
    // 4: MOTOR
    { "MOTOR",      0xFF, 0xFF, 0xFF, 3,    15, 11 },
    // 5: MEDBAY
    { "MEDBAY",     3,    0xFF, 6,    0xFF, 15, 11 },
    // 6: ESCOTILLA
    { "ESCOTILLA",  0xFF, 0xFF, 0xFF, 5,    15, 11 },
};

// ── Variables ───────────────────────────────────────────────────────
static u8 g_NB[768];
static u8 g_CurRoom = 2;  // start in cafeteria
static u8 g_PX = 15, g_PY = 12; // player position (tile coords)
static u8 g_MoveDelay = 0;
static u8 g_Systems[NUM_ROOMS]; // 0=OK, 1=sabotaged (per room with system)

// ── Name buffer helpers ─────────────────────────────────────────────

static void NB_Flush(void) {
    VDP_WriteVRAM(g_NB, 0x1800, 0, 768);
}

#define NB_SET(x,y,v) do { \
    u16 _i=(u16)(y)*32+(x); g_NB[_i]=(v); \
} while(0)

static void NB_Text(u8 x, u8 y, const c8* s) {
    while(*s && x<32) {
        u8 ch=(u8)*s, t;
        if(ch>='A'&&ch<='Z') t=T_FA+ch-'A';
        else if(ch>='0'&&ch<='9') t=T_F0+ch-'0';
        else if(ch==':') t=T_COLON;
        else if(ch=='-') t=T_DASH;
        else t=T_SPC;
        NB_SET(x,y,t); x++; s++;
    }
}

// ── Load tileset ────────────────────────────────────────────────────
static void LoadTiles(void) {
    u8 b, t;
    u16 pb, cb;
    for(b = 0; b < 3; b++) {
        pb = b*2048; cb = 0x2000+pb;
        // 0: floor
        VDP_WriteVRAM(PAT_FLOOR,pb+0,0,8); VDP_WriteVRAM(CL_FLOOR,cb+0,0,8);
        // 1: wall H
        VDP_WriteVRAM(PAT_WALL_H,pb+1*8,0,8); VDP_WriteVRAM(CL_WALL,cb+1*8,0,8);
        // 2: wall V
        VDP_WriteVRAM(PAT_WALL_V,pb+2*8,0,8); VDP_WriteVRAM(CL_WALL,cb+2*8,0,8);
        // 3-6: corners (same pattern, different orientations — simplified)
        VDP_WriteVRAM(PAT_CORNER,pb+3*8,0,8); VDP_WriteVRAM(CL_WALL,cb+3*8,0,8);
        VDP_WriteVRAM(PAT_CORNER,pb+4*8,0,8); VDP_WriteVRAM(CL_WALL,cb+4*8,0,8);
        VDP_WriteVRAM(PAT_CORNER,pb+5*8,0,8); VDP_WriteVRAM(CL_WALL,cb+5*8,0,8);
        VDP_WriteVRAM(PAT_CORNER,pb+6*8,0,8); VDP_WriteVRAM(CL_WALL,cb+6*8,0,8);
        // 7: door H
        VDP_WriteVRAM(PAT_DOOR,pb+7*8,0,8); VDP_WriteVRAM(CL_DOOR,cb+7*8,0,8);
        // 8: door V
        VDP_WriteVRAM(PAT_DOOR,pb+8*8,0,8); VDP_WriteVRAM(CL_DOOR,cb+8*8,0,8);
        // 9: system OK
        VDP_WriteVRAM(PAT_SYS,pb+9*8,0,8); VDP_WriteVRAM(CL_SYSOK,cb+9*8,0,8);
        // 10: system BAD
        VDP_WriteVRAM(PAT_SYS,pb+10*8,0,8); VDP_WriteVRAM(CL_SYSBAD,cb+10*8,0,8);
        // 11: corpse
        VDP_WriteVRAM(PAT_CORPSE,pb+11*8,0,8); VDP_WriteVRAM(CL_CORPSE,cb+11*8,0,8);
        // 12: table
        VDP_WriteVRAM(PAT_TABLE,pb+12*8,0,8); VDP_WriteVRAM(CL_FURN,cb+12*8,0,8);
        // 13: chair
        VDP_WriteVRAM(PAT_CHAIR,pb+13*8,0,8); VDP_WriteVRAM(CL_FURN,cb+13*8,0,8);
        // 14: console
        VDP_WriteVRAM(PAT_CONS,pb+14*8,0,8); VDP_WriteVRAM(CL_FURN,cb+14*8,0,8);
        // 15: pipe
        VDP_WriteVRAM(PAT_PIPE,pb+15*8,0,8); VDP_WriteVRAM(CL_FURN,cb+15*8,0,8);
        // 16-41: font A-Z
        for(t=0;t<26;t++){
            VDP_WriteVRAM(FONT[t],pb+(16+t)*8,0,8);
            VDP_WriteVRAM(CL_FNT,cb+(16+t)*8,0,8);
        }
        // 42-51: font 0-9
        for(t=0;t<10;t++){
            VDP_WriteVRAM(FONT[26+t],pb+(42+t)*8,0,8);
            VDP_WriteVRAM(CL_FNT,cb+(42+t)*8,0,8);
        }
        // 52: space
        VDP_WriteVRAM(PAT_EMP,pb+52*8,0,8); VDP_WriteVRAM(CL_EMP,cb+52*8,0,8);
        // 53: colon
        VDP_WriteVRAM(FONT[36],pb+53*8,0,8); VDP_WriteVRAM(CL_FNT,cb+53*8,0,8);
        // 54: dash
        VDP_WriteVRAM(FONT[37],pb+54*8,0,8); VDP_WriteVRAM(CL_FNT,cb+54*8,0,8);
    }
}

// ── Draw room ───────────────────────────────────────────────────────

void DrawRoom(u8 roomIdx) {
    u16 i;
    u8 r, c;
    const RoomDef* rm = &g_Rooms[roomIdx];

    // Fill floor
    for(i = 0; i < 768; i++) g_NB[i] = T_FLOOR;

    // Walls: top row, bottom row, left col, right col
    for(c = 0; c < 32; c++) { NB_SET(c, 0, T_WALL_H); NB_SET(c, 22, T_WALL_H); }
    for(r = 0; r < 23; r++) { NB_SET(0, r, T_WALL_V); NB_SET(31, r, T_WALL_V); }

    // Corners
    NB_SET(0,  0,  T_CORNER_TL);
    NB_SET(31, 0,  T_CORNER_TR);
    NB_SET(0,  22, T_CORNER_BL);
    NB_SET(31, 22, T_CORNER_BR);

    // Doors
    if(rm->doorN != 0xFF) { NB_SET(15, 0, T_DOOR_H); NB_SET(16, 0, T_DOOR_H); }
    if(rm->doorS != 0xFF) { NB_SET(15, 22, T_DOOR_H); NB_SET(16, 22, T_DOOR_H); }
    if(rm->doorE != 0xFF) { NB_SET(31, 10, T_DOOR_V); NB_SET(31, 11, T_DOOR_V); }
    if(rm->doorW != 0xFF) { NB_SET(0, 10, T_DOOR_V); NB_SET(0, 11, T_DOOR_V); }

    // System
    if(rm->sysX != 0xFF) {
        u8 sysTile = g_Systems[roomIdx] ? T_SYS_BAD : T_SYS_OK;
        NB_SET(rm->sysX, rm->sysY, sysTile);
    }

    // Some furniture per room (simple)
    if(roomIdx == 2) { // Cafeteria: tables
        NB_SET(8, 8, T_TABLE); NB_SET(8, 9, T_CHAIR);
        NB_SET(14, 8, T_TABLE); NB_SET(14, 9, T_CHAIR);
        NB_SET(20, 8, T_TABLE); NB_SET(20, 9, T_CHAIR);
    }
    if(roomIdx == 0) { // Reactor: consoles
        NB_SET(10, 6, T_CONSOLE); NB_SET(20, 6, T_CONSOLE);
        NB_SET(10, 16, T_CONSOLE); NB_SET(20, 16, T_CONSOLE);
    }
    if(roomIdx == 3) { // Electrica: pipes
        NB_SET(5, 5, T_PIPE); NB_SET(5, 17, T_PIPE);
        NB_SET(26, 5, T_PIPE); NB_SET(26, 17, T_PIPE);
    }
    if(roomIdx == 4) { // Motor: consoles
        NB_SET(15, 5, T_CONSOLE); NB_SET(16, 5, T_CONSOLE);
        NB_SET(15, 17, T_CONSOLE); NB_SET(16, 17, T_CONSOLE);
    }

    // HUD: row 23
    for(c = 0; c < 32; c++) NB_SET(c, 23, T_SPC);
    NB_Text(1, 23, rm->name);
    NB_Text(16, 23, "ESPACIO ACCION");
}

// ── Collision ───────────────────────────────────────────────────────

u8 CanWalk(u8 x, u8 y) {
    u8 t;
    if(x >= 32 || y >= 23) return 0;
    t = g_NB[(u16)y * 32 + x];
    // Can walk on floor, doors, and space
    if(t == T_FLOOR || t == T_DOOR_H || t == T_DOOR_V || t == T_SPC) return 1;
    return 0;
}

// ── Check door transition ───────────────────────────────────────────

u8 CheckDoor(void) {
    const RoomDef* rm = &g_Rooms[g_CurRoom];

    // North door
    if(g_PY == 0 && (g_PX == 15 || g_PX == 16) && rm->doorN != 0xFF) {
        g_CurRoom = rm->doorN;
        g_PY = 21; // appear at bottom
        return 1;
    }
    // South door
    if(g_PY == 22 && (g_PX == 15 || g_PX == 16) && rm->doorS != 0xFF) {
        g_CurRoom = rm->doorS;
        g_PY = 1; // appear at top
        return 1;
    }
    // East door
    if(g_PX == 31 && (g_PY == 10 || g_PY == 11) && rm->doorE != 0xFF) {
        g_CurRoom = rm->doorE;
        g_PX = 1; // appear at left
        return 1;
    }
    // West door
    if(g_PX == 0 && (g_PY == 10 || g_PY == 11) && rm->doorW != 0xFF) {
        g_CurRoom = rm->doorW;
        g_PX = 30; // appear at right
        return 1;
    }
    return 0;
}

// ── Main ────────────────────────────────────────────────────────────

void main(void) {
    u8 i;
    u8 moved;

    *((u8*)0xF3DB) = 0; // no key click

    VDP_SetMode(VDP_MODE_SCREEN4);
    VDP_SetColor(1);
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_8 | VDP_SPRITE_SCALE_1);
    VDP_LoadSpritePattern(SPR_PLAYER, 0, 1);
    for(i = 0; i < 32; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);

    LoadTiles();

    // Init systems all OK
    for(i = 0; i < NUM_ROOMS; i++) g_Systems[i] = 0;

    // Draw initial room
    DrawRoom(g_CurRoom);
    Halt(); NB_Flush();

    // Game loop
    while(1) {
        Halt();
        NB_Flush();

        Keyboard_Update();
        *((u16*)0xF3F8) = *((u16*)0xF3FA);

        if(Keyboard_IsKeyPressed(KEY_ESC)) break;

        moved = 0;

        if(g_MoveDelay > 0) { g_MoveDelay--; }
        else {
            if(Keyboard_IsKeyPressed(KEY_UP) && CanWalk(g_PX, g_PY - 1))
                { g_PY--; moved = 1; g_MoveDelay = 3; }
            if(Keyboard_IsKeyPressed(KEY_DOWN) && CanWalk(g_PX, g_PY + 1))
                { g_PY++; moved = 1; g_MoveDelay = 3; }
            if(Keyboard_IsKeyPressed(KEY_LEFT) && CanWalk(g_PX - 1, g_PY))
                { g_PX--; moved = 1; g_MoveDelay = 3; }
            if(Keyboard_IsKeyPressed(KEY_RIGHT) && CanWalk(g_PX + 1, g_PY))
                { g_PX++; moved = 1; g_MoveDelay = 3; }
        }

        // Check door transition
        if(moved && CheckDoor()) {
            DrawRoom(g_CurRoom);
        }

        // Draw player sprite
        VDP_SetSpriteExUniColor(0, g_PX * 8, g_PY * 8 - 1, 0, g_PlayerCol[0]);
    }

    for(i = 0; i < 32; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);
    Bios_Exit(0);
}
